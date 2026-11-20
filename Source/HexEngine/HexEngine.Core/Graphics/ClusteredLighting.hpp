#pragma once

#include "../Required.hpp"
#include "IShader.hpp"

struct ID3D11Buffer;
struct ID3D11ShaderResourceView;
struct ID3D11UnorderedAccessView;
struct ID3D11Texture2D;
struct ID3D11SamplerState;
struct ID3D11DeviceContext;

namespace HexEngine
{
	class Scene;
	class Camera;
	class ITexture2D;

	// Phase 2, first slice: clustered light culling.
	//
	// Gathers EVERY point and spot light in the scene (not the closest-16) into
	// a GPU buffer and bins them into a 16 x 9 x 32 view-frustum cluster grid in
	// compute. The depth slicing reuses the froxel volumetric system's
	// exponential mapping so a later step can share assignment between surface
	// lighting and fog. Nothing consumes the lists yet - the deliverable of
	// this slice is the lists themselves plus the occupancy heatmap
	// (r_clusterDebug) that proves the binning against the world.
	//
	// Raw D3D11 by deliberate convention: every compute producer in this
	// codebase (DiffuseGI, VolumetricScattering, GpuVisibilityCulling) drops to
	// the native context, and the D3D12 story for all of them is the same
	// Phase B5 port.
	class HEX_API ClusteredLighting
	{
	public:
		static constexpr uint32_t kClustersX = 16;
		static constexpr uint32_t kClustersY = 9;
		static constexpr uint32_t kClustersZ = 32;
		static constexpr uint32_t kClusterCount = kClustersX * kClustersY * kClustersZ;
		static constexpr uint32_t kMaxLightsPerCluster = 64;
		static constexpr uint32_t kMaxLights = 1024;

		// Textured rect lights: each distinct image is resampled into one slice of
		// a shared RGBA16F array with a full mip chain (PBRutils picks the mip from
		// the reflection lobe / diffuse footprint). Keep kAreaTextureSize in sync
		// with AREA_LIGHT_TEXTURE_MAX_MIP in PBRutils.shader (log2 of it).
		static constexpr uint32_t kAreaTextureSize = 512;
		static constexpr uint32_t kMaxAreaTextures = 16;
		// Shader slots of the array (+ its linear-clamp sampler) for the deferred
		// apply and forward transparents; the froxel fog reads it at CS t22.
		static constexpr uint32_t kAreaTextureSlot = 41;
		static constexpr uint32_t kAreaSamplerSlot = 5;

		bool Create();
		void Destroy();

		// Gather lights, upload, dispatch the cull. Main camera only.
		// shadowCasters marks lights that keep the per-light shadowed path;
		// they stay IN the lists (future consumers want them) but carry a flag
		// the apply pass skips on.
		//
		// Slice 7: when a ShadowAtlas is supplied, a shadowed SPOT whose face
		// has valid atlas content packs its tile index into params.w and the
		// apply shades it from the atlas instead of skipping it. Shadowed
		// point lights stay on the per-light path in v1 (six-face cube
		// sampling is a follow-up; the neon-street acceptance scene is spots).
		void UpdateAndCull(Scene* scene, Camera* camera, const std::vector<class Light*>& shadowCasters,
			const class ShadowAtlas* atlas = nullptr);

		// Bind/unbind the lists + constants for the fullscreen apply pass
		// (raw PS slots t21..t23 and b5 - the engine API has no PS
		// structured-buffer bind).
		void BindApply();
		void UnbindApply();

		// Render the occupancy heatmap into the debug texture. Needs the
		// gbuffer normal RT (its .w carries view depth).
		void RenderDebug(ITexture2D* gbufferNormalDepth);

