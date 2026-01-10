

#pragma once

#include "Chunk.hpp"
#include "../Scene/IEntityListener.hpp"
#include "../Scene/PVS.hpp"
#include "../Scene/Scene.hpp"

namespace HexEngine
{
	class Camera;

	struct ChunkData
	{
		int32_t _numChunks = 0;
		float _chunkWidth = 0.0f;
		float _totalWidth = 0.0f;
		float _halfTotalWidth = 0.0f;

		HexEngine::Chunk*** _chunks = nullptr;
	};

	class HEX_API ChunkManager : public IEntityListener
	{
	public:
		friend class Chunk;

		void CreateChunks(Scene* scene, float chunkSize, int32_t numChunks);
		bool HasActiveChunks(Scene* scene) const;
		bool GetChunkData(Scene* scene, ChunkData* data) const;
		void CalculatePVS(Scene* scene, PVS* pvs, const PVSParams& params, std::vector<StaticMeshComponent*>& components);
		void RemoveAllChunks(Scene* scene);

		void DebugRender();

		//Tile* GetTileByPosition(HexEngine::Entity* entity, const math::Vector3& position) const;
		//Tile* GetTileByPosition(const math::Vector3& position) const;

		Chunk* GetChunkByPosition(ChunkData& data, const math::Vector3& position) const;

		void ForEachChunkExecute(ChunkData& data, std::function<void (int32_t, int32_t, HexEngine::Chunk*)> function) const;

		void RecalculateAllChunkBounds(Scene* scene);

		void EnableContinuousChunkBoundCalculation(bool enable) { _continuousCalculationEnabled = enable; }
		bool IsContinuousChunkBoundCalculationEnabled() const { return _continuousCalculationEnabled; }

		void Destroy();

		void CalculateVisibility(Scene* scene, Camera* camera, PVS::MeshInstanceMap& map);

		//void CalculateShadowVisibility(const dx::BoundingSphere& cascadeSphere, std::unordered_map<MeshInstance*, std::vector<std::pair<Mesh*, Entity*>>>& visMap);

		void OnEntityPositionChanged(Entity* entity, const math::Vector3& oldPosition, const math::Vector3& newPosition);

		// IEntityListener
		virtual void OnAddEntity(Entity* entity) override;

		virtual void OnRemoveEntity(Entity* entity) override;

		virtual void OnAddComponent(Entity* entity, BaseComponent* component) override;

		virtual void OnRemoveComponent(Entity* entity, BaseComponent* component) override;

		int32_t GetNumChunksVisible() const;

		void EnableCaching(bool enable) { _cachingEnabled = enable; }

		void ChunkLoader();

	private:
		//bool IsChunkOccluded(const math::Vector3& cameraPos, Chunk* chunk);
		bool _continuousCalculationEnabled = true;
		bool _cachingEnabled = false;

	public:
		std::map<Scene*, ChunkData> _sceneToChunkMap;

		const int32_t _visMapSizeIncrease = 1024;
		int32_t _chunksVisible = 0;

		std::thread _chunkLoader;
		std::recursive_mutex _loaderLock;
		std::list<Chunk*> _loadList;
		std::list<Chunk*> _unloadList;

		//std::vector<HexEngine::Chunk*> _chunks;
	};
}
