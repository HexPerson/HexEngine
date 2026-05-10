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
	};

	class VolumetricTerrain : public HexEngine::ISceneCustomRenderer
	{
public:
		VolumetricTerrain();
		~VolumetricTerrain();

		void Initialize(Entity* owner, const SdfTerrainGenerationParams& params);
		void RebuildAll(bool rebuildCollision);
		void Tick(float deltaTime);
		bool ApplyBrush(const math::Vector3& worldPosition, const BrushSettings& settings, float deltaTime);
		void QueueCollisionRefresh();
		void FlushCollisionRefresh();
		void SetSculptingActive(bool active) { _sculptingActive = active; }

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
		SdfTerrainGenerationParams _params;
		BrushSettings _brush;
		MarchingCubes _marchingCubes;
		std::unique_ptr<SdfTerrainGenerator> _generator;
		std::unordered_map<ChunkCoord, std::unique_ptr<VolumetricTerrainChunk>, ChunkCoordHash> _chunks;
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
