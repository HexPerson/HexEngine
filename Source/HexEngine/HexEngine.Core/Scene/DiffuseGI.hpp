#pragma once

#include "../Required.hpp"
#include "../Graphics/GBuffer.hpp"
#include <unordered_map>

namespace HexEngine
{
	class Scene;
	class Camera;
	class IConstantBuffer;
	class ITexture2D;
	class ITexture3D;
	class StaticMeshComponent;
	class Material;
	class Mesh;
	class Entity;

	/**
	 * @brief Runtime-friendly diffuse global illumination system based on clipmapped probe volumes.
	 *
	 * This implementation targets D3D11 and keeps predictable frame cost by updating one clipmap
	 * level per frame under a configurable probe budget.
	 */
	class HEX_API DiffuseGI
	{
	public:
		DiffuseGI() = default;
		~DiffuseGI() = default;

		void Create(uint32_t width, uint32_t height);
		void Destroy();
		void Resize(uint32_t width, uint32_t height);

		/**
		 * @brief Updates clipmap/probe data for the current frame.
		 */
		void Update(Scene* scene, Camera* camera);

		/**
		 * @brief Renders GI (half-res trace + full-res resolve) and composites to beauty target.
		 */
		void Render(Scene* scene, Camera* camera, const GBuffer& gbuffer, ITexture2D* beautyTarget);
		ITexture2D* GetResolvedTexture() const { return _giResolved; }
		// Bilateral-blurred AO single-channel target. Null until the first
		// frame after Create() runs the blur pass, or if the engine was built
		// without the blur shader. DiffuseGIAOProvider reads .r from this in
		// preference to _giResolved.a when r_useGIAO compound mode is active.
		ITexture2D* GetBlurredAOTexture() const { return _giAoBlurred; }

		/**
		 * @brief Binds the 4 clipmaps' voxel radiance/opacity/albedo (12 SRVs) via the auto-slot
		 * SetTexture3D path and the GI constant buffer on PS b4. The caller is responsible for
		 * having advanced the auto SRV slot to the desired starting register before calling.
		 */
		void BindVoxelsForReflection() const;

		/**
		 * @brief Scatters the current frame's LIT scene radiance into per-clip
		 * atomic accumulation buffers (clips 0-1). Call after deferred lighting
		 * with the lit HDR scene and the gbuffer position/normal targets; the
		 * next voxelize update folds the accumulated radiance into injection,
		 * making GI track every light type, shadowing and its own bounce.
		 */
		void DispatchScreenFeedback(ITexture2D* litScene, ITexture2D* gbufferPosition, ITexture2D* gbufferNormal);

	private:
		static constexpr uint32_t ClipmapCount = 4;
		// Lit-scene feedback covers the two near clips; far clips get their
		// energy through the constant-driven injection + second bounce.
		static constexpr uint32_t FeedbackLevelCount = 2;
		static constexpr uint32_t ProbeGridX = 16;
		static constexpr uint32_t ProbeGridY = 10;
		static constexpr uint32_t ProbeGridZ = 16;

		struct ClipmapLevel
		{
			math::Vector3 center = math::Vector3::Zero;
			math::Vector3 previousCenter = math::Vector3::Zero;
			math::Vector3 targetCenter = math::Vector3::Zero;
			float extent = 64.0f;
			uint32_t resolution = 48;
			bool dirty = true;
			bool initialized = false;
			math::Vector3 pendingShiftWs = math::Vector3::Zero;

