#include "ClusteredLighting.hpp"
#include "../HexEngine.hpp"
#include "IGraphicsDevice.hpp"
#include "ITexture2D.hpp"
#include "../Scene/Scene.hpp"
#include "../Entity/Entity.hpp"
#include "../Entity/Component/Camera.hpp"
#include "../Entity/Component/PointLight.hpp"
#include "../Entity/Component/SpotLight.hpp"

#include <d3d11.h>

namespace HexEngine
{
	namespace
	{
		constexpr uint32_t kDebugWidth = 480;
		constexpr uint32_t kDebugHeight = 270;

		template <typename T>
		void SafeRelease(T*& ptr)
		{
			if (ptr != nullptr)
			{
				ptr->Release();
				ptr = nullptr;
			}
		}
	}

	bool ClusteredLighting::Create()
	{
		_cullShader = IShader::Create("EngineData.Shaders/ClusterLightCull.hcs");
		_debugShader = IShader::Create("EngineData.Shaders/ClusterLightDebug.hcs");
		if (_cullShader == nullptr || _debugShader == nullptr)
		{
			LOG_WARN("ClusteredLighting: cull/debug shaders missing - clustering disabled");
			return false;
		}

		ID3D11Device* device = reinterpret_cast<ID3D11Device*>(g_pEnv->_graphicsDevice->GetNativeDevice());
		if (device == nullptr)
			return false;

		// Lights: CPU-written each frame, GPU-read.
		{
			D3D11_BUFFER_DESC desc = {};
			desc.ByteWidth = sizeof(GpuLight) * kMaxLights;
			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			desc.StructureByteStride = sizeof(GpuLight);
			if (FAILED(device->CreateBuffer(&desc, nullptr, &_lightsBuffer)))
				return false;

			D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};
			srv.Format = DXGI_FORMAT_UNKNOWN;
			srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
			srv.Buffer.NumElements = kMaxLights;
			if (FAILED(device->CreateShaderResourceView(_lightsBuffer, &srv, &_lightsSrv)))
				return false;
		}

		// Cluster counts + index lists: GPU-written, GPU-read.
		const auto makeUavBuffer = [&](uint32_t elements, ID3D11Buffer** buffer,
			ID3D11UnorderedAccessView** uav, ID3D11ShaderResourceView** srvOut) -> bool
		{
			D3D11_BUFFER_DESC desc = {};
			desc.ByteWidth = sizeof(uint32_t) * elements;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			desc.StructureByteStride = sizeof(uint32_t);
			if (FAILED(device->CreateBuffer(&desc, nullptr, buffer)))
				return false;

			D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
			ud.Format = DXGI_FORMAT_UNKNOWN;
			ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			ud.Buffer.NumElements = elements;
			if (FAILED(device->CreateUnorderedAccessView(*buffer, &ud, uav)))
				return false;

			if (srvOut != nullptr)
			{
				D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
				sd.Format = DXGI_FORMAT_UNKNOWN;
				sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
				sd.Buffer.NumElements = elements;
				if (FAILED(device->CreateShaderResourceView(*buffer, &sd, srvOut)))
					return false;
			}
			return true;
		};

		if (!makeUavBuffer(kClusterCount, &_countsBuffer, &_countsUav, &_countsSrv))
			return false;
		if (!makeUavBuffer(kClusterCount * kMaxLightsPerCluster, &_listsBuffer, &_listsUav, nullptr))
			return false;

		// Constants (b5, matching the compute shaders).
		{
			D3D11_BUFFER_DESC desc = {};
			desc.ByteWidth = sizeof(ClusterConstants);
			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			if (FAILED(device->CreateBuffer(&desc, nullptr, &_constantsBuffer)))
				return false;
		}

		// Debug heatmap target. Engine texture so the overlay path can draw it;
		// the UAV is created raw here because that is the one accessor the
		// engine texture API doesn't expose.
		_debugTexture = g_pEnv->_graphicsDevice->CreateTexture2D(
			kDebugWidth, kDebugHeight,
			DXGI_FORMAT_R8G8B8A8_UNORM,
			1,
			D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
			1);
		if (_debugTexture != nullptr)
		{
			ID3D11Texture2D* native = reinterpret_cast<ID3D11Texture2D*>(_debugTexture->GetNativePtr());
			if (native == nullptr || FAILED(device->CreateUnorderedAccessView(native, nullptr, &_debugUav)))
				LOG_WARN("ClusteredLighting: debug UAV creation failed - heatmap disabled");
		}

