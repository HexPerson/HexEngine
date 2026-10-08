#include "ClusteredLighting.hpp"
#include "../HexEngine.hpp"
#include "IGraphicsDevice.hpp"
#include "ITexture2D.hpp"
#include "../Scene/Scene.hpp"
#include "../Entity/Entity.hpp"
#include "../Entity/Component/Camera.hpp"
#include "../Entity/Component/PointLight.hpp"
#include "../Entity/Component/SpotLight.hpp"
#include "../Entity/Component/AreaLight.hpp"
#include "../Entity/Component/Light.hpp"
#include "ShadowAtlas.hpp"

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
		if (!makeUavBuffer(kClusterCount * kMaxLightsPerCluster, &_listsBuffer, &_listsUav, &_listsSrv))
			return false;

		// Per-atlas-tile view-proj matrices (slice 7). CPU-written when
		// UpdateAndCull runs with an atlas; sized to the atlas tile count.
		{
			constexpr uint32_t kTileVpElements = 16; // ShadowAtlas::kTileCount
			D3D11_BUFFER_DESC desc = {};
			desc.ByteWidth = sizeof(math::Matrix) * kTileVpElements;
			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			desc.StructureByteStride = sizeof(math::Matrix);
			if (FAILED(device->CreateBuffer(&desc, nullptr, &_tileVpBuffer)))
				return false;

			D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};
			srv.Format = DXGI_FORMAT_UNKNOWN;
			srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
			srv.Buffer.NumElements = kTileVpElements;
			if (FAILED(device->CreateShaderResourceView(_tileVpBuffer, &srv, &_tileVpSrv)))
				return false;
		}

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
		SafeRelease(_tileVpSrv);
		SafeRelease(_tileVpBuffer);
		SafeRelease(_listsSrv);
		SafeRelease(_listsUav);
		SafeRelease(_listsBuffer);
		SafeRelease(_countsSrv);
		SafeRelease(_countsUav);
		SafeRelease(_countsBuffer);
		SafeRelease(_lightsSrv);
		SafeRelease(_lightsBuffer);
		_cullShader.reset();
		_debugShader.reset();
		ReleaseAreaTextures();
	}

	void ClusteredLighting::ReleaseAreaTextures()
	{
		for (auto& slot : _areaSlots)
			slot = AreaTextureSlot{};
		SafeRelease(_areaTexUav);
		SafeRelease(_areaTexSrv);
		SafeRelease(_areaTex);
		SafeRelease(_areaSampler);
		SafeRelease(_areaBlitConstants);
		_areaBlitShader.reset();
	}

	bool ClusteredLighting::EnsureAreaTextureArray()
	{
		if (_areaTex != nullptr)
			return true;
		if (_areaTexFailed)
			return false;
		_areaTexFailed = true;	// cleared on success; don't retry a failure every frame

		ID3D11Device* device = reinterpret_cast<ID3D11Device*>(g_pEnv->_graphicsDevice->GetNativeDevice());
		if (device == nullptr)
			return false;

		_areaBlitShader = IShader::Create("EngineData.Shaders/AreaLightTextureBlit.hcs");
		if (_areaBlitShader == nullptr || _areaBlitShader->GetShaderStage(ShaderStage::ComputeShader) == nullptr)
		{
			LOG_WARN("ClusteredLighting: AreaLightTextureBlit shader missing - rect light images disabled");
			return false;
		}

		// Lazily created: ~44 MB at 16 x 512^2 RGBA16F with mips, only once a
		// textured rect light exists. RT bind for GenerateMips, UAV for the blit.
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = kAreaTextureSize;
		desc.Height = kAreaTextureSize;
		desc.MipLevels = 0;
		desc.ArraySize = kMaxAreaTextures;
		desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS;
		desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
		if (FAILED(device->CreateTexture2D(&desc, nullptr, &_areaTex)))
			return false;

		D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};
		srv.Format = desc.Format;
		srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
		srv.Texture2DArray.MipLevels = (UINT)-1;
		srv.Texture2DArray.ArraySize = kMaxAreaTextures;
		D3D11_UNORDERED_ACCESS_VIEW_DESC uav = {};
		uav.Format = desc.Format;
		uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
		uav.Texture2DArray.ArraySize = kMaxAreaTextures;
		if (FAILED(device->CreateShaderResourceView(_areaTex, &srv, &_areaTexSrv)) ||
			FAILED(device->CreateUnorderedAccessView(_areaTex, &uav, &_areaTexUav)))
		{
			ReleaseAreaTextures();
			return false;
		}

		D3D11_SAMPLER_DESC sampler = {};
		sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
		sampler.MaxLOD = D3D11_FLOAT32_MAX;
		D3D11_BUFFER_DESC cb = {};
		cb.ByteWidth = 32;
		cb.Usage = D3D11_USAGE_DYNAMIC;
		cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (FAILED(device->CreateSamplerState(&sampler, &_areaSampler)) ||
			FAILED(device->CreateBuffer(&cb, nullptr, &_areaBlitConstants)))
		{
			ReleaseAreaTextures();
			return false;
		}

		_areaTexFailed = false;
		return true;
	}

	uint32_t ClusteredLighting::AcquireAreaTexture(ID3D11DeviceContext* context, const std::shared_ptr<ITexture2D>& texture,
		bool srgb, bool live)
	{
		if (texture == nullptr || !EnsureAreaTextureArray())
			return 0;

		// Reuse the slice already holding this image; else the first slice not
		// claimed this frame (stale entries are overwritten).
		int32_t index = -1;
		for (uint32_t i = 0; i < kMaxAreaTextures && index < 0; ++i)
			if (_areaSlots[i].texture == texture && _areaSlots[i].srgb == srgb)
				index = (int32_t)i;
		bool resample = index < 0;
		if (index < 0)
		{
			// Empty slices first, so an image whose light just hasn't been
			// gathered yet this frame isn't evicted (and re-sampled) needlessly.
			for (uint32_t i = 0; i < kMaxAreaTextures && index < 0; ++i)
				if (!_areaSlots[i].usedThisFrame && _areaSlots[i].texture == nullptr)
					index = (int32_t)i;
			for (uint32_t i = 0; i < kMaxAreaTextures && index < 0; ++i)
				if (!_areaSlots[i].usedThisFrame)
					index = (int32_t)i;
			if (index < 0)
			{
				static bool s_warned = false;
				if (!s_warned)
				{
					s_warned = true;
					LOG_WARN("ClusteredLighting: more than %u distinct rect light images in view - extras render untextured", kMaxAreaTextures);
				}
				return 0;
			}
		}

		AreaTextureSlot& slot = _areaSlots[index];
		slot.usedThisFrame = true;
		if (live && !slot.resampledThisFrame)
			resample = true;

		if (resample)
		{
			auto* srcSrv = reinterpret_cast<ID3D11ShaderResourceView*>(texture->GetNativeShaderView());
			auto* stage = _areaBlitShader->GetShaderStage(ShaderStage::ComputeShader);
			if (srcSrv == nullptr || stage == nullptr)
				return 0;

			// Read the source mip nearest 4x the slice resolution and box 4x4 taps
			// per texel (fewer when the source is already small).
			const float srcSize = (float)std::max(1, std::max(texture->GetWidth(), texture->GetHeight()));
			const float ratio = srcSize / (float)kAreaTextureSize;
			D3D11_SHADER_RESOURCE_VIEW_DESC srcDesc = {};
			srcSrv->GetDesc(&srcDesc);
			const float maxSrcMip = srcDesc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D && srcDesc.Texture2D.MipLevels > 0
				? (float)(srcDesc.Texture2D.MipLevels - 1) : 0.0f;
			const uint32_t taps = ratio > 3.0f ? 4u : (ratio > 1.5f ? 2u : 1u);
			const float srcMip = std::clamp(std::log2(std::max(ratio / (float)taps, 1.0f)), 0.0f, maxSrcMip);
			// An _SRGB view already decodes in the sampler.
			const bool viewIsSrgb = srcDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || srcDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
				srcDesc.Format == DXGI_FORMAT_BC1_UNORM_SRGB || srcDesc.Format == DXGI_FORMAT_BC2_UNORM_SRGB ||
				srcDesc.Format == DXGI_FORMAT_BC3_UNORM_SRGB || srcDesc.Format == DXGI_FORMAT_BC7_UNORM_SRGB;

			struct { uint32_t params[4]; float lod[4]; } constants = {
				{ (uint32_t)index, kAreaTextureSize, (srgb && !viewIsSrgb) ? 1u : 0u, taps }, { srcMip, 0.0f, 0.0f, 0.0f } };
			D3D11_MAPPED_SUBRESOURCE mapped = {};
			if (FAILED(context->Map(_areaBlitConstants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
				return 0;
			memcpy(mapped.pData, &constants, sizeof(constants));
			context->Unmap(_areaBlitConstants, 0);

			context->CSSetShader(reinterpret_cast<ID3D11ComputeShader*>(stage->GetNativePtr()), nullptr, 0);
			context->CSSetShaderResources(0, 1, &srcSrv);
			context->CSSetUnorderedAccessViews(0, 1, &_areaTexUav, nullptr);
			context->CSSetSamplers(0, 1, &_areaSampler);
			context->CSSetConstantBuffers(6, 1, &_areaBlitConstants);
			context->Dispatch((kAreaTextureSize + 7) / 8, (kAreaTextureSize + 7) / 8, 1);

			ID3D11ShaderResourceView* nullSrv = nullptr;
			ID3D11UnorderedAccessView* nullUav = nullptr;
			ID3D11Buffer* nullCb = nullptr;
			context->CSSetShaderResources(0, 1, &nullSrv);
			context->CSSetUnorderedAccessViews(0, 1, &nullUav, nullptr);
			context->CSSetConstantBuffers(6, 1, &nullCb);
			context->CSSetShader(nullptr, nullptr, 0);

			slot.texture = texture;
			slot.srgb = srgb;
			slot.resampledThisFrame = true;
			_areaMipsDirty = true;
		}

		return (uint32_t)index + 1;
	}

	void ClusteredLighting::BindAreaTexturesPS()
	{
		auto* context = reinterpret_cast<ID3D11DeviceContext*>(g_pEnv->_graphicsDevice->GetNativeDeviceContext());
		if (context == nullptr)
			return;
		context->PSSetShaderResources(kAreaTextureSlot, 1, &_areaTexSrv);
		if (_areaSampler != nullptr)
			context->PSSetSamplers(kAreaSamplerSlot, 1, &_areaSampler);
	}

	void ClusteredLighting::UnbindAreaTexturesPS()
	{
		auto* context = reinterpret_cast<ID3D11DeviceContext*>(g_pEnv->_graphicsDevice->GetNativeDeviceContext());
		if (context == nullptr)
			return;
		ID3D11ShaderResourceView* nullSrv = nullptr;
		context->PSSetShaderResources(kAreaTextureSlot, 1, &nullSrv);
	}

	// Units slice part 3 (SceneRenderer.cpp declares it): lumens -> candela
	// at gather time. The shading path's existing 1/d^2 attenuation makes
	// candela the correct gathered quantity.
	// Part 4 note: r_legacyLightScale (legacy strength -> lumens) and the
	// static pre-exposure (1/scale, physical -> rendered units) BOTH apply
	// here conceptually and cancel exactly, so the code below stays in its
	// part-3 form. If either factor ever stops being the other's inverse
	// (content migration, dynamic pre-exposure), both must appear explicitly
	// in every packed strength - here, SetupForwardLights, and the per-light
	// passes.
	extern HVar r_physicalLightUnits;

	void ClusteredLighting::UpdateAndCull(Scene* scene, Camera* camera, const std::vector<Light*>& shadowCasters,
		const ShadowAtlas* atlas)
	{
		const bool physicalUnits = r_physicalLightUnits._val.b;
		constexpr float kPi = 3.14159265358979f;
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
				float strength = std::max(0.0f, l->GetLightStrength() * l->GetLightMultiplier());
				if (diffuse.w <= 0.0f || strength <= 0.0f)
					continue;
				if (physicalUnits)
					strength /= 4.0f * kPi; // lumens -> candela, isotropic point
				if (lights.size() >= kMaxLights)
					break;

				GpuLight gl = {};
				const auto pos = l->GetEntity()->GetWorldTM().Translation();
				gl.posRadius = math::Vector4(pos.x, pos.y, pos.z, std::max(0.05f, l->GetRadius()));
				gl.colorStrength = math::Vector4(diffuse.x, diffuse.y, diffuse.z, strength);
				const bool shadowed = std::find(shadowCasters.begin(), shadowCasters.end(),
					static_cast<Light*>(l)) != shadowCasters.end();
				gl.params = math::Vector4(0.0f, 0.0f, shadowed ? 1.0f : 0.0f, 0.0f);
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
				float strength = std::max(0.0f, l->GetLightStrength() * l->GetLightMultiplier());
				if (diffuse.w <= 0.0f || strength <= 0.0f)
					continue;
				if (physicalUnits)
				{
					// Frostbite cone-coupled: narrowing the cone concentrates
					// the same lumens into a brighter pool.
					const float cosOuter = cosf(ToRadian(l->GetOuterConeAngle() * 0.5f));
					strength /= std::max(2.0f * kPi * (1.0f - cosOuter), 1e-4f);
				}
				if (lights.size() >= kMaxLights)
					break;

				GpuLight gl = {};
				auto* ent = l->GetEntity();
				const auto pos = ent->GetWorldTM().Translation();
				const auto fwd = ent->GetWorldTM().Forward();
				gl.posRadius = math::Vector4(pos.x, pos.y, pos.z, std::max(0.05f, l->GetRadius()));
				gl.colorStrength = math::Vector4(diffuse.x, diffuse.y, diffuse.z, strength);
				gl.dirCone = math::Vector4(fwd.x, fwd.y, fwd.z, cosf(ToRadian(l->GetOuterConeAngle() * 0.5f)));
				const bool shadowed = std::find(shadowCasters.begin(), shadowCasters.end(),
					static_cast<Light*>(l)) != shadowCasters.end();
				// Slice 7: params.w = atlas tile holding this spot's shadow map,
				// or -1. A shadowed spot WITH a tile shades on the clustered
				// path (the apply samples the atlas); without one it keeps the
				// per-light path via the params.z skip, exactly as before.
				float atlasTile = -1.0f;
				if (shadowed && atlas != nullptr)
					atlasTile = (float)atlas->FindContentTile(static_cast<Light*>(l), 0);
				gl.params = math::Vector4(cosf(ToRadian(l->GetInnerConeAngle() * 0.5f)), 1.0f, shadowed ? 1.0f : 0.0f, atlasTile);
				lights.push_back(gl);
			}
		}

		// Area lights (tube / rect). Always unshadowed. posRadius.w is the
		// BOUNDING radius (range + shape extent) so the cull's sphere test needs
		// no special case; the authored range, measured from the shape, rides in
		// params.x. Packing documented in PBRutils.shader.
		for (auto& slot : _areaSlots)
		{
			slot.usedThisFrame = false;
			slot.resampledThisFrame = false;
		}

		std::vector<AreaLight*> areas;
		if (scene->GetComponents<AreaLight>(areas))
		{
			for (auto* l : areas)
			{
				if (l == nullptr || l->GetEntity() == nullptr || l->GetEntity()->IsPendingDeletion())
					continue;
				const auto diffuse = l->GetDiffuseColour();
				float strength = std::max(0.0f, l->GetLightStrength() * l->GetLightMultiplier());
				if (diffuse.w <= 0.0f || strength <= 0.0f)
					continue;
				const bool isRect = l->GetShape() == AreaLight::Shape::Rect;
				if (physicalUnits)
				{
					// lumens -> intensity. Tube: isotropic like a point (4pi).
					// Rect: cosine emitter, normal intensity = flux / pi over one
					// hemisphere, half that per side when two-sided.
					strength /= isRect ? (l->GetTwoSided() ? 2.0f * kPi : kPi) : 4.0f * kPi;
				}
				if (lights.size() >= kMaxLights)
					break;

				math::Vector3 centre, halfW, halfH, facing;
				l->GetWorldAxes(centre, halfW, halfH, facing);

				GpuLight gl = {};
				gl.posRadius = math::Vector4(centre.x, centre.y, centre.z, l->GetBoundingRadius());
				gl.colorStrength = math::Vector4(diffuse.x, diffuse.y, diffuse.z, strength);
				// dirCone.w: tube radius, or for a rect the 1-based image slice (0 = none).
				const float rectImage = isRect
					? (float)AcquireAreaTexture(context, l->GetTexture(), l->GetTextureIsSRGB(), l->GetTextureIsLive())
					: 0.0f;
				gl.dirCone = math::Vector4(halfW.x, halfW.y, halfW.z, isRect ? rectImage : l->GetTubeRadius());
				gl.params = math::Vector4(std::max(0.05f, l->GetRadius()), isRect ? 3.0f : 2.0f, 0.0f, -1.0f);
				gl.shape = math::Vector4(halfH.x, halfH.y, halfH.z, l->GetTwoSided() ? 1.0f : 0.0f);
				lights.push_back(gl);
			}
		}

		// New / changed images: one mip rebuild covering every slice.
		if (_areaMipsDirty && _areaTexSrv != nullptr)
		{
			context->GenerateMips(_areaTexSrv);
			_areaMipsDirty = false;
		}
		// Drop references to images no light used this frame.
		for (auto& slot : _areaSlots)
			if (!slot.usedThisFrame)
				slot.texture.reset();

		_lastLightCount = (uint32_t)lights.size();

		// ---- Upload -------------------------------------------------------
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (SUCCEEDED(context->Map(_lightsBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		{
			if (!lights.empty())
				memcpy(mapped.pData, lights.data(), sizeof(GpuLight) * lights.size());
			context->Unmap(_lightsBuffer, 0);
		}

		// Tile view-proj matrices for the atlas sampling path. TRANSPOSED
		// like every engine cbuffer/structured matrix (shaders mul(vec, m)).
		if (atlas != nullptr && _tileVpBuffer != nullptr &&
			SUCCEEDED(context->Map(_tileVpBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		{
			auto* dst = reinterpret_cast<math::Matrix*>(mapped.pData);
			for (int32_t i = 0; i < ShadowAtlas::kTileCount && i < 16; ++i)
				dst[i] = atlas->GetTile(i).viewProj.Transpose();
			context->Unmap(_tileVpBuffer, 0);
		}

		ClusterConstants constants = {};
		// Transposed like every engine cbuffer matrix - the shaders do
		// mul(vector, matrix) against row-major uploads.
		constants.view = camera->GetViewMatrix().Transpose();
		const auto& vp = camera->GetViewport();
		const float aspect = vp.height > 0.0f ? vp.width / vp.height : 1.0f;
		const float tanHalfFovY = tanf(ToRadian(camera->GetFov()) * 0.5f);
		HVar* applyDebug = g_pEnv->_commandManager->FindHVar("r_clusterApplyDebug");
		constants.screenParams = math::Vector4(tanHalfFovY * aspect, tanHalfFovY, (float)lights.size(),
			(applyDebug != nullptr) ? (float)applyDebug->_val.i32 : 0.0f);

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

	void ClusteredLighting::BindApply()
	{
		ID3D11DeviceContext* context = reinterpret_cast<ID3D11DeviceContext*>(g_pEnv->_graphicsDevice->GetNativeDeviceContext());
		if (context == nullptr)
			return;
		ID3D11ShaderResourceView* srvs[3] = { _lightsSrv, _countsSrv, _listsSrv };
		context->PSSetShaderResources(21, 3, srvs);
		context->PSSetConstantBuffers(5, 1, &_constantsBuffer);
		BindAreaTexturesPS();	// rect light images (t41 / s5)
	}

	void ClusteredLighting::UnbindApply()
	{
		ID3D11DeviceContext* context = reinterpret_cast<ID3D11DeviceContext*>(g_pEnv->_graphicsDevice->GetNativeDeviceContext());
		if (context == nullptr)
			return;
		ID3D11ShaderResourceView* nullSrvs[3] = { nullptr, nullptr, nullptr };
		context->PSSetShaderResources(21, 3, nullSrvs);
		ID3D11Buffer* nullCb = nullptr;
		context->PSSetConstantBuffers(5, 1, &nullCb);
		UnbindAreaTexturesPS();
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
