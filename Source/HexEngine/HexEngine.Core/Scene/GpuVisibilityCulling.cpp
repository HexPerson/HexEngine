#include "GpuVisibilityCulling.hpp"
#include "../HexEngine.hpp"
#include "../Entity/Component/Camera.hpp"
#include "../Graphics/Material.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace HexEngine
{
	HVar r_gpuCullEnable("r_gpuCullEnable", "Enable GPU frustum/occlusion culling for opaque pass", false, false, true);
	HVar r_gpuCullFrustum("r_gpuCullFrustum", "Enable GPU frustum culling stage", true, false, true);
	HVar r_gpuCullOcclusion("r_gpuCullOcclusion", "Enable GPU occlusion culling stage", true, false, true);
	HVar r_gpuCullDepthPrepassFallback("r_gpuCullDepthPrepassFallback", "Allow depth prepass fallback if history depth is unavailable", true, false, true);
	HVar r_gpuCullFreeze("r_gpuCullFreeze", "Freeze GPU culling visibility results for debugging", false, false, true);
	HVar r_gpuCullDebugBounds("r_gpuCullDebugBounds", "Draw bounds for GPU culling candidates", false, false, true);
	HVar r_gpuCullDebugFrustumRejected("r_gpuCullDebugFrustumRejected", "Draw frustum-rejected objects", false, false, true);
	HVar r_gpuCullDebugOcclusionRejected("r_gpuCullDebugOcclusionRejected", "Draw occlusion-rejected objects", false, false, true);
	HVar r_gpuCullGraceFrames("r_gpuCullGraceFrames", "Frames to keep an object visible after becoming occluded", 2, 0, 10);
	HVar r_gpuCullFastCameraDistance("r_gpuCullFastCameraDistance", "Camera movement threshold to suppress occlusion for a frame", 1.0f, 0.0f, 1000.0f);
	HVar r_gpuCullFastCameraAngleDeg("r_gpuCullFastCameraAngleDeg", "Camera rotation angle (degrees) threshold to suppress occlusion for a frame", 6.0f, 0.0f, 90.0f);
	HVar r_gpuCullNearBypassDistance("r_gpuCullNearBypassDistance", "Distance where objects bypass occlusion", 1.25f, 0.0f, 250.0f);
	HVar r_gpuCullLargeSphereBypass("r_gpuCullLargeSphereBypass", "Sphere radius where objects bypass occlusion", 5000.0f, 0.0f, 5000.0f);
	HVar r_gpuCullFrustumRadiusScale("r_gpuCullFrustumRadiusScale", "Radius scale used for conservative GPU frustum tests", 1.05f, 1.0f, 4.0f);
	HVar r_gpuCullOcclusionDepthBias("r_gpuCullOcclusionDepthBias", "Depth bias for conservative HZB tests", 0.0013f, 0.0f, 0.1f);
	HVar r_gpuCullMinCandidates("r_gpuCullMinCandidates", "Minimum candidate count before GPU culling path is used", 64, 0, 100000);
	HVar r_gpuCullOcclusionRejectFrames("r_gpuCullOcclusionRejectFrames", "Consecutive occluded frames required before culling", 2, 1, 8);
	HVar r_gpuCullOcclusionStableFrames("r_gpuCullOcclusionStableFrames", "Camera-stable frames required before enabling occlusion", 0, 0, 30);
	HVar r_gpuCullOcclusionAggressive("r_gpuCullOcclusionAggressive", "Use aggressive occlusion decision policy", true, false, true);

	namespace
	{
		static float ElapsedMs(const std::chrono::high_resolution_clock::time_point& start)
		{
			using namespace std::chrono;
			return duration<float, std::milli>(high_resolution_clock::now() - start).count();
		}

		static DXGI_FORMAT ResolveDepthSrvFormat(DXGI_FORMAT format)
		{
			switch (format)
			{
			case DXGI_FORMAT_R32_TYPELESS: return DXGI_FORMAT_R32_FLOAT;
			case DXGI_FORMAT_R24G8_TYPELESS: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
			case DXGI_FORMAT_R16_TYPELESS: return DXGI_FORMAT_R16_UNORM;
			default: return format;
			}
		}
	}

	void GpuVisibilityCulling::Create()
	{
		Destroy();

		_frustumCullShader = IShader::Create("EngineData.Shaders/GpuFrustumCull.hcs");
		_occlusionCullShader = IShader::Create("EngineData.Shaders/GpuOcclusionCull.hcs");
		_buildDepthPyramidShader = IShader::Create("EngineData.Shaders/BuildDepthPyramid.hcs");
		_cullConstantBuffer = g_pEnv->_graphicsDevice->CreateConstantBuffer(sizeof(GpuCullConstants));

		uint32_t width = 0;
		uint32_t height = 0;
		g_pEnv->_graphicsDevice->GetBackBufferDimensions(width, height);
		Resize(width, height);
	}

	void GpuVisibilityCulling::DestroyBuffers()
	{
		SAFE_RELEASE(_candidateSrv);
		SAFE_RELEASE(_candidateBuffer);
		SAFE_RELEASE(_frustumVisibilitySrv);
		SAFE_RELEASE(_frustumVisibilityUav);
		SAFE_RELEASE(_frustumVisibilityBuffer);
		SAFE_RELEASE(_finalVisibilityUav);
		SAFE_RELEASE(_finalVisibilityBuffer);

		for (auto& rb : _visibilityReadback)
			SAFE_RELEASE(rb);
		for (uint32_t i = 0; i < ReadbackLatencyFrames; ++i)
		{
			_visibilityReadbackCount[i] = 0;
			_visibilityReadbackReady[i] = false;
			_visibilityReadbackKeys[i].clear();
		}

		_candidateCapacity = 0;
		_lastDispatchCandidateCount = 0;
		_cpuVisibility.clear();
		_cpuVisibilityByEntity.clear();
		_frozenVisibility.clear();
		_graceFramesRemaining.clear();
		_occlusionRejectStreak.clear();
	}

	void GpuVisibilityCulling::DestroyHzbResources(HzbResources& resources)
	{
		for (auto* uav : resources.mipUavs)
			SAFE_RELEASE(uav);
		for (auto* srv : resources.mipSrvs)
			SAFE_RELEASE(srv);
		resources.mipUavs.clear();
		resources.mipSrvs.clear();
		SAFE_RELEASE(resources.srv);
		SAFE_RELEASE(resources.texture);
	}

	void GpuVisibilityCulling::Destroy()
	{
		DestroyBuffers();
		DestroyHzbResources(_hzbRead);
		DestroyHzbResources(_hzbWrite);
		_hzbHistoryValid = false;
		_hzbMipCount = 0;
		_hzbWidth = 0;
		_hzbHeight = 0;
		SAFE_DELETE(_cullConstantBuffer);
	}

	void GpuVisibilityCulling::Resize(uint32_t width, uint32_t height)
	{
		DestroyHzbResources(_hzbRead);
		DestroyHzbResources(_hzbWrite);
		_hzbHistoryValid = false;
		_hzbWidth = width;
		_hzbHeight = height;

		if (width == 0 || height == 0)
		{
			_hzbMipCount = 0;
			return;
		}

		const uint32_t maxDim = std::max(width, height);
		_hzbMipCount = static_cast<uint32_t>(std::floor(std::log2(static_cast<float>(maxDim)))) + 1u;
	}

	void GpuVisibilityCulling::EnsureCandidateCapacity(uint32_t candidateCount)
	{
		if (candidateCount <= _candidateCapacity)
			return;

		const uint32_t newCapacity = std::max(candidateCount, (_candidateCapacity * 2u) + 256u);
		DestroyBuffers();

		ID3D11Device* device = reinterpret_cast<ID3D11Device*>(g_pEnv->_graphicsDevice->GetNativeDevice());
		if (!device)
			return;

		D3D11_BUFFER_DESC candidateDesc = {};
		candidateDesc.ByteWidth = static_cast<UINT>(sizeof(GpuCullCandidate) * newCapacity);
		candidateDesc.Usage = D3D11_USAGE_DYNAMIC;
		candidateDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		candidateDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		candidateDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		candidateDesc.StructureByteStride = sizeof(GpuCullCandidate);
		if (FAILED(device->CreateBuffer(&candidateDesc, nullptr, &_candidateBuffer)) || _candidateBuffer == nullptr)
			return;

		D3D11_SHADER_RESOURCE_VIEW_DESC candidateSrvDesc = {};
		candidateSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
		candidateSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		candidateSrvDesc.Buffer.FirstElement = 0;
		candidateSrvDesc.Buffer.NumElements = newCapacity;
		if (FAILED(device->CreateShaderResourceView(_candidateBuffer, &candidateSrvDesc, &_candidateSrv)) || _candidateSrv == nullptr)
			return;

		D3D11_BUFFER_DESC visDesc = {};
		visDesc.ByteWidth = static_cast<UINT>(sizeof(uint32_t) * newCapacity);
		visDesc.Usage = D3D11_USAGE_DEFAULT;
		visDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		visDesc.CPUAccessFlags = 0;
		visDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		visDesc.StructureByteStride = sizeof(uint32_t);

		if (FAILED(device->CreateBuffer(&visDesc, nullptr, &_frustumVisibilityBuffer)) || _frustumVisibilityBuffer == nullptr)
			return;
		if (FAILED(device->CreateBuffer(&visDesc, nullptr, &_finalVisibilityBuffer)) || _finalVisibilityBuffer == nullptr)
			return;

		D3D11_SHADER_RESOURCE_VIEW_DESC visSrvDesc = {};
		visSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
		visSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		visSrvDesc.Buffer.FirstElement = 0;
		visSrvDesc.Buffer.NumElements = newCapacity;
		CHECK_HR(device->CreateShaderResourceView(_frustumVisibilityBuffer, &visSrvDesc, &_frustumVisibilitySrv));

		D3D11_UNORDERED_ACCESS_VIEW_DESC visUavDesc = {};
		visUavDesc.Format = DXGI_FORMAT_UNKNOWN;
		visUavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		visUavDesc.Buffer.FirstElement = 0;
		visUavDesc.Buffer.NumElements = newCapacity;
		CHECK_HR(device->CreateUnorderedAccessView(_frustumVisibilityBuffer, &visUavDesc, &_frustumVisibilityUav));
		CHECK_HR(device->CreateUnorderedAccessView(_finalVisibilityBuffer, &visUavDesc, &_finalVisibilityUav));

		for (auto& rb : _visibilityReadback)
		{
			D3D11_BUFFER_DESC rbDesc = {};
			rbDesc.ByteWidth = visDesc.ByteWidth;
			rbDesc.Usage = D3D11_USAGE_STAGING;
			rbDesc.BindFlags = 0;
			rbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			rbDesc.MiscFlags = 0;
			rbDesc.StructureByteStride = 0;
			CHECK_HR(device->CreateBuffer(&rbDesc, nullptr, &rb));
		}

		_cpuVisibility.assign(newCapacity, 1u);
		_candidateCapacity = newCapacity;
	}

	void GpuVisibilityCulling::EnsureHzb(uint32_t width, uint32_t height)
	{
		if (width == 0 || height == 0)
			return;

		if (_hzbRead.texture != nullptr && _hzbWrite.texture != nullptr && _hzbWidth == width && _hzbHeight == height)
			return;

		Resize(width, height);

		ID3D11Device* device = reinterpret_cast<ID3D11Device*>(g_pEnv->_graphicsDevice->GetNativeDevice());
		if (!device)
			return;

		auto createHzbResources = [&](HzbResources& resources)
		{
			D3D11_TEXTURE2D_DESC texDesc = {};
			texDesc.Width = width;
			texDesc.Height = height;
			texDesc.MipLevels = _hzbMipCount;
			texDesc.ArraySize = 1;
			texDesc.Format = DXGI_FORMAT_R32_FLOAT;
			texDesc.SampleDesc.Count = 1;
			texDesc.Usage = D3D11_USAGE_DEFAULT;
			texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			texDesc.CPUAccessFlags = 0;
			texDesc.MiscFlags = 0;

			CHECK_HR(device->CreateTexture2D(&texDesc, nullptr, &resources.texture));

			D3D11_SHADER_RESOURCE_VIEW_DESC fullSrvDesc = {};
			fullSrvDesc.Format = texDesc.Format;
			fullSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			fullSrvDesc.Texture2D.MostDetailedMip = 0;
			fullSrvDesc.Texture2D.MipLevels = _hzbMipCount;
			CHECK_HR(device->CreateShaderResourceView(resources.texture, &fullSrvDesc, &resources.srv));

			resources.mipSrvs.resize(_hzbMipCount);
			resources.mipUavs.resize(_hzbMipCount);
			for (uint32_t mip = 0; mip < _hzbMipCount; ++mip)
			{
				D3D11_SHADER_RESOURCE_VIEW_DESC mipSrvDesc = {};
				mipSrvDesc.Format = texDesc.Format;
				mipSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				mipSrvDesc.Texture2D.MostDetailedMip = mip;
				mipSrvDesc.Texture2D.MipLevels = 1;
				CHECK_HR(device->CreateShaderResourceView(resources.texture, &mipSrvDesc, &resources.mipSrvs[mip]));

				D3D11_UNORDERED_ACCESS_VIEW_DESC mipUavDesc = {};
				mipUavDesc.Format = texDesc.Format;
				mipUavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
				mipUavDesc.Texture2D.MipSlice = mip;
				CHECK_HR(device->CreateUnorderedAccessView(resources.texture, &mipUavDesc, &resources.mipUavs[mip]));
			}
		};

		createHzbResources(_hzbRead);
		createHzbResources(_hzbWrite);
		_hzbHistoryValid = false;
	}

	void GpuVisibilityCulling::BeginFrame(uint64_t frameIndex, Camera* camera)
	{
		_frameIndex = frameIndex;
		_stats = {};
		_cameraMovedFastThisFrame = false;
		_cameraRotatedFastThisFrame = false;

		if (camera != nullptr && camera->GetEntity() != nullptr)
		{
			const math::Vector3 currentPos = camera->GetEntity()->GetPosition();
			math::Vector3 currentLookDir = camera->GetLookDir();
			if (currentLookDir.LengthSquared() > 1e-6f)
			{
				currentLookDir.Normalize();
			}
			else
			{
				currentLookDir = math::Vector3::Forward;
			}

			if (_cameraPosValid)
			{
				_cameraMovedFastThisFrame = (currentPos - _lastCameraPos).Length() > r_gpuCullFastCameraDistance._val.f32;
			}
			_lastCameraPos = currentPos;
			_cameraPosValid = true;

			if (_cameraLookValid)
			{
				const float dotRaw =
					(_lastCameraLookDir.x * currentLookDir.x) +
					(_lastCameraLookDir.y * currentLookDir.y) +
					(_lastCameraLookDir.z * currentLookDir.z);
				const float dotVal = std::clamp(dotRaw, -1.0f, 1.0f);
				const float angleDeg = std::acos(dotVal) * (180.0f / 3.14159265358979323846f);
				_cameraRotatedFastThisFrame = angleDeg > r_gpuCullFastCameraAngleDeg._val.f32;
			}
			_lastCameraLookDir = currentLookDir;
			_cameraLookValid = true;

			if (_cameraMovedFastThisFrame || _cameraRotatedFastThisFrame)
			{
				_cameraStableFrames = 0;
			}
			else
			{
				_cameraStableFrames++;
			}
		}
		else
		{
			_cameraPosValid = false;
			_cameraLookValid = false;
			_cameraStableFrames = 0;
		}
	}

	void GpuVisibilityCulling::BuildFrustumPlanes(const math::Matrix& viewProjection, math::Vector4 outPlanes[6]) const
	{
		const math::Matrix m = viewProjection;

		outPlanes[0] = math::Vector4(m._14 + m._11, m._24 + m._21, m._34 + m._31, m._44 + m._41);
		outPlanes[1] = math::Vector4(m._14 - m._11, m._24 - m._21, m._34 - m._31, m._44 - m._41);
		outPlanes[2] = math::Vector4(m._14 - m._12, m._24 - m._22, m._34 - m._32, m._44 - m._42);
		outPlanes[3] = math::Vector4(m._14 + m._12, m._24 + m._22, m._34 + m._32, m._44 + m._42);
		outPlanes[4] = math::Vector4(m._13, m._23, m._33, m._43);
		outPlanes[5] = math::Vector4(m._14 - m._13, m._24 - m._23, m._34 - m._33, m._44 - m._43);

		for (int32_t i = 0; i < 6; ++i)
		{
			const math::Vector3 n(outPlanes[i].x, outPlanes[i].y, outPlanes[i].z);
			const float len = std::max(n.Length(), 1e-5f);
			outPlanes[i] /= len;
		}
	}

	bool GpuVisibilityCulling::IsTransparentMaterial(const Material* material) const
	{
		if (!material)
			return false;

		if (material->_properties.hasTransparency == 1 || material->_properties.isWater == 1)
			return true;

		return material->GetBlendState() != BlendState::Opaque;
	}

bool GpuVisibilityCulling::ShouldBypassOcclusion(const RenderableSnapshot& snapshot, const math::Vector3& cameraPos) const
{
	if (!snapshot.entity)
		return true;

	const auto& sphere = snapshot.entity->GetWorldBoundingSphere();
	const float distance = (sphere.Center - cameraPos).Length();
	if (distance <= r_gpuCullNearBypassDistance._val.f32)
		return true;
		if (sphere.Radius >= r_gpuCullLargeSphereBypass._val.f32)
			return true;

		return false;
	}

	uint64_t GpuVisibilityCulling::MakeEntityKey(const RenderableSnapshot& snapshot) const
	{
		if (!snapshot.entity)
			return 0ull;

		const auto entityId = snapshot.entity->GetId();
		return (static_cast<uint64_t>(entityId.generation) << 32ull) | static_cast<uint64_t>(entityId.index);
	}

	void GpuVisibilityCulling::ClearSnapshotFlags(RenderBatchSnapshot& snapshot)
	{
		uint32_t stableIndex = 0;
		for (auto& batch : snapshot)
		{
			for (auto& renderable : batch.second)
			{
				renderable.stableIndex = stableIndex++;
				renderable.cullEligible = false;
				renderable.forceVisible = true;
				renderable.gpuVisible = true;
				renderable.culledByFrustum = false;
				renderable.culledByOcclusion = false;
			}
		}
	}

bool GpuVisibilityCulling::GatherCandidates(
	RenderBatchSnapshot& snapshot,
	std::vector<GpuCullCandidate>& outCandidates,
	std::vector<RenderableSnapshot*>& outRenderableMap,
	const math::Vector3& cameraPos,
	LayerMask layerMask)
{
	outCandidates.clear();
	outRenderableMap.clear();

		for (auto& batch : snapshot)
		{
			auto material = batch.first;
			const bool transparent = IsTransparentMaterial(material.get());

			for (auto& renderable : batch.second)
			{
				renderable.cullEligible = false;
				renderable.forceVisible = true;
				renderable.gpuVisible = true;
				renderable.culledByFrustum = false;
				renderable.culledByOcclusion = false;

				if (!renderable.entity || transparent)
					continue;

				if ((layerMask & LAYERMASK(renderable.layer)) == 0)
					continue;

				if (renderable.layer == Layer::Sky)
					continue;

				auto worldSphere = renderable.entity->GetWorldBoundingSphere();
				if (worldSphere.Radius <= 0.0f)
					continue;

				GpuCullCandidate candidate = {};
				candidate.sphereWs = math::Vector4(worldSphere.Center.x, worldSphere.Center.y, worldSphere.Center.z, worldSphere.Radius * r_gpuCullFrustumRadiusScale._val.f32);

				const auto worldOcclusion = renderable.entity->GetWorldOcclusionVolume();
				candidate.occlusionCenterExtent = math::Vector4(
					worldOcclusion.Center.x,
					worldOcclusion.Center.y,
					worldOcclusion.Center.z,
					std::max({ worldOcclusion.Extents.x, worldOcclusion.Extents.y, worldOcclusion.Extents.z, 0.01f }));

				candidate.stableIndex = renderable.stableIndex;
				const uint64_t key = MakeEntityKey(renderable);
				candidate.entityKeyLo = static_cast<uint32_t>(key & 0xffffffffull);
				candidate.entityKeyHi = static_cast<uint32_t>((key >> 32ull) & 0xffffffffull);
				candidate.flags = 0u;

			if (ShouldBypassOcclusion(renderable, cameraPos))
				{
					candidate.flags |= CandidateForceVisible;
				}
				else
				{
					candidate.flags |= CandidateOcclusionEligible;
				}

				renderable.cullEligible = true;
				renderable.forceVisible = (candidate.flags & CandidateForceVisible) != 0u;

				outRenderableMap.push_back(&renderable);
				outCandidates.push_back(candidate);
			}
		}

		return !outCandidates.empty();
	}

	void GpuVisibilityCulling::DispatchFrustumPass(ID3D11DeviceContext* context, uint32_t candidateCount)
	{
		if (!context || !_frustumCullShader || !_candidateSrv || !_frustumVisibilityUav)
			return;

		if (auto* stage = _frustumCullShader->GetShaderStage(ShaderStage::ComputeShader); stage != nullptr)
		{
			ID3D11ShaderResourceView* srvs[1] = { _candidateSrv };
			ID3D11UnorderedAccessView* uavs[1] = { _frustumVisibilityUav };
			ID3D11Buffer* cbs[1] = { _cullConstantBuffer ? reinterpret_cast<ID3D11Buffer*>(_cullConstantBuffer->GetNativePtr()) : nullptr };

			context->CSSetShaderResources(0, 1, srvs);
			context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
			context->CSSetConstantBuffers(5, 1, cbs);
			context->CSSetShader(reinterpret_cast<ID3D11ComputeShader*>(stage->GetNativePtr()), nullptr, 0);
			context->Dispatch(std::max<uint32_t>((candidateCount + 63u) / 64u, 1u), 1u, 1u);

			ID3D11ShaderResourceView* nullSrv[1] = {};
			ID3D11UnorderedAccessView* nullUav[1] = {};
			ID3D11Buffer* nullCb[1] = {};
			context->CSSetShaderResources(0, 1, nullSrv);
			context->CSSetUnorderedAccessViews(0, 1, nullUav, nullptr);
			context->CSSetConstantBuffers(5, 1, nullCb);
			context->CSSetShader(nullptr, nullptr, 0);
		}
	}

	void GpuVisibilityCulling::DispatchOcclusionPass(ID3D11DeviceContext* context, uint32_t candidateCount, bool useOcclusion)
	{
		if (!context || !_occlusionCullShader || !_candidateSrv || !_frustumVisibilitySrv || !_finalVisibilityUav)
			return;

		if (auto* stage = _occlusionCullShader->GetShaderStage(ShaderStage::ComputeShader); stage != nullptr)
		{
			ID3D11ShaderResourceView* srvs[3] = { _candidateSrv, _frustumVisibilitySrv, _hzbRead.srv };
			if (!useOcclusion)
				srvs[2] = nullptr;
			ID3D11UnorderedAccessView* uavs[1] = { _finalVisibilityUav };
			ID3D11Buffer* cbs[1] = { _cullConstantBuffer ? reinterpret_cast<ID3D11Buffer*>(_cullConstantBuffer->GetNativePtr()) : nullptr };

			context->CSSetShaderResources(0, 3, srvs);
			context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
			context->CSSetConstantBuffers(5, 1, cbs);
			context->CSSetShader(reinterpret_cast<ID3D11ComputeShader*>(stage->GetNativePtr()), nullptr, 0);
			context->Dispatch(std::max<uint32_t>((candidateCount + 63u) / 64u, 1u), 1u, 1u);

			ID3D11ShaderResourceView* nullSrv[3] = {};
			ID3D11UnorderedAccessView* nullUav[1] = {};
			ID3D11Buffer* nullCb[1] = {};
			context->CSSetShaderResources(0, 3, nullSrv);
			context->CSSetUnorderedAccessViews(0, 1, nullUav, nullptr);
			context->CSSetConstantBuffers(5, 1, nullCb);
			context->CSSetShader(nullptr, nullptr, 0);
		}
	}

	bool GpuVisibilityCulling::ReadbackVisibility(const std::vector<GpuCullCandidate>& candidates, uint32_t& outResultCount)
	{
		outResultCount = 0;
		const uint32_t candidateCount = static_cast<uint32_t>(candidates.size());

		if (candidateCount == 0 || !_finalVisibilityBuffer)
			return false;

		ID3D11DeviceContext* context = reinterpret_cast<ID3D11DeviceContext*>(g_pEnv->_graphicsDevice->GetNativeDeviceContext());
		if (!context)
			return false;

		const uint32_t writeIndex = static_cast<uint32_t>(_frameIndex % ReadbackLatencyFrames);
		const uint32_t readIndex = static_cast<uint32_t>((_frameIndex + 1ull) % ReadbackLatencyFrames);

		if (_visibilityReadback[writeIndex] == nullptr || _visibilityReadback[readIndex] == nullptr)
			return false;

		context->CopyResource(_visibilityReadback[writeIndex], _finalVisibilityBuffer);
		_visibilityReadbackCount[writeIndex] = candidateCount;
		_visibilityReadbackReady[writeIndex] = true;
		_visibilityReadbackKeys[writeIndex].resize(candidateCount);
		for (uint32_t i = 0; i < candidateCount; ++i)
		{
			_visibilityReadbackKeys[writeIndex][i] =
				(static_cast<uint64_t>(candidates[i].entityKeyHi) << 32ull) |
				static_cast<uint64_t>(candidates[i].entityKeyLo);
		}

		if (!_visibilityReadbackReady[readIndex])
			return false;

		D3D11_MAPPED_SUBRESOURCE mapped = {};
		// Non-blocking readback: never stall the render thread waiting for GPU completion.
		// If results are not ready yet, we keep the previous visibility and fall back conservatively.
		if (FAILED(context->Map(_visibilityReadback[readIndex], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped)))
			return false;

		const auto* visible = reinterpret_cast<const uint32_t*>(mapped.pData);
		const uint32_t resultCount = _visibilityReadbackCount[readIndex];
		const uint32_t copyCount = std::min<uint32_t>(resultCount, static_cast<uint32_t>(_cpuVisibility.size()));
		const uint32_t keyCount = static_cast<uint32_t>(_visibilityReadbackKeys[readIndex].size());
		const uint32_t resultWithKeys = std::min(copyCount, keyCount);
		_cpuVisibilityByEntity.clear();
		_cpuVisibilityByEntity.reserve(resultWithKeys);
		for (uint32_t i = 0; i < copyCount; ++i)
		{
			_cpuVisibility[i] = visible[i];
			if (i < resultWithKeys)
			{
				_cpuVisibilityByEntity[_visibilityReadbackKeys[readIndex][i]] = visible[i];
			}
		}
		context->Unmap(_visibilityReadback[readIndex], 0);
		outResultCount = resultCount;
		return true;
	}

	bool GpuVisibilityCulling::CullOpaqueRenderables(
		RenderBatchSnapshot& snapshot,
		Camera* camera,
		LayerMask layerMask,
		MeshRenderFlags renderFlags)
	{
		ClearSnapshotFlags(snapshot);

		if (!r_gpuCullEnable._val.b)
			return false;

		const bool isOpaquePass = (renderFlags & MeshRenderFlags::MeshRenderNormal) != 0 &&
			(renderFlags & MeshRenderFlags::MeshRenderTransparency) == 0 &&
			(renderFlags & MeshRenderFlags::MeshRenderShadowMap) == 0;
		if (!isOpaquePass)
			return false;

		if (!camera)
			return false;
		if (camera->GetEntity() == nullptr)
			return false;

		const math::Matrix viewMatrix = camera->GetViewMatrix();
		const math::Matrix projectionMatrix = camera->GetProjectionMatrix();
		const auto viewport = camera->GetViewport();
		const math::Vector3 cameraPos = camera->GetEntity()->GetPosition();

		auto buildStart = std::chrono::high_resolution_clock::now();

		std::vector<GpuCullCandidate> candidates;
		std::vector<RenderableSnapshot*> renderableMap;
		if (!GatherCandidates(snapshot, candidates, renderableMap, cameraPos, layerMask))
			return false;

		_stats.totalCandidates = static_cast<uint32_t>(candidates.size());
		if (_stats.totalCandidates < static_cast<uint32_t>(std::max(0, r_gpuCullMinCandidates._val.i32)))
			return false;

		EnsureCandidateCapacity(_stats.totalCandidates);

		if (!_candidateBuffer || !_candidateSrv || !_frustumVisibilityBuffer || !_finalVisibilityBuffer)
			return false;

		ID3D11DeviceContext* context = reinterpret_cast<ID3D11DeviceContext*>(g_pEnv->_graphicsDevice->GetNativeDeviceContext());
		if (!context)
			return false;

		{
			D3D11_MAPPED_SUBRESOURCE mappedCandidates = {};
			if (FAILED(context->Map(_candidateBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedCandidates)) || mappedCandidates.pData == nullptr)
				return false;

			const size_t candidateBytes = static_cast<size_t>(_stats.totalCandidates) * sizeof(GpuCullCandidate);
			memcpy(mappedCandidates.pData, candidates.data(), candidateBytes);
			context->Unmap(_candidateBuffer, 0);
		}

		alignas(16) GpuCullConstants constants = {};
		constants.view = viewMatrix.Transpose();
		constants.projection = projectionMatrix.Transpose();
		const math::Matrix viewProjection = viewMatrix * projectionMatrix;
		constants.viewProjection = viewProjection.Transpose();
		BuildFrustumPlanes(viewProjection, constants.frustumPlanes);
		constants.cameraPos = math::Vector4(cameraPos.x, cameraPos.y, cameraPos.z, 1.0f);
		const auto& vp = viewport;
		constants.viewportSizeInvSize = math::Vector4(vp.width, vp.height, vp.width > 0.0f ? 1.0f / vp.width : 0.0f, vp.height > 0.0f ? 1.0f / vp.height : 0.0f);
		constants.hzbInfo = math::Vector4(static_cast<float>(_hzbWidth), static_cast<float>(_hzbHeight), static_cast<float>(_hzbMipCount), _hzbHistoryValid ? 1.0f : 0.0f);
		const bool occlusionEnabled = r_gpuCullOcclusion._val.b &&
			_hzbHistoryValid &&
			(_cameraStableFrames >= static_cast<uint32_t>(std::max(0, r_gpuCullOcclusionStableFrames._val.i32))) &&
			!r_gpuCullFreeze._val.b;
		constants.cullParams0 = math::Vector4(
			r_gpuCullFrustum._val.b ? 1.0f : 0.0f,
			occlusionEnabled ? 1.0f : 0.0f,
			r_gpuCullOcclusionDepthBias._val.f32,
			static_cast<float>(std::max(0, r_gpuCullGraceFrames._val.i32)));
		constants.cullParams1 = math::Vector4(
			r_gpuCullNearBypassDistance._val.f32,
			r_gpuCullLargeSphereBypass._val.f32,
			r_gpuCullFrustumRadiusScale._val.f32,
			r_gpuCullOcclusionAggressive._val.b ? 1.0f : 0.0f);

		if (_cullConstantBuffer)
			_cullConstantBuffer->Write(&constants, sizeof(constants));

		auto frustumStart = std::chrono::high_resolution_clock::now();
		DispatchFrustumPass(context, _stats.totalCandidates);
		_stats.gpuFrustumMs = ElapsedMs(frustumStart);

		auto occlusionStart = std::chrono::high_resolution_clock::now();
		DispatchOcclusionPass(context, _stats.totalCandidates, occlusionEnabled);
		_stats.gpuOcclusionMs = ElapsedMs(occlusionStart);
		_stats.usedOcclusion = occlusionEnabled;

		uint32_t resultCount = 0;
		const bool hasResults = ReadbackVisibility(candidates, resultCount);
		if (hasResults)
		{
			_lastDispatchCandidateCount = resultCount;
		}
		_stats.cpuBuildMs = ElapsedMs(buildStart);

		// Stability first: never consume stale delayed-readback visibility.
		// If this frame has no fresh mapped results, skip GPU visibility application.
		if (!hasResults)
		{
			_cpuVisibilityByEntity.clear();
			_lastDispatchCandidateCount = 0;
			return false;
		}

		for (uint32_t i = 0; i < _stats.totalCandidates; ++i)
		{
			auto* renderable = renderableMap[i];
			if (!renderable)
				continue;

			bool frustumVisible = true;
			bool finalVisible = true;
			const uint64_t entityKey = MakeEntityKey(*renderable);
			if (const auto it = _cpuVisibilityByEntity.find(entityKey); it != _cpuVisibilityByEntity.end())
			{
				const uint32_t result = it->second;
				frustumVisible = (result & 0x1u) != 0u;
				finalVisible = (result & 0x2u) != 0u;
			}

			const bool wasFrozenVisible = _frozenVisibility.contains(entityKey) ? _frozenVisibility[entityKey] : true;

			if (r_gpuCullFreeze._val.b)
			{
				finalVisible = wasFrozenVisible;
			}
			else
			{
				_frozenVisibility[entityKey] = finalVisible;
			}

			// Delayed readback + camera motion can produce unstable occlusion decisions.
			// Suppress occlusion-based hiding only for very aggressive camera motion.
			if (_cameraMovedFastThisFrame && _cameraRotatedFastThisFrame && frustumVisible)
			{
				finalVisible = true;
			}

			const bool occlusionRejectedRaw = frustumVisible && !finalVisible;
			if (occlusionRejectedRaw && !renderable->forceVisible)
			{
				auto& streak = _occlusionRejectStreak[entityKey];
				++streak;
				if (streak < static_cast<uint32_t>(std::max(1, r_gpuCullOcclusionRejectFrames._val.i32)))
				{
					finalVisible = true;
				}
			}
			else
			{
				_occlusionRejectStreak[entityKey] = 0;
			}

			if (!finalVisible && !renderable->forceVisible)
			{
				auto& grace = _graceFramesRemaining[entityKey];
				if (grace > 0)
				{
					finalVisible = true;
					--grace;
				}
				else
				{
					grace = static_cast<uint32_t>(std::max(0, r_gpuCullGraceFrames._val.i32));
				}
			}
			else
			{
				_graceFramesRemaining[entityKey] = static_cast<uint32_t>(std::max(0, r_gpuCullGraceFrames._val.i32));
			}

			renderable->gpuVisible = finalVisible || renderable->forceVisible;
			renderable->culledByFrustum = !frustumVisible;
			renderable->culledByOcclusion = frustumVisible && !finalVisible;

			if (!renderable->gpuVisible)
			{
				if (renderable->culledByFrustum)
					_stats.frustumRejected++;
				else if (renderable->culledByOcclusion)
					_stats.occlusionRejected++;
			}
			else
			{
				_stats.visibleInstances++;
			}
		}

		return true;
	}

	void GpuVisibilityCulling::BuildDepthPyramid(ITexture2D* depthSource)
	{
		if (!_buildDepthPyramidShader || depthSource == nullptr)
			return;

		auto* depthTexture = reinterpret_cast<ID3D11Texture2D*>(depthSource->GetNativePtr());
		if (!depthTexture)
			return;

		D3D11_TEXTURE2D_DESC depthDesc = {};
		depthTexture->GetDesc(&depthDesc);
		EnsureHzb(depthDesc.Width, depthDesc.Height);
		if (!_hzbWrite.texture || !_hzbWrite.srv || _hzbWrite.mipUavs.empty())
			return;

		ID3D11Device* device = reinterpret_cast<ID3D11Device*>(g_pEnv->_graphicsDevice->GetNativeDevice());
		ID3D11DeviceContext* context = reinterpret_cast<ID3D11DeviceContext*>(g_pEnv->_graphicsDevice->GetNativeDeviceContext());
		if (!device || !context)
			return;

		// If the source depth is still bound as OM depth output, unbind it before SRV use to avoid D3D11 hazards.
		ID3D11RenderTargetView* boundRtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
		ID3D11DepthStencilView* boundDsv = nullptr;
		context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, boundRtvs, &boundDsv);

		bool restoreOmTargets = false;
		const auto releaseOmBindings = [&]()
		{
			if (restoreOmTargets)
			{
				context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, boundRtvs, boundDsv);
			}
			for (auto*& rtv : boundRtvs)
				SAFE_RELEASE(rtv);
			SAFE_RELEASE(boundDsv);
		};
		if (boundDsv != nullptr)
		{
			ID3D11Resource* dsvResource = nullptr;
			boundDsv->GetResource(&dsvResource);
			if (dsvResource == depthTexture)
			{
				restoreOmTargets = true;
				context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, boundRtvs, nullptr);
			}
			SAFE_RELEASE(dsvResource);
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC depthSrvDesc = {};
		depthSrvDesc.Format = ResolveDepthSrvFormat(depthDesc.Format);
		depthSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		depthSrvDesc.Texture2D.MostDetailedMip = 0;
		depthSrvDesc.Texture2D.MipLevels = 1;

		ID3D11ShaderResourceView* depthSrv = nullptr;
		if (FAILED(device->CreateShaderResourceView(depthTexture, &depthSrvDesc, &depthSrv)) || !depthSrv)
		{
			releaseOmBindings();
			return;
		}

		auto* stage = _buildDepthPyramidShader->GetShaderStage(ShaderStage::ComputeShader);
		if (!stage)
		{
			SAFE_RELEASE(depthSrv);
			releaseOmBindings();
			return;
		}

		ID3D11Buffer* cbs[1] = { _cullConstantBuffer ? reinterpret_cast<ID3D11Buffer*>(_cullConstantBuffer->GetNativePtr()) : nullptr };
		context->CSSetConstantBuffers(5, 1, cbs);
		context->CSSetShader(reinterpret_cast<ID3D11ComputeShader*>(stage->GetNativePtr()), nullptr, 0);

		uint32_t srcWidth = _hzbWidth;
		uint32_t srcHeight = _hzbHeight;
		for (uint32_t mip = 0; mip < _hzbMipCount; ++mip)
		{
			// mip0 is a full-resolution copy from depth source.
			// Higher mips are 2x downsampled from previous HZB mip.
			const uint32_t dstWidth = (mip == 0) ? _hzbWidth : std::max(1u, _hzbWidth >> mip);
			const uint32_t dstHeight = (mip == 0) ? _hzbHeight : std::max(1u, _hzbHeight >> mip);
			srcWidth = (mip == 0) ? _hzbWidth : std::max(1u, _hzbWidth >> (mip - 1u));
			srcHeight = (mip == 0) ? _hzbHeight : std::max(1u, _hzbHeight >> (mip - 1u));

			GpuCullConstants constants = {};
			constants.hzbInfo = math::Vector4(
				static_cast<float>(srcWidth),
				static_cast<float>(srcHeight),
				static_cast<float>(mip),
				static_cast<float>(_hzbMipCount));
			if (_cullConstantBuffer)
				_cullConstantBuffer->Write(&constants, sizeof(constants));

			ID3D11ShaderResourceView* srcSrv = depthSrv;
			if (mip > 0)
			{
				// D3D11 does not allow SRV+UAV aliasing on the same texture resource.
				// Stage previous mip into the opposite pyramid resource, then sample from there.
				context->CopySubresourceRegion(
					_hzbRead.texture,
					mip - 1,
					0, 0, 0,
					_hzbWrite.texture,
					mip - 1,
					nullptr);
				srcSrv = _hzbRead.mipSrvs[mip - 1];
			}
			ID3D11UnorderedAccessView* dstUav = _hzbWrite.mipUavs[mip];
			context->CSSetShaderResources(0, 1, &srcSrv);
			context->CSSetUnorderedAccessViews(0, 1, &dstUav, nullptr);
			context->Dispatch(std::max<uint32_t>((dstWidth + 7u) / 8u, 1u), std::max<uint32_t>((dstHeight + 7u) / 8u, 1u), 1u);

			ID3D11ShaderResourceView* nullSrv[1] = {};
			ID3D11UnorderedAccessView* nullUav[1] = {};
			context->CSSetShaderResources(0, 1, nullSrv);
			context->CSSetUnorderedAccessViews(0, 1, nullUav, nullptr);
		}

		ID3D11Buffer* nullCb[1] = {};
		context->CSSetConstantBuffers(5, 1, nullCb);
		context->CSSetShader(nullptr, nullptr, 0);

		SAFE_RELEASE(depthSrv);
		releaseOmBindings();

		std::swap(_hzbRead, _hzbWrite);
		_hzbHistoryValid = true;
	}

	void GpuVisibilityCulling::ReportSubmission(uint32_t submittedDraws, uint32_t visibleInstances)
	{
		_stats.submittedDraws = submittedDraws;
		_stats.visibleInstances = visibleInstances;
	}

	void GpuVisibilityCulling::DebugDraw(const RenderBatchSnapshot& snapshot)
	{
		if (!r_gpuCullDebugBounds._val.b &&
			!r_gpuCullDebugFrustumRejected._val.b &&
			!r_gpuCullDebugOcclusionRejected._val.b)
		{
			return;
		}

		for (const auto& batch : snapshot)
		{
			for (const auto& renderable : batch.second)
			{
				if (!renderable.entity)
					continue;

				if (r_gpuCullDebugBounds._val.b && renderable.cullEligible)
				{
					g_pEnv->_debugRenderer->DrawAABB(renderable.entity->GetWorldAABB(), math::Color(HEX_RGBA_TO_FLOAT4(90, 120, 255, 110)));
				}

				if (r_gpuCullDebugFrustumRejected._val.b && renderable.culledByFrustum)
				{
					g_pEnv->_debugRenderer->DrawAABB(renderable.entity->GetWorldAABB(), math::Color(HEX_RGBA_TO_FLOAT4(255, 70, 70, 170)));
				}

				if (r_gpuCullDebugOcclusionRejected._val.b && renderable.culledByOcclusion)
				{
					g_pEnv->_debugRenderer->DrawAABB(renderable.entity->GetWorldAABB(), math::Color(HEX_RGBA_TO_FLOAT4(255, 188, 32, 170)));
				}
			}
		}
	}
}