		LOG_INFO("ClusteredLighting: grid %ux%ux%u (%u clusters), %u lights max, %u per cluster",
			kClustersX, kClustersY, kClustersZ, kClusterCount, kMaxLights, kMaxLightsPerCluster);
		return true;
	}

	void ClusteredLighting::Destroy()
	{
		SafeRelease(_normalDepthSrv);
		SafeRelease(_debugUav);
		SAFE_DELETE(_debugTexture);
		SafeRelease(_constantsBuffer);
		SafeRelease(_listsUav);
		SafeRelease(_listsBuffer);
		SafeRelease(_countsSrv);
		SafeRelease(_countsUav);
		SafeRelease(_countsBuffer);
		SafeRelease(_lightsSrv);
		SafeRelease(_lightsBuffer);
		_cullShader.reset();
		_debugShader.reset();
	}

	void ClusteredLighting::UpdateAndCull(Scene* scene, Camera* camera)
	{
		if (scene == nullptr || camera == nullptr || _cullShader == nullptr || _lightsBuffer == nullptr)
			return;

		ID3D11DeviceContext* context = reinterpret_cast<ID3D11DeviceContext*>(g_pEnv->_graphicsDevice->GetNativeDeviceContext());
		if (context == nullptr)
			return;

		// ---- Gather every light, no closest-N cap -------------------------
		std::vector<GpuLight> lights;
		lights.reserve(256);

		std::vector<PointLight*> points;
		if (scene->GetComponents<PointLight>(points))
		{
			for (auto* l : points)
			{
				if (l == nullptr || l->GetEntity() == nullptr || l->GetEntity()->IsPendingDeletion())
					continue;
				const auto diffuse = l->GetDiffuseColour();
				const float strength = std::max(0.0f, l->GetLightStrength() * l->GetLightMultiplier());
				if (diffuse.w <= 0.0f || strength <= 0.0f)
					continue;
				if (lights.size() >= kMaxLights)
					break;

				GpuLight gl = {};
				const auto pos = l->GetEntity()->GetWorldTM().Translation();
				gl.posRadius = math::Vector4(pos.x, pos.y, pos.z, std::max(0.05f, l->GetRadius()));
				gl.colorStrength = math::Vector4(diffuse.x, diffuse.y, diffuse.z, strength);
				gl.params = math::Vector4(0.0f, 0.0f, 0.0f, 0.0f);
				lights.push_back(gl);
			}
		}

		std::vector<SpotLight*> spots;
		if (scene->GetComponents<SpotLight>(spots))
		{
			for (auto* l : spots)
			{
				if (l == nullptr || l->GetEntity() == nullptr || l->GetEntity()->IsPendingDeletion())
					continue;
				const auto diffuse = l->GetDiffuseColour();
				const float strength = std::max(0.0f, l->GetLightStrength() * l->GetLightMultiplier());
				if (diffuse.w <= 0.0f || strength <= 0.0f)
					continue;
				if (lights.size() >= kMaxLights)
					break;

				GpuLight gl = {};
				auto* ent = l->GetEntity();
				const auto pos = ent->GetWorldTM().Translation();
				const auto fwd = ent->GetWorldTM().Forward();
				gl.posRadius = math::Vector4(pos.x, pos.y, pos.z, std::max(0.05f, l->GetRadius()));
				gl.colorStrength = math::Vector4(diffuse.x, diffuse.y, diffuse.z, strength);
				gl.dirCone = math::Vector4(fwd.x, fwd.y, fwd.z, cosf(ToRadian(l->GetOuterConeAngle() * 0.5f)));
				gl.params = math::Vector4(cosf(ToRadian(l->GetInnerConeAngle() * 0.5f)), 1.0f, 0.0f, 0.0f);
				lights.push_back(gl);
			}
		}

		_lastLightCount = (uint32_t)lights.size();

		// ---- Upload -------------------------------------------------------
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (SUCCEEDED(context->Map(_lightsBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		{
			if (!lights.empty())
				memcpy(mapped.pData, lights.data(), sizeof(GpuLight) * lights.size());
			context->Unmap(_lightsBuffer, 0);
		}

		ClusterConstants constants = {};
		// Transposed like every engine cbuffer matrix - the shaders do
		// mul(vector, matrix) against row-major uploads.
		constants.view = camera->GetViewMatrix().Transpose();
		const auto& vp = camera->GetViewport();
		const float aspect = vp.height > 0.0f ? vp.width / vp.height : 1.0f;
		const float tanHalfFovY = tanf(ToRadian(camera->GetFov()) * 0.5f);
		constants.screenParams = math::Vector4(tanHalfFovY * aspect, tanHalfFovY, (float)lights.size(), 0.0f);

		if (SUCCEEDED(context->Map(_constantsBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		{
			memcpy(mapped.pData, &constants, sizeof(constants));
			context->Unmap(_constantsBuffer, 0);
		}

		// ---- Dispatch -----------------------------------------------------
		auto* stage = _cullShader->GetShaderStage(ShaderStage::ComputeShader);
		if (stage == nullptr)
			return;

		context->CSSetShader(reinterpret_cast<ID3D11ComputeShader*>(stage->GetNativePtr()), nullptr, 0);
		context->CSSetConstantBuffers(5, 1, &_constantsBuffer);
		context->CSSetShaderResources(0, 1, &_lightsSrv);
		ID3D11UnorderedAccessView* uavs[2] = { _countsUav, _listsUav };
		context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);

		context->Dispatch((kClusterCount + 63) / 64, 1, 1);

		// Unbind so the lists are readable as SRVs downstream.
		ID3D11UnorderedAccessView* nullUavs[2] = { nullptr, nullptr };
		context->CSSetUnorderedAccessViews(0, 2, nullUavs, nullptr);
		ID3D11ShaderResourceView* nullSrv = nullptr;
		context->CSSetShaderResources(0, 1, &nullSrv);
		context->CSSetShader(nullptr, nullptr, 0);
	}

	void ClusteredLighting::RenderDebug(ITexture2D* gbufferNormalDepth)
	{
		if (gbufferNormalDepth == nullptr || _debugShader == nullptr || _debugUav == nullptr)
			return;

		ID3D11Device* device = reinterpret_cast<ID3D11Device*>(g_pEnv->_graphicsDevice->GetNativeDevice());
		ID3D11DeviceContext* context = reinterpret_cast<ID3D11DeviceContext*>(g_pEnv->_graphicsDevice->GetNativeDeviceContext());
		if (device == nullptr || context == nullptr)
			return;

		// (Re)create the normal-RT SRV when the underlying texture changes
		// (resize recreates the gbuffer).
		void* native = gbufferNormalDepth->GetNativePtr();
		if (native != _normalDepthSrvSource)
		{
			SafeRelease(_normalDepthSrv);
			if (FAILED(device->CreateShaderResourceView(
					reinterpret_cast<ID3D11Texture2D*>(native), nullptr, &_normalDepthSrv)))
				return;
			_normalDepthSrvSource = native;
		}

		auto* stage = _debugShader->GetShaderStage(ShaderStage::ComputeShader);
		if (stage == nullptr)
			return;

		context->CSSetShader(reinterpret_cast<ID3D11ComputeShader*>(stage->GetNativePtr()), nullptr, 0);
		context->CSSetConstantBuffers(5, 1, &_constantsBuffer);
		ID3D11ShaderResourceView* srvs[2] = { _normalDepthSrv, _countsSrv };
		context->CSSetShaderResources(0, 2, srvs);
		context->CSSetUnorderedAccessViews(0, 1, &_debugUav, nullptr);

		context->Dispatch((kDebugWidth + 7) / 8, (kDebugHeight + 7) / 8, 1);

		ID3D11UnorderedAccessView* nullUav = nullptr;
		context->CSSetUnorderedAccessViews(0, 1, &nullUav, nullptr);
		ID3D11ShaderResourceView* nullSrvs[2] = { nullptr, nullptr };
		context->CSSetShaderResources(0, 2, nullSrvs);
		context->CSSetShader(nullptr, nullptr, 0);
	}
}