			ITexture3D* radianceVolume = nullptr;
			ITexture3D* radianceScratchVolume = nullptr;
			ITexture3D* albedoVolume = nullptr;
			ITexture3D* albedoScratchVolume = nullptr;
			ITexture3D* opacityVolume = nullptr;
			// Directional voxels (SH band 1): per-channel linear moments.
			// radianceVolume stays the SH L0 (ambient) + occlusion - every
			// legacy consumer (SSR fallback, reflections, resolve, AO) reads
			// it unchanged. l1Volume[axis] holds the per-channel moment along
			// X/Y/Z (RGBA16F, SIGNED). A receiver evaluates
			// E(N) = max(0, 0.5*L0 + 0.5*(L1x*Nx + L1y*Ny + L1z*Nz)), which
			// cancels radiance behind emitting surfaces - the fix for GI
			// wrapping around silhouettes (isotropic-voxel light leak).
			ITexture3D* l1Volume[3] = {};
			ITexture3D* l1ScratchVolume[3] = {};
			ITexture2D* probeIrradianceAtlas = nullptr;
			ITexture2D* probeVisibilityAtlas = nullptr;
			ID3D11UnorderedAccessView* radianceUav = nullptr;
			ID3D11UnorderedAccessView* radianceScratchUav = nullptr;
			ID3D11UnorderedAccessView* albedoUav = nullptr;
			ID3D11UnorderedAccessView* albedoScratchUav = nullptr;
			ID3D11ShaderResourceView* radianceSrv = nullptr;
			ID3D11ShaderResourceView* radianceScratchSrv = nullptr;
			ID3D11ShaderResourceView* albedoSrv = nullptr;
			ID3D11ShaderResourceView* albedoScratchSrv = nullptr;
			ID3D11UnorderedAccessView* l1Uav[3] = {};
			ID3D11UnorderedAccessView* l1ScratchUav[3] = {};
			ID3D11ShaderResourceView* l1Srv[3] = {};
			ID3D11ShaderResourceView* l1ScratchSrv[3] = {};

			std::vector<float> radianceCpu;
			std::vector<uint8_t> opacityCpu;
			std::vector<float> probeIrradianceCpu;
			std::vector<uint8_t> probeVisibilityCpu;
		};

		struct GIConstants
		{
			math::Vector4 clipCenterExtent[ClipmapCount];
			math::Vector4 clipPreviousCenterExtent[ClipmapCount];
			math::Vector4 clipVoxelInfo[ClipmapCount];
			math::Vector4 params0; // x=intensity, y=energyClamp, z=debugMode, w=activeClipmap
			math::Vector4 params1; // x=hysteresis, y=historyReject, z=halfResInvW, w=halfResInvH
			math::Vector4 params2; // x=screenBounce, y=probeBlend, z=voxelDecay, w=useVoxelAlphaOpacity
			math::Vector4 params3; // xyz=sunDirectionWS, w=sunDirectionality
			math::Vector4 params4; // x=jitterScale, y=clipBlendWidth, z=pixelMotionStart, w=pixelMotionStrength
			math::Vector4 params5; // x=luminanceRejectScale, y=ditherDarkAmp, z=ditherBrightAmp, w=movementPreset
			math::Vector4 params6; // x=voxelNeighbourBlend, y=shiftSettle, z=voxelAlbedoInfluence, w=reserved
			math::Vector4 params7; // x=gpuMaterialProxyBlend, y=gpuComputeBaseSunEnabled, z=sunShadowPerVoxel, w=cameraMotionBlend
			math::Vector4 params8; // x=diffuseInject, y=sunInject, z=sunDirectionalBoost, w=emissiveInject
			math::Vector4 params9; // x=sunStrength, y=unlitAlbedoInjection, z=maxVoxelTestsPerTri, w=sunShadowMode
			math::Vector4 params10; // x=gpuEdgeSmoothThreshold, y=gpuEdgeSmoothBlendStrength, z=bounceAlbedoMinLuma, w=bounceAlbedoRemapAmount
			math::Vector4 params11; // x=localLightInjection, y=clipAttenuation, z=receiverMinLuma, w=receiverRemapAmount
			math::Vector4 params12; // x=live source-triangle count this update, y=candidate routing active, z=snap boost, w reserved
			math::Vector4 params13; // x=litInjection strength, y=litInjection maxLuma, z=feedback accum bound for this level, w reserved
		};

		struct GpuVoxelTriangle
		{
			math::Vector4 p0;
			math::Vector4 p1;
			math::Vector4 p2;
			math::Vector4 radianceOpacity;
			math::Vector4 albedoWeight;
			math::Vector4 uv0uv1;
			math::Vector4 uv2Pad;
			math::Vector4 uvRect;
			math::Vector4 emissiveUvRect;
		};

		struct VoxelShiftConstants
		{
			int32_t offsetX = 0;
			int32_t offsetY = 0;
			int32_t offsetZ = 0;
			int32_t _padding = 0;
		};

		struct MeshTrackingState
		{
			math::Vector3 position = math::Vector3::Zero;
			math::Vector4 emissive = math::Vector4::Zero;
			math::Vector4 diffuse = math::Vector4::Zero;
			bool emissiveAffectsGI = true;
		};