		// Raw SRVs for compute consumers (the froxel volume). Valid after
		// Create(); null until then, which consumers treat as "off".
		ID3D11ShaderResourceView* GetLightsSrv() const { return _lightsSrv; }
		ID3D11ShaderResourceView* GetCountsSrv() const { return _countsSrv; }
		ID3D11ShaderResourceView* GetListsSrv() const { return _listsSrv; }
		// Per-atlas-tile view-proj matrices (row per tile), uploaded when
		// UpdateAndCull ran with an atlas. Null-safe for consumers.
		ID3D11ShaderResourceView* GetTileVpSrv() const { return _tileVpSrv; }
		// Area-light image array; null until a textured rect light appears.
		ID3D11ShaderResourceView* GetAreaTextureSrv() const { return _areaTexSrv; }
		ID3D11SamplerState* GetAreaTextureSampler() const { return _areaSampler; }
		// Raw PS bind of the array + sampler at kAreaTextureSlot / kAreaSamplerSlot.
		void BindAreaTexturesPS();
		void UnbindAreaTexturesPS();

		ITexture2D* GetDebugTexture() const { return _debugTexture; }
		uint32_t GetLastLightCount() const { return _lastLightCount; }

	private:
		// Slice for a rect light's image (resampling it when new, changed or live),
		// as 1-based index for the GpuLight packing; 0 = untextured.
		uint32_t AcquireAreaTexture(ID3D11DeviceContext* context, const std::shared_ptr<ITexture2D>& texture,
			bool srgb, bool live);
		bool EnsureAreaTextureArray();
		void ReleaseAreaTextures();

		// Mirrored by every HLSL consumer (ClusterLightCull/Apply, DefaultPixel +
		// DefaultAnimated ClFwdLight, VolumetricScatterDensity ClGpuLight) - the
		// structured-buffer stride must match all of them.
		struct GpuLight
		{
			math::Vector4 posRadius;
			math::Vector4 colorStrength;
			math::Vector4 dirCone;
			math::Vector4 params;	// x cos(inner) | area range, y type (0 point, 1 spot, 2 tube, 3 rect), z shadowed, w atlas tile
			math::Vector4 shape;	// area lights: xyz rect up * halfHeight, w two-sided
		};
		static_assert(sizeof(GpuLight) == 80, "update the HLSL GpuLight mirrors");

		struct ClusterConstants
		{
			math::Matrix view;
			math::Vector4 screenParams; // x tanHalfFovX, y tanHalfFovY, z lightCount, w unused
		};

		std::shared_ptr<IShader> _cullShader;
		std::shared_ptr<IShader> _debugShader;

		ID3D11Buffer* _lightsBuffer = nullptr;
		ID3D11ShaderResourceView* _lightsSrv = nullptr;
		ID3D11Buffer* _countsBuffer = nullptr;
		ID3D11UnorderedAccessView* _countsUav = nullptr;
		ID3D11ShaderResourceView* _countsSrv = nullptr;
		ID3D11Buffer* _listsBuffer = nullptr;
		ID3D11UnorderedAccessView* _listsUav = nullptr;
		ID3D11ShaderResourceView* _listsSrv = nullptr;
		ID3D11Buffer* _constantsBuffer = nullptr;
		ID3D11Buffer* _tileVpBuffer = nullptr;
		ID3D11ShaderResourceView* _tileVpSrv = nullptr;

		ITexture2D* _debugTexture = nullptr;
		ID3D11UnorderedAccessView* _debugUav = nullptr;

		// SRV over the gbuffer normal RT, cached against the texture it was
		// created for (the RT is recreated on resize).
		ID3D11ShaderResourceView* _normalDepthSrv = nullptr;
		void* _normalDepthSrvSource = nullptr;

		uint32_t _lastLightCount = 0;

		// Area-light image array (see kAreaTextureSize).
		struct AreaTextureSlot
		{
			std::shared_ptr<ITexture2D> texture;	// held so a freed + reallocated texture can't alias the slot
			bool srgb = true;
			bool usedThisFrame = false;
			bool resampledThisFrame = false;
		};
		AreaTextureSlot _areaSlots[kMaxAreaTextures];
		bool _areaMipsDirty = false;
		bool _areaTexFailed = false;
		std::shared_ptr<IShader> _areaBlitShader;
		ID3D11Texture2D* _areaTex = nullptr;
		ID3D11ShaderResourceView* _areaTexSrv = nullptr;
		ID3D11UnorderedAccessView* _areaTexUav = nullptr;
		ID3D11SamplerState* _areaSampler = nullptr;
		ID3D11Buffer* _areaBlitConstants = nullptr;
	};
}
