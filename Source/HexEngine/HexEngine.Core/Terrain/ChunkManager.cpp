

#include "ChunkManager.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../Math/FloatMath.hpp"
#include "../Scene/SceneManager.hpp"
#include "../Entity/Component/Transform.hpp"
#include "../Entity/Component/StaticMeshComponent.hpp"
#include "../Environment/LogFile.hpp"
#include "../Input/HVar.hpp"
#include "../Graphics/DebugRenderer.hpp"
#include <unordered_set>

namespace HexEngine
{
	extern HVar r_lodPartition;
	extern HVar r_shadowMinimumLodThreshold;

	namespace
	{
		bool IsHlodEntity(const Entity* entity)
		{
			return entity != nullptr && entity->GetName().rfind("HLOD_", 0) == 0;
		}
	}

	void ChunkManager::CreateChunks(Scene* scene, float chunkSize, int32_t numChunks)
	{
		if (HasActiveChunks(scene))
		{
			LOG_WARN("Trying to create chunks for a scene that already has chunks!");
			return;
		}

		ChunkData data;

		data._numChunks = numChunks;
		data._chunkWidth = chunkSize;
		data._totalWidth = data._chunkWidth * (float)numChunks;
		data._halfTotalWidth = data._totalWidth / 2.0f;

		math::Vector3 min, max;
		scene->CalculateBounds(min, max);

		float dx = (max.x - min.x) / (float)numChunks;
		float dz = (max.z - min.z) / (float)numChunks;

		math::Vector3 start = min;
		start.x += dx / 2.0f;
		start.z += dz / 2.0f;

		int32_t halfGridSize = numChunks / 2;

		data._chunks = new Chunk ** [numChunks];

		int32_t currentId = 0;

		for (int32_t i = 0; i < numChunks; ++i)
		{
			data._chunks[i] = new HexEngine::Chunk*[numChunks];

			for (int32_t j = 0; j < numChunks; ++j)
			{
				dx::BoundingBox initialVolume;
				initialVolume.Extents = math::Vector3(dx / 2.0f, 1.0f, dz / 2.0f);
				initialVolume.Center = math::Vector3(start.x + (float)i * dx, start.y, start.z + (float)j * dz);

				Chunk* chunk = new Chunk(initialVolume, currentId);

				currentId++;

				data._chunks[i][j] = chunk;
			}
		}

		g_pEnv->_sceneManager->GetCurrentScene()->AddEntityListener(this);		

		_sceneToChunkMap[scene] = data;

		// because the scene may already have entities in it that the chunk system might not be aware of, we should add them all back in

		std::vector<StaticMeshComponent*> components;
		scene->GetComponents<StaticMeshComponent>(components);

		for (auto& comp : components)
		{
			OnAddComponent(comp->GetEntity(), comp);
		}

		//_chunkLoader = std::thread(&ChunkManager::ChunkLoader, this);
		//_chunkLoader.detach();
	}

	int32_t ChunkManager::GetNumChunksVisible() const
	{
		return _chunksVisible;
	}

	bool ChunkManager::HasActiveChunks(Scene* scene) const
	{
		return _sceneToChunkMap.find(scene) != _sceneToChunkMap.end();
	}

	bool ChunkManager::GetChunkData(Scene* scene, ChunkData* data) const
	{
		auto chunkData = _sceneToChunkMap.find(scene);

		if (chunkData == _sceneToChunkMap.end())
			return false;

		memcpy(data, &chunkData->second, sizeof(ChunkData));
		return true;
	}

	void ChunkManager::Destroy()
	{
		for (auto it = _sceneToChunkMap.begin(); it != _sceneToChunkMap.end(); it++)
		{
			RemoveAllChunks(it->first);
		}	

		g_pEnv->_sceneManager->GetCurrentScene()->RemoveEntityListener(this);
	}

	void ChunkManager::RemoveAllChunks(Scene* scene)
	{
		for (auto it = _sceneToChunkMap.begin(); it != _sceneToChunkMap.end(); it++)
		{
			if (it->first == scene)
			{
				for (auto i = 0; i < it->second._numChunks; ++i)
				{
					for (auto j = 0; j < it->second._numChunks; ++j)
					{
						SAFE_DELETE(it->second._chunks[i][j]);
					}

					SAFE_DELETE_ARRAY(it->second._chunks[i]);
				}

				SAFE_DELETE_ARRAY(it->second._chunks);

				_sceneToChunkMap.erase(it);
				return;
			}
		}
	}