		struct MaterialTriangleAlbedoCacheEntry
		{
			math::Vector3 diffuseTint = math::Vector3(0.75f, 0.75f, 0.75f);
			int32_t width = 0;
			int32_t height = 0;
			bool isBgra = false;
			bool hasTexture = false;
			const void* textureIdentity = nullptr;
			std::vector<uint8_t> pixels;
		};

		// Memoised per-mesh RAW texcoord bounds (pre-uvScale - they scale
		// linearly with a component's UV scale, so one entry serves every
		// component sharing the mesh). Mesh has no version counter, so
		// validity rides the vertex allocation identity plus a sparse
		// texcoord sentinel hash: an in-place rebuild that keeps the same
		// allocation and count still misses via the sentinel.
		struct MeshUvRectCacheEntry
		{
			const void* vertexData = nullptr;
			size_t vertexCount = 0u;
			uint64_t texcoordSentinelHash = 0ull;
			float minU = 0.0f;
			float minV = 0.0f;
			float maxU = 0.0f;
			float maxV = 0.0f;
			bool valid = false; // false = mesh had no finite texcoords
		};

		struct MeshEmissiveCacheEntry
		{
			const Material* material = nullptr;
			uint64_t transformVersion = 0ull;
			math::Vector2 uvScale = math::Vector2(1.0f, 1.0f);
			float score = 0.0f;
			math::Vector4 uvRect = math::Vector4(0.0f, 0.0f, 1.0f, 1.0f);
		};

		struct MaterialAlbedoCacheKey
		{
			const Material* material = nullptr;
			uint16_t uMin = 0u;
			uint16_t vMin = 0u;
			uint16_t uMax = 65535u;
			uint16_t vMax = 65535u;

			bool operator==(const MaterialAlbedoCacheKey& other) const
			{
				return material == other.material &&
					uMin == other.uMin &&
					vMin == other.vMin &&
					uMax == other.uMax &&
					vMax == other.vMax;
			}
		};

		struct MaterialAlbedoCacheKeyHash
		{
			size_t operator()(const MaterialAlbedoCacheKey& key) const
			{
				size_t hash = std::hash<const Material*>{}(key.material);
				const uint64_t packed =
					static_cast<uint64_t>(key.uMin) |
					(static_cast<uint64_t>(key.vMin) << 16u) |
					(static_cast<uint64_t>(key.uMax) << 32u) |
					(static_cast<uint64_t>(key.vMax) << 48u);
				hash ^= std::hash<uint64_t>{}(packed) + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
				return hash;
			}
		};

		struct GiClipmapParams
		{
			math::Vector3 center = math::Vector3::Zero;
			float extent = 1.0f;
			uint32_t resolution = 1u;
			uint32_t levelIndex = 0u;
			bool dirty = true;
		};

		struct GiMaterialProxy
		{
			const Material* material = nullptr;
			math::Vector4 diffuse = math::Vector4(1.0f, 1.0f, 1.0f, 1.0f);
			math::Vector4 emissive = math::Vector4::Zero;
			bool emissiveAffectsGI = true;
			uint32_t index = 0u;
		};

		struct GiLocalLightProxy
		{
			math::Vector3 position = math::Vector3::Zero;
			math::Vector3 direction = math::Vector3(0.0f, 0.0f, 1.0f);
			math::Vector3 colour = math::Vector3::Zero;
			float radius = 0.0f;
			float coneExponent = 1.0f;
			bool isSpot = false;
		};

		struct GiMeshInstanceProxy
		{
			StaticMeshComponent* component = nullptr;
			Entity* entity = nullptr;
			Mesh* mesh = nullptr;
			const Material* material = nullptr;
			uint32_t materialProxyIndex = 0u;
			math::Matrix worldTransform = math::Matrix::Identity;
			math::Vector2 uvScale = math::Vector2(1.0f, 1.0f);
			math::Vector3 aabbMin = math::Vector3::Zero;
			math::Vector3 aabbMax = math::Vector3::Zero;
		};

