#pragma once

#include "VolumetricTerrainChunk.hpp"
#include <HexEngine.Core/Scene/ISceneCustomRenderer.hpp>

namespace HexEngine::VolumetricTerrain
{
	struct TerrainStats
	{
		int32_t dirtyChunks = 0;
		int32_t totalChunks = 0;
		uint64_t densityMemoryBytes = 0;
		uint64_t triangleCount = 0;
		float lastMeshRebuildMs = 0.0f;
	};

	struct EditedChunkBlob
	{
		ChunkCoord coord;
		std::vector<float> densities;
		std::vector<uint8_t> materials;
		std::vector<uint8_t> materialWeights;
		bool hasMaterialEdits = false;

		// Pre-cooked PhysX triangle mesh for this chunk's collision. Populated
		// either:
		//   - on the editor's save path, after the async cook finishes (so the
		//     scene file captures the cooked bytes), OR
		//   - on Deserialize when loading a scene that has the cached blob
		// When present and the chunk's density hasn't been edited, the runtime
		// can skip PxCookTriangleMesh entirely and feed these bytes directly
		// into PxPhysics::createTriangleMesh - turning ~50-70ms of CPU cook
		// work per chunk into a ~5ms shape-create on the main thread.
		std::vector<uint8_t> cookedCollisionBlob;
	};

	class VolumetricTerrain : public HexEngine::ISceneCustomRenderer
	{
public:
		VolumetricTerrain();
		~VolumetricTerrain();

		void Initialize(Entity* owner, const SdfTerrainGenerationParams& params);
		void Regenerate(bool preserveEdits);
		void RebuildAll(bool rebuildCollision);
		void Tick(float deltaTime);
		bool ApplyBrush(const math::Vector3& worldPosition, const BrushSettings& settings, float deltaTime);
		void QueueCollisionRefresh();
		void FlushCollisionRefresh();
		void SetSculptingActive(bool active) { _sculptingActive = active; }
		void SetOwner(Entity* owner);

		void Serialize(json& data, JsonFile* file);
		void Deserialize(json& data, JsonFile* file);

		void SetBrush(const BrushSettings& settings) { _brush = settings; }
		const BrushSettings& GetBrush() const { return _brush; }
		SdfTerrainGenerationParams& GetGenerationParams() { return _params; }
		const SdfTerrainGenerationParams& GetGenerationParams() const { return _params; }
		const TerrainStats& GetStats() const { return _stats; }
		bool IsInitialized() const { return _initialized; }

		Entity* FindChunkEntityFromRayHit(Entity* hitEntity) const;
		bool RaycastTerrainBounds(const math::Ray& ray, math::Vector3& hitPosition) const;
		bool RaycastTerrainSurface(const math::Ray& ray, float maxDistance, math::Vector3& hitPosition) const;
		Entity* GetRootEntity() const { return _rootEntity; }
		virtual void RenderCustom(Scene* scene, Camera* camera, MeshRenderFlags renderFlags) override;

	private:
		void BuildGpuVisualsOrFallback(bool rebuildCollisionFallback);
		void BuildChunks();
		void StitchChunkBorders();
		void StitchChunkBordersAt(const ChunkCoord& coord);
		void RegenerateDirtyChunks();
		bool ProcessCollisionRefreshStep(int32_t chunkBudget);
		void MarkNeighbourChunksDirty(const ChunkCoord& coord, const math::Vector3& worldPosition, float brushRadius);
		void GatherEditedChunkData(std::vector<EditedChunkBlob>& chunks) const;
		void ApplyEditedChunkData(const std::vector<EditedChunkBlob>& chunks);

	private:
		Entity* _owner = nullptr;
		Entity* _rootEntity = nullptr;
		math::Vector3 _lastOwnerPosition = math::Vector3::Zero;
		SdfTerrainGenerationParams _params;
		BrushSettings _brush;
		MarchingCubes _marchingCubes;
		std::unique_ptr<SdfTerrainGenerator> _generator;
		std::unordered_map<ChunkCoord, std::unique_ptr<VolumetricTerrainChunk>, ChunkCoordHash> _chunks;

		// Pre-baked per-chunk snapshot data populated by Deserialize, consumed
		// by BuildChunks. When a chunk's coord has an entry here, BuildChunks
		// skips SDF generation for that chunk entirely and applies the
		// snapshot directly via VolumetricTerrainChunk::ApplyBakedData. Empty
		// (no entries) when the scene was authored without a snapshot - in
		// which case BuildChunks runs the original SDF generation flow.
		std::unordered_map<ChunkCoord, EditedChunkBlob, ChunkCoordHash> _bakedChunks;
		TerrainStats _stats;
		bool _gpuVisualsEnabled = false;
		bool _initialized = false;
		float _collisionDebounce = 0.0f;
		float _collisionStepAccumulator = 0.0f;
		bool _pendingCollisionRefresh = false;
		bool _collisionRefreshStarted = false;
		bool _pendingPvsRefresh = false;
		bool _sculptingActive = false;
	};
}
