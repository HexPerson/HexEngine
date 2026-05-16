#pragma once

#include <array>
#include <compare>

#include "MarchingCubes.hpp"
#include "SdfTerrainGenerator.hpp"

namespace HexEngine::VolumetricTerrain
{
	struct ChunkCoord
	{
		int32_t x = 0;
		int32_t y = 0;
		int32_t z = 0;

		auto operator<=>(const ChunkCoord&) const = default;
	};

	struct ChunkCoordHash
	{
		size_t operator()(const ChunkCoord& c) const
		{
			const uint64_t h1 = static_cast<uint32_t>(c.x) * 73856093ull;
			const uint64_t h2 = static_cast<uint32_t>(c.y) * 19349663ull;
			const uint64_t h3 = static_cast<uint32_t>(c.z) * 83492791ull;
			return static_cast<size_t>(h1 ^ h2 ^ h3);
		}
	};

	enum class BrushMode : int32_t
	{
		Add,
		Subtract,
		Elevate,
		Flatten,
		Smooth,
		Erode,
		PaintMaterial,
		Tunnel,
		Noise
	};

	struct BrushSettings
	{
		BrushMode mode = BrushMode::Subtract;
		float radius = 16.0f;
		float strength = 10.0f;
		float falloff = 1.8f;
		float hardness = 0.5f;
		float targetHeight = 0.0f;
		float noiseScale = 4.0f;
		int32_t materialIndex = 0;
		bool symmetry = false;
		bool useGpu = false; // Experimental path; CPU brush remains the reliable default.
	};

	class VolumetricTerrainChunk
	{
	public:
		static constexpr uint32_t kGpuSurfaceLodCount = 3;

		// Density convention used throughout volumetric terrain:
		// density < 0 -> solid voxel space, density >= 0 -> empty space.
		~VolumetricTerrainChunk();

		VolumetricTerrainChunk(
			const ChunkCoord& coord,
			const SdfTerrainGenerationParams& params,
			const math::Vector3& origin,
			Entity* parentEntity);

		void Generate(SdfTerrainGenerator& generator);
		void RebuildMesh(const MarchingCubes& marchingCubes, bool rebuildCollision);
		void RebuildCollision();
		bool ApplyBrush(const math::Vector3& center, const BrushSettings& settings, float deltaTime);
		void ReleaseEntityReferences(bool queueDeleteEntity);
		void SyncDensityToGpu();
		void SyncMaterialsToGpu();
		void SetVisualMeshHidden(bool hidden);
		bool BuildGpuSurface(uint32_t lodIndex);
		void RenderGpuSurface(uint32_t lodIndex);

		void MarkDirtyAll();
		void MarkDirtyMeshOnly();

		const ChunkCoord& GetCoord() const { return _coord; }
		const dx::BoundingBox& GetBounds() const { return _bounds; }
		bool HasEdits() const { return _hasEdits; }
		bool HasMaterialEdits() const { return _hasMaterialEdits; }
		bool IsGenerated() const { return _generated; }
		bool IsDensityDirty() const { return _densityDirty; }
		bool IsMeshDirty() const { return _meshDirty; }
		bool IsCollisionDirty() const { return _collisionDirty; }
		bool IsMaterialDirty() const { return _materialDirty; }

		void ClearDensityDirty() { _densityDirty = false; }
		void ClearMeshDirty() { _meshDirty = false; }
		void ClearCollisionDirty() { _collisionDirty = false; }
		void ClearMaterialDirty() { _materialDirty = false; }

		const std::vector<float>& GetDensities() const { return _densities; }
		const std::vector<uint8_t>& GetMaterials() const { return _materials; }
		const std::vector<uint8_t>& GetMaterialWeights() const { return _materialWeights; }
		float GetDensityAt(int32_t x, int32_t y, int32_t z) const;
		void SetDensityAt(int32_t x, int32_t y, int32_t z, float density);
		uint8_t GetMaterialAt(int32_t x, int32_t y, int32_t z) const;
		void SetMaterialAt(int32_t x, int32_t y, int32_t z, uint8_t material);
		std::array<uint8_t, 4> GetMaterialWeightsAt(int32_t x, int32_t y, int32_t z) const;
		void SetMaterialWeightsAt(int32_t x, int32_t y, int32_t z, const std::array<uint8_t, 4>& weights);
		void SetEditedData(const std::vector<float>& densities, const std::vector<uint8_t>& materials, const std::vector<uint8_t>& materialWeights = {});