		struct GiRuntimeStats
		{
			float cpuTriangleBuildMs = 0.0f;
			float cpuUploadMs = 0.0f;
			float gpuDispatchMs = 0.0f;
			float candidateBuildMs = 0.0f;
			uint64_t uploadBytes = 0ull;
			uint32_t sourceTriangleCount = 0u;
			uint32_t candidateTriangleCount = 0u;
			uint32_t gpuLightCount = 0u;
			uint32_t emissiveMaterialCount = 0u;
			uint32_t emissiveTriangleCount = 0u;
			uint32_t emissiveActiveTriangleCount = 0u;
			uint32_t emissiveTiledTriangleCount = 0u;
			uint32_t emissivePayloadTriangleCount = 0u;
			float emissiveProxyMaxLuma = 0.0f;
			float emissiveProxyMaxStrength = 0.0f;
			float emissivePayloadMaxHint = 0.0f;
			uint32_t updatedClipMask = 0u;
		};

		struct GpuGiLight
		{
			math::Vector4 positionRadius = math::Vector4::Zero;
			math::Vector4 directionCone = math::Vector4(0.0f, 0.0f, 1.0f, 1.0f);
			math::Vector4 colourType = math::Vector4::Zero;
		};

		struct GpuGiMaterial
		{
			math::Vector4 diffuse = math::Vector4(1.0f, 1.0f, 1.0f, 1.0f);
			math::Vector4 emissive = math::Vector4::Zero;
			uint32_t texelOffset = 0u;
			uint32_t textureWidth = 0u;
			uint32_t textureHeight = 0u;
			uint32_t flags = 0u; // bit0=hasTexture, bit1=isBgra
			uint32_t emissiveTexelOffset = 0u;
			uint32_t emissiveTextureWidth = 0u;
			uint32_t emissiveTextureHeight = 0u;
			uint32_t emissiveFlags = 0u; // bit0=hasTexture, bit1=isBgra
		};

		struct GpuGiMaterialTexelBinding
		{
			uint32_t texelOffset = 0u;
			uint32_t textureWidth = 0u;
			uint32_t textureHeight = 0u;
			uint32_t flags = 0u;
			uint32_t contentHash = 0u;
		};

	private:
		bool CreateClipmapResources();
		void DestroyClipmapResources();
		void RebuildClipmapTransforms(const math::Vector3& cameraPosition, bool movementActive);
		void UpdateClipmapData(Scene* scene, uint32_t levelIndex);
		void UpdateProbeAtlases(ClipmapLevel& level);
		void UpdateConstants(Scene* scene);
		void AddDirtyRegion(uint32_t levelIndex, const dx::BoundingBox& bounds);
		bool IsMeshStateDirty(StaticMeshComponent* smc, const math::Vector3& worldPos);
		math::Vector3 GetMaterialAlbedoTint(const Material* material, const StaticMeshComponent* meshComponent);
		// Returns (uMin, vMin, uMax, vMax) of the component's scaled UVs,
		// (0,0,1,1) for wrapped/tiled or degenerate UVs. Memoised via
		// _meshUvRectCache - the raw bounds scan is O(vertices) once per
		// mesh, O(1) per call after.
		math::Vector4 ResolveMeshUvRect(const StaticMeshComponent* meshComponent);
		bool EnsureGpuVoxelTriangleBuffer(uint32_t levelIndex, uint32_t elementCapacity);
		bool EnsureFeedbackAccumBuffer(uint32_t feedbackLevel, uint32_t elementCount);
		bool EnsureInjectAccumBuffer(uint32_t elementCount);
		bool EnsureGpuGiLightBuffer(uint32_t elementCapacity);
		bool EnsureGpuGiMaterialBuffer(uint32_t elementCapacity);
		bool EnsureGpuGiMaterialTexelBuffer(uint32_t elementCapacity);
		bool EnsureGpuVoxelCandidateBuffer(uint32_t elementCapacity);
		uint64_t ComputeGiMaterialProxySignature() const;
		uint32_t BuildGpuVoxelTriangleList(Scene* scene, uint32_t levelIndex, std::vector<GpuVoxelTriangle>& out);
		uint32_t BuildGpuVoxelCandidateList(uint32_t levelIndex, uint32_t sourceTriangleCount, bool& outDispatchIndirectReady);
		void ExtractGiSceneProxies(
			Scene* scene,
			const GiClipmapParams& clipmapParams,
			std::vector<GiMeshInstanceProxy>& outMeshes,
			std::vector<GiMaterialProxy>& outMaterials,
			std::vector<GiLocalLightProxy>& outLights);
		void ExtractGiLocalLights(
			Scene* scene,
			const GiClipmapParams& clipmapParams,
			std::vector<GiLocalLightProxy>& outLights);
		void RunGpuVoxelization(Scene* scene, uint32_t levelIndex);