	void ChunkManager::ForEachChunkExecute(ChunkData& data, std::function<void(int32_t, int32_t, HexEngine::Chunk*)> function) const
	{
		for (int32_t i = 0; i < data._numChunks; ++i)
		{
			for (int32_t j = 0; j < data._numChunks; ++j)
			{
				function(i, j, data._chunks[i][j]);
			}
		}
	}

	Chunk* ChunkManager::GetChunkByPosition(ChunkData& data, const math::Vector3& position) const
	{
		float posX = position.x + data._halfTotalWidth + data._chunkWidth / 2.0f;// -(_tileSize / 2.0f);
		float posZ = position.z + data._halfTotalWidth + data._chunkWidth / 2.0f;// -(_tileSize / 2.0f);

		//posX -= _tileSize;
		//posZ -= _tileSize;

		float xposCorrected;// = HexEngine::RoundDownToNearest(posX, _tileSize);
		float zposCorrected;// = HexEngine::RoundDownToNearest(posZ, _tileSize);

		xposCorrected = HexEngine::RoundDownToNearest(posX, data._chunkWidth);
		zposCorrected = HexEngine::RoundDownToNearest(posZ, data._chunkWidth);

		int32_t xpos = (int32_t)(xposCorrected / data._chunkWidth);
		int32_t zpos = (int32_t)(zposCorrected / data._chunkWidth);

		if (xpos < 0 || xpos >= data._numChunks)
			return nullptr;
		if (zpos < 0 || zpos >= data._numChunks)
			return nullptr;
		
		return data._chunks[xpos][zpos];
	}

	void ChunkManager::OnAddEntity(Entity* entity)
	{
		if (entity->GetLayer() == Layer::Sky)
			return;
		if (IsHlodEntity(entity))
			return;

		auto chunkData = _sceneToChunkMap.find(entity->GetScene());

		if (chunkData == _sceneToChunkMap.end())
			return;

		auto chunk = entity->GetChunk() ? entity->GetChunk() : GetChunkByPosition(chunkData->second, entity->GetPosition());

		if (chunk == nullptr)
			return;

		LOG_DEBUG("Adding entity %p to chunk %p", entity, chunk);

		chunk->AddChunkEntity(entity);
	}

	void ChunkManager::OnRemoveEntity(Entity* entity)
	{
		if (entity->GetLayer() == Layer::Sky)
			return;
		if (IsHlodEntity(entity))
			return;

		auto chunkData = _sceneToChunkMap.find(entity->GetScene());

		if (chunkData == _sceneToChunkMap.end())
			return;

		auto chunk = entity->GetChunk() ? entity->GetChunk() : GetChunkByPosition(chunkData->second, entity->GetPosition());

		if (chunk == nullptr)
			return;

		LOG_DEBUG("Removing entity %p from chunk %p", entity, chunk);

		chunk->RemoveChunkEntity(entity);
	}

	void ChunkManager::OnAddComponent(Entity* entity, BaseComponent* component)
	{
		if (component->GetComponentId() != StaticMeshComponent::_GetComponentId())
			return;

		if (entity->GetLayer() == Layer::Sky)
			return;
		if (IsHlodEntity(entity))
			return;

		auto chunkData = _sceneToChunkMap.find(entity->GetScene());

		if (chunkData == _sceneToChunkMap.end())
			return;

		auto chunk = entity->GetChunk() ? entity->GetChunk() : GetChunkByPosition(chunkData->second, entity->GetPosition());

		if (chunk == nullptr)
			return;

		chunk->AddChunkComponent(entity, component);
	}

	void ChunkManager::OnRemoveComponent(Entity* entity, BaseComponent* component)
	{
		if (component->GetComponentId() != StaticMeshComponent::_GetComponentId())
			return;

		if (entity->GetLayer() == Layer::Sky)
			return;
		if (IsHlodEntity(entity))
			return;

		auto chunkData = _sceneToChunkMap.find(entity->GetScene());

		if (chunkData == _sceneToChunkMap.end())
			return;

		auto chunk = entity->GetChunk() ? entity->GetChunk() : GetChunkByPosition(chunkData->second, entity->GetPosition());

		if (chunk == nullptr)
			return;

		chunk->RemoveChunkComponent(entity, component);
	}

