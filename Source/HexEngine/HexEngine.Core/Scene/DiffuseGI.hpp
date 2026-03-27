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
			float extent = 64.0f;
			uint32_t resolution = 48;
			bool dirty = true;
			bool initialized = false;
			math::Vector3 pendingShiftWs = math::Vector3::Zero;

			ITexture3D* radianceVolume = nullptr;
			ITexture3D* radianceScratchVolume = nullptr;
			ITexture3D* opacityVolume = nullptr;
			ITexture2D* probeIrradianceAtlas = nullptr;
			ITexture2D* probeVisibilityAtlas = nullptr;
			ID3D11UnorderedAccessView* radianceUav = nullptr;
			ID3D11UnorderedAccessView* radianceScratchUav = nullptr;
			ID3D11ShaderResourceView* radianceSrv = nullptr;
			ID3D11ShaderResourceView* radianceScratchSrv = nullptr;

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
		};

		struct GpuVoxelTriangle
		{
			math::Vector4 p0;
			math::Vector4 p1;
			math::Vector4 p2;
			math::Vector4 radianceOpacity;
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

	private:
		bool CreateClipmapResources();
		void DestroyClipmapResources();
		void RebuildClipmapTransforms(const math::Vector3& cameraPosition);
		void UpdateClipmapData(Scene* scene, uint32_t levelIndex);
		void UpdateProbeAtlases(ClipmapLevel& level);
		void UpdateConstants(Scene* scene);
		void AddDirtyRegion(uint32_t levelIndex, const dx::BoundingBox& bounds);
		bool IsMeshStateDirty(StaticMeshComponent* smc, const math::Vector3& worldPos);
		math::Vector3 GetMaterialAlbedoTint(const Material* material);
		bool EnsureGpuVoxelTriangleBuffer(uint32_t elementCapacity);
		uint32_t BuildGpuVoxelTriangleList(Scene* scene, uint32_t levelIndex, std::vector<GpuVoxelTriangle>& out);
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
		int32_t _lastLocalLightMaxPerMesh = 20;
		float _lastLocalLightBaseSuppression = 0.85f;
		float _lastLocalLightSunSuppression = 1.0f;
		float _lastLocalLightAlbedoWeight = 0.0f;
		math::Vector3 _lastSunDirection = math::Vector3(0.0f, -1.0f, 0.0f);
		bool _lastSunDirectionInitialized = false;
		uint32_t _sunRelightFramesRemaining = 0;

		std::array<ClipmapLevel, ClipmapCount> _clipmaps = {};
		mutable GIConstants _constants = {};
		std::array<std::vector<dx::BoundingBox>, ClipmapCount> _dirtyRegions = {};
		std::unordered_map<StaticMeshComponent*, MeshTrackingState> _meshTracking;
		std::unordered_map<const Material*, math::Vector3> _materialAlbedoCache;
		std::vector<GpuVoxelTriangle> _voxelTriangleUpload;

		ITexture2D* _giHalfRes = nullptr;
		ITexture2D* _giResolved = nullptr;
		ITexture2D* _giHistory = nullptr;
		IConstantBuffer* _constantBuffer = nullptr;
		IConstantBuffer* _voxelShiftConstantBuffer = nullptr;
		ID3D11Buffer* _voxelTriangleBuffer = nullptr;
		ID3D11ShaderResourceView* _voxelTriangleSrv = nullptr;
		uint32_t _voxelTriangleCapacity = 0;

		std::shared_ptr<IShader> _traceShader;
		std::shared_ptr<IShader> _resolveShader;
		std::shared_ptr<IShader> _fullScreenShader;
		std::shared_ptr<IShader> _voxelizeShader;
		std::shared_ptr<IShader> _voxelClearShader;
		std::shared_ptr<IShader> _voxelPropagateShader;
		std::shared_ptr<IShader> _voxelShiftShader;
		std::array<std::vector<GpuVoxelTriangle>, ClipmapCount> _cachedVoxelTriangles = {};
		std::array<bool, ClipmapCount> _cachedVoxelTrianglesValid = { false, false, false, false };
		std::array<uint64_t, ClipmapCount> _cachedVoxelTrianglesFrame = { 0ull, 0ull, 0ull, 0ull };
		std::array<uint32_t, ClipmapCount> _clipmapWarmFramesRemaining = { 0u, 0u, 0u, 0u };
	};
}