		void RenderTracePass(const GBuffer& gbuffer, ITexture2D* beautyTarget);
		void RenderResolvePass(const GBuffer& gbuffer);
		// Two-pass separable bilateral blur on _giResolved.a into
		// _giAoBlurred. Runs every frame so r_useGIAO can be flipped at
		// runtime without a setup delay; cost is two fullscreen quads.
		void RenderAoBlurPass(const GBuffer& gbuffer);
		void CompositeToBeauty(ITexture2D* beautyTarget);
		void DebugDrawProbeGrid(uint32_t levelIndex) const;
		void ApplyQualityPreset();

		uint32_t GetVoxelResolution() const;
		float GetBaseExtent() const;
		uint32_t GetFrameBudget() const;

	private:
		uint32_t _width = 0;
		uint32_t _height = 0;
		uint32_t _halfWidth = 0;
		uint32_t _halfHeight = 0;
		uint64_t _frameCounter = 0;
		uint32_t _activeClipmap = 0;
		bool _created = false;
		float _resolveStabilityBoost = 0.0f;
		// Per-frame base-injection scale from the scene ambient level (see
		// r_giLightCoupling). Computed in UpdateConstants, applied wherever
		// r_giDiffuseInjection feeds injection.
		float _lightCouplingScale = 1.0f;
		bool _lastLocalLightsOnlyDebug = false;
		float _lastLocalLightInjection = 1.0f;
		bool _lastLocalLightInjectionEnable = true;
		bool _lastDisableBaseAndSunInjection = false;
		bool _lastDisableBaseInjection = false;
		bool _lastDisableSunInjection = false;
		int32_t _lastLocalLightMaxPerMesh = 20;
		float _lastLocalLightBaseSuppression = 0.85f;
		float _lastLocalLightSunSuppression = 1.0f;
		float _lastLocalLightAlbedoWeight = 0.0f;
		float _lastBaseSunSmallTriangleDamp = 0.85f;
		float _lastMeshBaseInjectionNormalization = 1.0f;
		float _lastMeshSunInjectionNormalization = 0.0f;
		float _lastMeshBaseInjectionMinScale = 0.02f;
		float _lastMeshSunInjectionMinScale = 0.20f;
		float _lastBounceAlbedoMinLuma = 0.22f;
		float _lastBounceAlbedoRemapAmount = 0.75f;
		bool _lastTerrainProxyEnable = false;
		float _lastTerrainProxyInjectionScale = 0.02f;
		bool _lastGpuComputeBaseSunEnabled = false;
		// LOCAL-light inject signature (points/spots). The SUN part is tracked
		// separately below - it interpolates per frame during weather
		// transitions and must never share the local set's cache-nuke path.
		uint64_t _lastInjectLightSignature = 0ull;
		uint64_t _lastSunInjectSignature = 0ull;
		uint64_t _pendingSunInjectSignature = 0ull;
		uint32_t _sunInjectSignatureStableFrames = 0u;
		math::Vector3 _lastSunDirection = math::Vector3(0.0f, -1.0f, 0.0f);
		bool _lastSunDirectionInitialized = false;
		uint32_t _sunRelightFramesRemaining = 0;
		uint32_t _lightResetFramesRemaining = 0u;
		// Snap-on-change window: >0 for a few frames after a detected lighting-
		// state change (local light set, light revision, sun jump). While active,
		// params12.z tells the voxelize shaders to blend mostly to the NEW
		// injection and lift the per-update delta brake - history is stale by
		// definition, so easing in at the shimmer-safe rate just delays truth.
		uint32_t _injectSnapFramesRemaining = 0u;
		// Per-frame budget: only one level may run a FULL CPU triangle regather
		// per Update (clip 0 exempt) - deferred levels stay dirty and retry.
		bool _fullGatherConsumedThisFrame = false;
		// Set by BuildGpuVoxelTriangleList when its empty result means "gather
		// parked at the slice limit" rather than a transient anomaly - the
		// caller must then NOT bump the warm counter (warm keeps the settling
		// fast path re-running clip 0/1 every frame for the whole gather).
		bool _gatherParkedThisCall = false;
		// FNV-1a over every injection-affecting cvar (+ quantized coupling
		// scale). A change arms the snap window - GI settings respond in a few
		// updates instead of easing through the steady-state EMA for seconds.
		uint64_t _lastInjectionTuningHash = 0ull;
		math::Vector3 _lastCameraPosition = math::Vector3::Zero;
		bool _lastCameraPositionInitialized = false;
		uint32_t _cameraMotionFramesRemaining = 0u;
		float _cameraMotionBlend = 0.0f;