	void ChunkManager::RecalculateAllChunkBounds(Scene* scene)
	{
		auto chunkData = _sceneToChunkMap.find(scene);

		if (chunkData == _sceneToChunkMap.end())
			return;

		ForEachChunkExecute(chunkData->second, [](int32_t, int32_t, Chunk* chunk) {

			chunk->RecalculateAABB();

		});
	}

	void ChunkManager::OnEntityPositionChanged(Entity* entity, const math::Vector3& oldPosition, const math::Vector3& newPosition)
	{
		//if (entity->GetLayer() == Layer::Sky)
		//	return;

		auto chunkData = _sceneToChunkMap.find(entity->GetScene());

		if (chunkData == _sceneToChunkMap.end())
			return;

		auto oldChunk = GetChunkByPosition(chunkData->second, oldPosition);
		auto newChunk = GetChunkByPosition(chunkData->second, newPosition);

		// we crossed the boundary between chunks, switch us to the new chunk
		if (oldChunk != newChunk)
		{
			LOG_DEBUG("Entity '%s' [%p] is being moved from chunk %p to chunk %p", entity->GetName().c_str(), entity, oldChunk, newChunk);

			if (oldChunk)
			{
				oldChunk->RemoveChunkEntity(entity);
			}

			if (newChunk)
			{
				newChunk->AddChunkEntity(entity);

				for (auto& mesh : entity->GetComponents<StaticMeshComponent>())
				{
					newChunk->AddChunkComponent(entity, mesh);
				}
			}
		}
	}

#if 0
	bool IsOccluded(const math::Vector3& cameraPos, const std::unordered_map<MeshInstance*, std::vector<std::pair<Mesh*, Entity*>>>& visMap, Entity* entity)
	{
		const auto& aabb = entity->GetWorldAABB();

		const float distanceFromCamera = (entity->GetPosition() - cameraPos).Length();

		math::Vector3 mins, maxs;
		mins.x = aabb.Center.x - aabb.Extents.x;
		mins.y = aabb.Center.y - aabb.Extents.y;
		mins.z = aabb.Center.z - aabb.Extents.z;

		maxs.x = aabb.Center.x + aabb.Extents.x;
		maxs.y = aabb.Center.y + aabb.Extents.y;
		maxs.z = aabb.Center.z + aabb.Extents.z;
		
		auto projection = math::Matrix::CreatePerspectiveOffCenter(mins.x, maxs.x, mins.y, maxs.y, mins.z, maxs.z);

		dx::BoundingFrustum frustumForAABB;
		dx::BoundingFrustum::CreateFromMatrix(frustumForAABB, projection, true);		

		for (auto& it : visMap)
		{
			for (auto ent : it.second)
			{
				if (ent.second == entity)
					continue;

				if (frustumForAABB.Intersects(ent.second->GetWorldAABB()))
				{
					if ((ent.second->GetPosition() - cameraPos).Length() < distanceFromCamera)
						return true;
				}
			}
		}

		return false;
	}


	bool ChunkManager::IsChunkOccluded(const math::Vector3& cameraPos, Chunk* chunk)
	{
		const auto& aabb = chunk->GetBoundingVolume();

		const float distanceFromCamera = (chunk->GetBoundingVolume().Center - cameraPos).Length();

		auto chunkView = math::Matrix::CreateLookAt(cameraPos, aabb.Center, math::Vector3::Up);

		//aabb.Transform(aabb, chunkView);

		math::Vector3 mins, maxs;
		mins.x = aabb.Center.x - aabb.Extents.x;
		mins.y = aabb.Center.y - aabb.Extents.y;
		mins.z = aabb.Center.z - aabb.Extents.z;

		maxs.x = aabb.Center.x + aabb.Extents.x;
		maxs.y = aabb.Center.y + aabb.Extents.y;
		maxs.z = aabb.Center.z + aabb.Extents.z;

		const float nearClipOffset = 50.0f;

		auto projection = math::Matrix::CreatePerspectiveOffCenter(mins.x, maxs.x, mins.y, maxs.y, 1.0f, distanceFromCamera + aabb.Extents.z);

		//math::Matrix::CreatePerspectiveOffCenter

		//auto projection = math::Matrix::CreatePerspectiveFieldOfView(ToRadian(60.0f), g_pEnv->GetAspectRatio(), 1.0f, g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera()->GetFarZ());

		dx::BoundingFrustum frustumForAABB;
		dx::BoundingFrustum::CreateFromMatrix(frustumForAABB, projection, true);

		auto viewMatrixInverse = chunkView;
		viewMatrixInverse = viewMatrixInverse.Invert();

		frustumForAABB.Transform(frustumForAABB, viewMatrixInverse);

		for (int32_t i = 0; i < _numChunks; ++i)
		{
			for (int32_t j = 0; j < _numChunks; ++j)
			{
				auto thisChunk = _chunks[i][j];

				if (thisChunk == chunk)
					continue;

				const auto& thisChunkAABB = thisChunk->GetBoundingVolume();

				if (frustumForAABB.Intersects(thisChunkAABB))
				{
					if ((thisChunkAABB.Center - cameraPos).Length() < distanceFromCamera)
						return true;
				}
			}
		}

		return false;
	}
#endif

