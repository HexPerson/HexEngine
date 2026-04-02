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

	private:
		static constexpr uint32_t ClipmapCount = 4;
		static constexpr uint32_t ProbeGridX = 16;
		static constexpr uint32_t ProbeGridY = 10;
		static constexpr uint32_t ProbeGridZ = 16;

		struct ClipmapLevel
		{
			math::Vector3 center = math::Vector3::Zero;
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

			std::vector<float> radianceCpu;
			std::vector<uint8_t> opacityCpu;
			std::vector<float> probeIrradianceCpu;
			std::vector<uint8_t> probeVisibilityCpu;
		};

		struct GIConstants
		{
			math::Vector4 clipCenterExtent[ClipmapCount];
			math::Vector4 clipVoxelInfo[ClipmapCount];
			math::Vector4 params0; // x=intensity, y=energyClamp, z=debugMode, w=activeClipmap
			math::Vector4 params1; // x=hysteresis, y=historyReject, z=halfResInvW, w=halfResInvH
			math::Vector4 params2; // x=screenBounce, y=probeBlend, z=voxelDecay, w=useVoxelAlphaOpacity
			math::Vector4 params3; // xyz=sunDirectionWS, w=sunDirectionality
			math::Vector4 params4; // x=jitterScale, y=clipBlendWidth, z=pixelMotionStart, w=pixelMotionStrength
			math::Vector4 params5; // x=luminanceRejectScale, y=ditherDarkAmp, z=ditherBrightAmp, w=movementPreset
			math::Vector4 params6; // x=voxelNeighbourBlend, y=shiftSettle, z=voxelAlbedoInfluence, w=reserved
			math::Vector4 params7; // x=gpuMaterialProxyBlend, y=gpuComputeBaseSunEnabled, zw=reserved
			math::Vector4 params8; // x=diffuseInject, y=sunInject, z=sunDirectionalBoost, w=emissiveInject
		};

		struct GpuVoxelTriangle
		{
			math::Vector4 p0;
			math::Vector4 p1;
			math::Vector4 p2;
			math::Vector4 radianceOpacity;
			math::Vector4 albedoWeight;
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
		};

		struct MaterialTriangleAlbedoCacheEntry
		{
			math::Vector3 diffuseTint = math::Vector3(0.75f, 0.75f, 0.75f);
			int32_t width = 0;
			int32_t height = 0;
			bool isBgra = false;
			bool hasTexture = false;
			std::vector<uint8_t> pixels;
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
		};

	private:
		bool CreateClipmapResources();
		void DestroyClipmapResources();
		void RebuildClipmapTransforms(const math::Vector3& cameraPosition);
		void UpdateClipmapData(Scene* scene, uint32_t levelIndex);
		void UpdateProbeAtlases(ClipmapLevel& level);
		void UpdateConstants(Scene* scene);
		void AddDirtyRegion(uint32_t levelIndex, const dx::BoundingBox& bounds);
		bool IsMeshStateDirty(StaticMeshComponent* smc, const math::Vector3& worldPos);
		math::Vector3 GetMaterialAlbedoTint(const Material* material, const StaticMeshComponent* meshComponent);
		bool EnsureGpuVoxelTriangleBuffer(uint32_t elementCapacity);
		bool EnsureGpuGiLightBuffer(uint32_t elementCapacity);
		bool EnsureGpuGiMaterialBuffer(uint32_t elementCapacity);
		bool EnsureGpuVoxelCandidateBuffer(uint32_t elementCapacity);
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
		bool _lastGpuComputeBaseSunEnabled = false;
		uint64_t _lastInjectLightSignature = 0ull;
		math::Vector3 _lastSunDirection = math::Vector3(0.0f, -1.0f, 0.0f);
		bool _lastSunDirectionInitialized = false;
		uint32_t _sunRelightFramesRemaining = 0;

		std::array<ClipmapLevel, ClipmapCount> _clipmaps = {};
		mutable GIConstants _constants = {};
		std::array<std::vector<dx::BoundingBox>, ClipmapCount> _dirtyRegions = {};
		std::unordered_map<StaticMeshComponent*, MeshTrackingState> _meshTracking;
		std::unordered_map<MaterialAlbedoCacheKey, math::Vector3, MaterialAlbedoCacheKeyHash> _materialAlbedoCache;
		std::unordered_map<const Material*, MaterialTriangleAlbedoCacheEntry> _materialTriangleAlbedoCache;
		std::vector<GpuVoxelTriangle> _voxelTriangleUpload;
		std::vector<GiMeshInstanceProxy> _giMeshProxies;
		std::vector<GiMaterialProxy> _giMaterialProxies;
		std::vector<GiLocalLightProxy> _giLightProxies;
		std::vector<GpuGiLight> _gpuGiLightUpload;
		std::vector<GpuGiMaterial> _gpuGiMaterialUpload;
		std::unordered_map<const Material*, uint32_t> _giMaterialProxyLookup;
		GiRuntimeStats _stats = {};
		uint64_t _statsFrameCounter = 0ull;

		ITexture2D* _giHalfRes = nullptr;
		ITexture2D* _giResolved = nullptr;
		ITexture2D* _giHistory = nullptr;
		IConstantBuffer* _constantBuffer = nullptr;
		IConstantBuffer* _voxelShiftConstantBuffer = nullptr;
		ID3D11Buffer* _voxelTriangleBuffer = nullptr;
		ID3D11ShaderResourceView* _voxelTriangleSrv = nullptr;
		uint32_t _voxelTriangleCapacity = 0;
		ID3D11Buffer* _giLightBuffer = nullptr;
		ID3D11ShaderResourceView* _giLightSrv = nullptr;
		uint32_t _giLightCapacity = 0;
		ID3D11Buffer* _giMaterialBuffer = nullptr;
		ID3D11ShaderResourceView* _giMaterialSrv = nullptr;
		uint32_t _giMaterialCapacity = 0;
		ID3D11Buffer* _voxelCandidateBuffer = nullptr;
		ID3D11ShaderResourceView* _voxelCandidateSrv = nullptr;
		ID3D11UnorderedAccessView* _voxelCandidateUav = nullptr;
		ID3D11Buffer* _voxelCandidateCountBuffer = nullptr;
		ID3D11Buffer* _voxelCandidateCountReadback = nullptr;
		ID3D11Buffer* _voxelCandidateDispatchArgs = nullptr;
		uint32_t _voxelCandidateCapacity = 0;

		std::shared_ptr<IShader> _traceShader;
		std::shared_ptr<IShader> _resolveShader;
		std::shared_ptr<IShader> _fullScreenShader;
		std::shared_ptr<IShader> _voxelizeShader;
		std::shared_ptr<IShader> _voxelizeEvalShader;
		std::shared_ptr<IShader> _voxelCandidateShader;
		std::shared_ptr<IShader> _voxelClearShader;
		std::shared_ptr<IShader> _voxelPropagateShader;
		std::shared_ptr<IShader> _voxelShiftShader;
		std::array<std::vector<GpuVoxelTriangle>, ClipmapCount> _cachedVoxelTriangles = {};
		std::array<bool, ClipmapCount> _cachedVoxelTrianglesValid = { false, false, false, false };
		std::array<uint64_t, ClipmapCount> _cachedVoxelTrianglesFrame = { 0ull, 0ull, 0ull, 0ull };
		std::array<uint32_t, ClipmapCount> _clipmapWarmFramesRemaining = { 0u, 0u, 0u, 0u };
	};
}