		std::array<ClipmapLevel, ClipmapCount> _clipmaps = {};
		mutable GIConstants _constants = {};
		std::array<std::vector<dx::BoundingBox>, ClipmapCount> _dirtyRegions = {};
		std::unordered_map<StaticMeshComponent*, MeshTrackingState> _meshTracking;
		std::unordered_map<MaterialAlbedoCacheKey, math::Vector3, MaterialAlbedoCacheKeyHash> _materialAlbedoCache;
		std::unordered_map<const Material*, MaterialTriangleAlbedoCacheEntry> _materialTriangleAlbedoCache;
		std::unordered_map<const Material*, MaterialTriangleAlbedoCacheEntry> _materialTriangleEmissiveCache;
		std::unordered_map<StaticMeshComponent*, MeshEmissiveCacheEntry> _meshEmissiveCache;
		std::unordered_map<const void*, MeshUvRectCacheEntry> _meshUvRectCache; // key: Mesh*
		std::vector<GpuVoxelTriangle> _voxelTriangleUpload;
		std::vector<GiMeshInstanceProxy> _giMeshProxies;
		std::vector<GiMaterialProxy> _giMaterialProxies;
		std::vector<GiLocalLightProxy> _giLightProxies;

		// In-flight time-sliced triangle gather (GPU base+sun path only). A full
		// regather of a big clip is 25-200ms of CPU in Debug; instead of paying
		// it in one frame the mesh loop stops after r_giGatherTrianglesPerFrame
		// appended triangles, parks its state here and resumes next frame (the
		// level keeps its previous radiance meanwhile via the transient-empty
		// path). Aborted whenever the scene revisions or the clip volume change.
		struct PendingTriangleGather
		{
			bool active = false;
			uint32_t nextMeshIndex = 0u;
			std::vector<GpuVoxelTriangle> triangles;
			std::vector<GiMeshInstanceProxy> meshes;
			std::vector<GiMaterialProxy> materials;
			std::vector<GiLocalLightProxy> lights;
			math::Vector3 center = math::Vector3::Zero;
			float extent = -1.0f;
			uint64_t geometryRevision = 0ull;
			uint64_t materialRevision = 0ull;
			uint32_t emissiveTriangleCount = 0u;
			uint32_t emissiveActiveTriangleCount = 0u;
			uint32_t emissiveTiledTriangleCount = 0u;
		};
		std::array<PendingTriangleGather, ClipmapCount> _pendingGather = {};
		std::vector<GpuGiLight> _gpuGiLightUpload;
		std::vector<GpuGiMaterial> _gpuGiMaterialUpload;
		std::vector<uint32_t> _gpuGiMaterialTexelUpload;
		std::unordered_map<const Material*, GpuGiMaterialTexelBinding> _gpuGiMaterialTexelLookup;
		std::unordered_map<const Material*, GpuGiMaterialTexelBinding> _gpuGiEmissiveTexelLookup;
		uint64_t _gpuGiMaterialUploadSignature = 0ull;
		bool _gpuGiMaterialUploadValid = false;
		std::unordered_map<const Material*, uint32_t> _giMaterialProxyLookup;
		GiRuntimeStats _stats = {};
		uint64_t _statsFrameCounter = 0ull;