		int32_t GetResolution() const { return _params.chunkResolution; }
		float GetVoxelSize() const { return _voxelSize; }
		const math::Vector3& GetOrigin() const { return _origin; }
		Entity* GetEntity() const { return _entity; }
		float SampleDensityNearestWorld(const math::Vector3& worldPosition) const;
		bool EnsureGpuSurfacePipeline();
		bool EnsureGpuSurfaceResources(uint32_t lodIndex);
		void ReleaseGpuSurfaceResources();
		void OffsetWorld(const math::Vector3& delta);

	private:
		float SmoothMin(float a, float b, float k) const;
		float SmoothMax(float a, float b, float k) const;
		void RelaxEditedRegion(
			const math::Vector3& center,
			float radius,
			float hardness,
			int32_t minX,
			int32_t maxX,
			int32_t minY,
			int32_t maxY,
			int32_t minZ,
			int32_t maxZ);
		bool IsGpuBrushMode(BrushMode mode) const;
		bool EnsureGpuDensityResources();
		bool EnsureGpuMaterialResources();
		void ReleaseGpuDensityResources();
		void ReleaseGpuMaterialResources();
		void UploadDensityToGpu();
		void UploadMaterialsToGpu();
		void ReadbackDensityFromGpu();
		std::array<uint8_t, 4> ComputeAutoMaterialWeightsAt(int32_t x, int32_t y, int32_t z) const;
		void InitializeAutoMaterialWeights();
		void InitializeMaterialWeightsFromIndices();
		void SetMaterialWeightsOneHot(size_t voxelIndex, uint8_t materialIndex);
		void BlendMaterialWeight(size_t voxelIndex, uint8_t materialIndex, float amount);
		bool ApplyBrushCpu(const math::Vector3& center, const BrushSettings& settings, float deltaTime, bool uploadGpuAfterModify, bool runRelaxation);
		bool ApplyBrushGpu(const math::Vector3& center, const BrushSettings& settings, float deltaTime);
		uint32_t GetGpuLodStep(uint32_t lodIndex) const;
		uint32_t GetGpuLodResolution(uint32_t lodIndex) const;
		int32_t Index(int32_t x, int32_t y, int32_t z) const;
		float ComputeFalloff(float distance, float radius, float falloffPower) const;
		std::vector<float> BuildCollisionDensityField(int32_t collisionResolution) const;
		math::Vector3 DensityToWorld(int32_t x, int32_t y, int32_t z) const;

	private:
		ChunkCoord _coord;
		SdfTerrainGenerationParams _params;
		math::Vector3 _origin = math::Vector3::Zero;
		float _voxelSize = 1.0f;
		dx::BoundingBox _bounds{};

		std::vector<float> _densities;
		std::vector<uint8_t> _materials;
		std::vector<uint8_t> _materialWeights;
		bool _generated = false;
		bool _densityDirty = true;
		bool _meshDirty = true;
		bool _collisionDirty = true;
		bool _materialDirty = true;
		bool _hasEdits = false;
		bool _hasMaterialEdits = false;

		Entity* _entity = nullptr;
		StaticMeshComponent* _meshComponent = nullptr;
		RigidBody* _rigidBody = nullptr;
		std::shared_ptr<Mesh> _mesh;
		ITexture3D* _gpuDensityTexture = nullptr;
		ITexture3D* _gpuMaterialTexture = nullptr;
		IStructuredBuffer* _gpuSurfaceTriangles[kGpuSurfaceLodCount] = {};
		IStructuredBuffer* _gpuSurfaceDrawArgs[kGpuSurfaceLodCount] = {};
		uint32_t _gpuSurfaceTriangleCapacity[kGpuSurfaceLodCount] = {};
		bool _gpuSurfaceReady[kGpuSurfaceLodCount] = {};
	};
}