	void ChunkManager::DebugRender()
	{
		auto chunkData = _sceneToChunkMap.find(g_pEnv->_sceneManager->GetCurrentScene().get());

		if (chunkData == _sceneToChunkMap.end())
			return;

		ForEachChunkExecute(chunkData->second, [](int32_t, int32_t, Chunk* chunk) {
			g_pEnv->_debugRenderer->DrawAABB(chunk->GetBoundingVolume(), math::Color(1, 0, 0, 1));
			});
	}

	void ChunkManager::CalculatePVS(Scene* scene, PVS* pvs, const PVSParams& params, std::vector<StaticMeshComponent*>& components)
	{
		static std::unordered_set<Scene*> s_chunkBoundsRefreshed;
		if (!params.isShadow && s_chunkBoundsRefreshed.find(scene) == s_chunkBoundsRefreshed.end())
		{
			RecalculateAllChunkBounds(scene);
			s_chunkBoundsRefreshed.insert(scene);
		}

		if(params.isShadow == false)
			_chunksVisible = 0;

		auto chunkData = _sceneToChunkMap.find(scene);

		if (chunkData == _sceneToChunkMap.end())
			return;

		for (int32_t i = 0; i < chunkData->second._numChunks; ++i)
		{
			for (int32_t j = 0; j < chunkData->second._numChunks; ++j)
			{
				const auto& chunk = chunkData->second._chunks[i][j];

				const auto& boundingVolume = chunk->GetBoundingVolume();				

				if (pvs->IsShapeVisible(boundingVolume, params))
				{
					// if the chunk was cached, read it back in from disk
					if (_cachingEnabled && params.isShadow == false && chunk->IsCached() == true)
					{
						_loaderLock.lock();

						if(std::find(_loadList.begin(), _loadList.end(), chunk) == _loadList.end())
							_loadList.push_front(chunk);

						_loaderLock.unlock();

						continue;
					}
					chunk->Lock();
					const auto& meshes = chunk->GetChunkChildrenMeshes();
					components.insert(components.end(), meshes.begin(), meshes.end());
					chunk->Unlock();

					if (params.isShadow == false)
						_chunksVisible++;
				}
				else if(_cachingEnabled && params.isShadow == false && chunk->IsCached() == false)
				{
					_loaderLock.lock();

					if (std::find(_unloadList.begin(), _unloadList.end(), chunk) == _unloadList.end())
						_unloadList.push_back(chunk);

					_loaderLock.unlock();
				}
			}
		}
	}

	void ChunkManager::ChunkLoader()
	{
		//while (g_pEnv->IsRunning())
		{
			_loaderLock.lock();

			if (_unloadList.size() > 0)
			{
				auto& head = _unloadList.front();
				if (g_pEnv->_sceneManager->GetCurrentScene()->TryLock() == true)
				{
					head->WriteToDisk();
					_unloadList.pop_front();

					g_pEnv->_sceneManager->GetCurrentScene()->Unlock();
				}
			}

			if (_loadList.size() > 0)
			{
				auto& head = _loadList.front();
				if (g_pEnv->_sceneManager->GetCurrentScene()->TryLock() == true)
				{
					head->ReadFromDisk();
					_loadList.pop_front();

					g_pEnv->_sceneManager->GetCurrentScene()->Unlock();
				}
			}

			_loaderLock.unlock();

			//std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
	}

}