		ITexture2D* _giHalfRes = nullptr;
		ITexture2D* _giResolved = nullptr;
		ITexture2D* _giHistory = nullptr;
		// AO blur targets: _giResolved.a → bilateral H pass → _giAoBlurredH →
		// bilateral V pass → _giAoBlurred. The provider samples _giAoBlurred
		// so r_useGIAO compound mode contributes smooth medium-scale AO
		// instead of the per-voxel grid pattern the raw alpha shows.
		ITexture2D* _giAoBlurredH = nullptr;
		ITexture2D* _giAoBlurred  = nullptr;
		IConstantBuffer* _constantBuffer = nullptr;
		IConstantBuffer* _voxelShiftConstantBuffer = nullptr;
		// Single float4 cbuffer feeding the blur shader: (dirX, dirY, sourceChannel, depthScale).
		// Re-bound each pass with the appropriate direction / source channel selector.
		IConstantBuffer* _aoBlurConstantBuffer = nullptr;
		// Per-level persistent triangle buffers: uploaded only when the cached
		// triangle list actually changes. Re-dispatching an unchanged level
		// (the steady-state injection-EMA refresh) binds the existing buffer -
		// no CPU-side vector copy and no Map/memcpy (~34MB + 10-15ms Debug per
		// far-clip refresh before this).
		std::array<ID3D11Buffer*, ClipmapCount> _voxelTriangleBuffer = {};
		std::array<ID3D11ShaderResourceView*, ClipmapCount> _voxelTriangleSrv = {};
		std::array<uint32_t, ClipmapCount> _voxelTriangleCapacity = {};
		std::array<uint32_t, ClipmapCount> _voxelTriangleGpuCount = {};
		std::array<bool, ClipmapCount> _voxelTriangleGpuValid = {};
		std::array<uint32_t, ClipmapCount> _voxelTriangleGpuEmissivePayloadCount = {};
		std::array<float, ClipmapCount> _voxelTriangleGpuEmissivePayloadMaxHint = {};
		// Set by BuildGpuVoxelTriangleList when it served the request from the
		// CPU cache AND the level's GPU buffer already holds that exact list -
		// the caller can then skip the upload and the payload rescan entirely.
		bool _gatherServedByGpuList = false;
		ID3D11Buffer* _giLightBuffer = nullptr;
		ID3D11ShaderResourceView* _giLightSrv = nullptr;
		uint32_t _giLightCapacity = 0;
		ID3D11Buffer* _giMaterialBuffer = nullptr;
		ID3D11ShaderResourceView* _giMaterialSrv = nullptr;
		uint32_t _giMaterialCapacity = 0;
		ID3D11Buffer* _giMaterialTexelBuffer = nullptr;
		ID3D11ShaderResourceView* _giMaterialTexelSrv = nullptr;
		uint32_t _giMaterialTexelCapacity = 0;
		ID3D11Buffer* _voxelCandidateBuffer = nullptr;
		ID3D11ShaderResourceView* _voxelCandidateSrv = nullptr;
		ID3D11UnorderedAccessView* _voxelCandidateUav = nullptr;
		ID3D11Buffer* _voxelCandidateCountBuffer = nullptr;
		// R32_UINT SRV over the 4-byte count buffer so the injection shaders can
		// read the live appended-candidate count directly (no CPU readback).
		ID3D11ShaderResourceView* _voxelCandidateCountSrv = nullptr;
		ID3D11Buffer* _voxelCandidateCountReadback = nullptr;
		ID3D11Buffer* _voxelCandidateDispatchArgs = nullptr;
		ID3D11UnorderedAccessView* _voxelCandidateDispatchArgsUav = nullptr;
		// Lit-scene feedback accumulators (uint4 per voxel: RGB scaled 1024 +
		// weight scaled 1024, atomically accumulated by the screen-feedback CS,
		// cleared before each scatter). Consumed by the eval voxelize at t13.
		std::array<ID3D11Buffer*, FeedbackLevelCount> _feedbackAccumBuffer = {};
		std::array<ID3D11UnorderedAccessView*, FeedbackLevelCount> _feedbackAccumUav = {};
		std::array<ID3D11ShaderResourceView*, FeedbackLevelCount> _feedbackAccumSrv = {};
		std::array<uint32_t, FeedbackLevelCount> _feedbackAccumElements = {};
		// Accum coords are voxel-space for the clip center at scatter time; a
		// consumed shift offsets them, so the accum is dropped on shift.
		std::array<bool, FeedbackLevelCount> _feedbackAccumValid = {};
		// Deterministic-injection accumulator (18 x 4B per voxel: radiance +
		// weight, albedo + weight, opacity max, signed L1 moments). Shared by
		// all clips (one level updates per dispatch), cleared before each
		// accumulate pass, resolved to the volumes by DiffuseGIInjectResolve.
		ID3D11Buffer* _injectAccumBuffer = nullptr;
		ID3D11UnorderedAccessView* _injectAccumUav = nullptr;
		ID3D11ShaderResourceView* _injectAccumSrv = nullptr;
		uint32_t _injectAccumElements = 0;
		uint32_t _voxelCandidateCapacity = 0;

		std::shared_ptr<IShader> _traceShader;
		std::shared_ptr<IShader> _resolveShader;
		std::shared_ptr<IShader> _fullScreenShader;
		// Wide separable bilateral blur on the GI AO alpha (see
		// DiffuseGIAOBlur.shader). Two passes - horizontal then vertical -
		// share the same shader; the direction is fed via the
		// _aoBlurConstantBuffer cbuffer above.
		std::shared_ptr<IShader> _aoBlurShader;
		std::shared_ptr<IShader> _voxelizeShader;
		std::shared_ptr<IShader> _voxelizeEvalShader;
		std::shared_ptr<IShader> _voxelCandidateShader;
		std::shared_ptr<IShader> _candidateArgsFixupShader;
		std::shared_ptr<IShader> _screenFeedbackShader;
		std::shared_ptr<IShader> _injectResolveShader;
		std::shared_ptr<IShader> _voxelClearShader;
		std::shared_ptr<IShader> _voxelPropagateShader;
		std::shared_ptr<IShader> _voxelShiftShader;
		std::array<std::vector<GpuVoxelTriangle>, ClipmapCount> _cachedVoxelTriangles = {};
		std::array<std::vector<GiMaterialProxy>, ClipmapCount> _cachedGiMaterialProxies = {};
		std::array<bool, ClipmapCount> _cachedVoxelTrianglesValid = { false, false, false, false };
		std::array<uint64_t, ClipmapCount> _cachedVoxelTrianglesFrame = { 0ull, 0ull, 0ull, 0ull };
		// Clip volume the cached triangles were gathered for. When the centre/
		// extent still match and the scene revisions are unchanged, the GPU
		// base+sun path can reuse the cache indefinitely (it stores geometry +
		// albedo only - lighting is recomputed on the GPU every dispatch).
		std::array<math::Vector3, ClipmapCount> _cachedVoxelTrianglesCenter = {};
		std::array<float, ClipmapCount> _cachedVoxelTrianglesExtent = { -1.0f, -1.0f, -1.0f, -1.0f };
		std::array<uint32_t, ClipmapCount> _cachedEmissiveMaterialCount = { 0u, 0u, 0u, 0u };
		std::array<uint32_t, ClipmapCount> _cachedEmissiveTriangleCount = { 0u, 0u, 0u, 0u };
		std::array<uint32_t, ClipmapCount> _cachedEmissiveActiveTriangleCount = { 0u, 0u, 0u, 0u };
		std::array<uint32_t, ClipmapCount> _cachedEmissiveTiledTriangleCount = { 0u, 0u, 0u, 0u };
		std::array<float, ClipmapCount> _cachedEmissiveProxyMaxLuma = { 0.0f, 0.0f, 0.0f, 0.0f };
		std::array<float, ClipmapCount> _cachedEmissiveProxyMaxStrength = { 0.0f, 0.0f, 0.0f, 0.0f };
		std::array<uint64_t, ClipmapCount> _cachedSceneGeometryRevision = { 0ull, 0ull, 0ull, 0ull };
		std::array<uint64_t, ClipmapCount> _cachedSceneMaterialRevision = { 0ull, 0ull, 0ull, 0ull };
		std::array<uint64_t, ClipmapCount> _cachedSceneLightRevision = { 0ull, 0ull, 0ull, 0ull };
		std::array<uint32_t, ClipmapCount> _clipmapWarmFramesRemaining = { 0u, 0u, 0u, 0u };
		std::array<uint64_t, ClipmapCount> _clipmapLastVoxelizationFrame = { 0ull, 0ull, 0ull, 0ull };
		uint32_t _sunRelightCooldownFrames = 0u;
		uint64_t _lastObservedSceneLightRevision = 0ull;
	};
}
