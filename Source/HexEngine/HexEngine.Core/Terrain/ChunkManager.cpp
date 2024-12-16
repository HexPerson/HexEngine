

#include "ChunkManager.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../Math/FloatMath.hpp"
#include "../Scene/SceneManager.hpp"
#include "../Entity/Component/Transform.hpp"
#include "../Entity/Component/StaticMeshComponent.hpp"
#include "../Environment/LogFile.hpp"
#include "../Input/HVar.hpp"

namespace HexEngine
{
	extern HVar r_lodPartition;
	extern HVar r_shadowMinimumLodThreshold;

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

		int32_t halfGridSize = numChunks / 2;

		data._chunks = new Chunk ** [numChunks];

		for (int32_t i = 0; i < numChunks; ++i)
		{
			data._chunks[i] = new HexEngine::Chunk*[numChunks];

			for (int32_t j = 0; j < numChunks; ++j)
			{
				dx::BoundingBox initialVolume;
				initialVolume.Extents = math::Vector3(chunkSize, 1.0f, chunkSize);
				initialVolume.Center = math::Vector3((float)(i - halfGridSize) * data._chunkWidth, 0.0f, (float)(j - halfGridSize) * data._chunkWidth);

				Chunk* chunk = new Chunk(initialVolume);	

				data._chunks[i][j] = chunk;
			}
		}

		g_pEnv->_sceneManager->GetCurrentScene()->AddEntityListener(this);		

		_sceneToChunkMap[scene] = data;

		// because the scene may already have entities in it that the chunk system might not be aware of, we should add them all back in

		Scene::EntityComponentVector components;
		scene->GetComponents(1 << StaticMeshComponent::_GetComponentId(), components);

		for (auto& comp : components)
		{
			OnAddComponent(comp->GetEntity(), comp);
		}
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
		
	}

	void ChunkManager::OnRemoveEntity(Entity* entity)
	{
		auto chunkData = _sceneToChunkMap.find(entity->GetScene());

		if (chunkData == _sceneToChunkMap.end())
			return;

		auto chunk = GetChunkByPosition(chunkData->second, entity->GetPosition());

		if (chunk == nullptr)
			return;

		LOG_DEBUG("Removing entity %p from chunk %p", entity, chunk);

		chunk->RemoveChunkChild(entity);
	}

	void ChunkManager::OnAddComponent(Entity* entity, BaseComponent* component)
	{
		if (component->GetComponentId() != StaticMeshComponent::_GetComponentId())
			return;

		auto chunkData = _sceneToChunkMap.find(entity->GetScene());

		if (chunkData == _sceneToChunkMap.end())
			return;

		auto chunk = GetChunkByPosition(chunkData->second, entity->GetPosition());

		if (chunk == nullptr)
			return;

		chunk->AddChunkChild(entity);
	}

	void ChunkManager::OnRemoveComponent(Entity* entity, BaseComponent* component)
	{

	}

	void ChunkManager::OnEntityPositionChanged(Entity* entity, const math::Vector3& oldPosition, const math::Vector3& newPosition)
	{
		auto chunkData = _sceneToChunkMap.find(entity->GetScene());

		if (chunkData == _sceneToChunkMap.end())
			return;

		auto oldChunk = GetChunkByPosition(chunkData->second, oldPosition);
		auto newChunk = GetChunkByPosition(chunkData->second, newPosition);

		// we crossed the boundary between chunks, switch us to the new chunk
		if (oldChunk != newChunk)
		{
			LOG_DEBUG("Entity '%s' [%p] is being moved from chunk %p to chunk %p", entity->GetName().c_str(), entity, oldChunk, newChunk);

			if(oldChunk)
				oldChunk->RemoveChunkChild(entity);

			if(newChunk)
				newChunk->AddChunkChild(entity);
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

	void ChunkManager::CalculatePVS(Scene* scene, PVS* pvs, const PVSParams& params, Scene::EntityComponentVector& components)
	{
		auto chunkData = _sceneToChunkMap.find(scene);

		if (chunkData == _sceneToChunkMap.end())
			return;

		for (int32_t i = 0; i < chunkData->second._numChunks; ++i)
		{
			for (int32_t j = 0; j < chunkData->second._numChunks; ++j)
			{
				const auto& boundingVolume = chunkData->second._chunks[i][j]->GetBoundingSphere();

				if (pvs->IsShapeVisible(boundingVolume, params))
				{
					const auto& entities = chunkData->second._chunks[i][j]->GetChunkChildren();

					for (auto& ent : entities)
					{
						if (auto meshRenderer = ent->GetCachedMeshRenderer(); meshRenderer != nullptr)
						{
							components.push_back(meshRenderer);
						}
					}
				}
			}
		}
	}

#if 0
	void ChunkManager::CalculateVisibility(Scene* scene, Camera* camera, PVS::MeshInstanceMap& map)
	{
		if (!camera)
			return;

		auto chunkData = _sceneToChunkMap.find(scene);

		if (chunkData == _sceneToChunkMap.end())
			return;

		//visMap.clear();

		auto cameraTransform = camera->GetEntity()->GetComponent<Transform>();
		const auto& cameraPos = cameraTransform->GetPosition();
		const auto& frustum = camera->GetFrustum();

		for (int32_t i = 0; i < chunkData->second._numChunks; ++i)
		{
			for (int32_t j = 0; j < chunkData->second._numChunks; ++j)
			{
				const auto& boundingVolume = chunkData->second._chunks[i][j]->GetBoundingSphere();

				if (frustum.Intersects(boundingVolume))
				{
					//if (IsChunkOccluded(cameraPos, _chunks[i][j]))
					//	continue;

					

					MeshInstance* lastMeshInstance = nullptr;
					auto it = map.end();
					const auto& visMapEnd = map.end();

					for (auto& entity : entities)
					{
						if (entity->IsPendingDeletion())
							continue;

						if (entity->GetLayer() == Layer::Invisible || entity->GetLayer() == Layer::Trigger)
							continue;

						auto meshComponent = entity->GetCachedMeshRenderer();

						if (meshComponent)
						{
							if (frustum.Intersects(entity->GetWorldAABB()) == false)
								continue;

							float distance = (entity->GetPosition() - g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera()->GetEntity()->GetPosition()).Length();

							for (auto& mesh : meshComponent->GetMeshes())
							{
								if (!mesh)
									continue;

								if (mesh->GetLodLevel() != -1)
								{
									//if (mesh->GetLodLevel() != 1)
									//	continue;

									const float lodPartitions = r_lodPartition._val.f32;

									float minDistance = lodPartitions * (float)(mesh->GetLodLevel());
									float maxDistance = lodPartitions * (float)(mesh->GetLodLevel() + 1);

									float distance = (entity->GetPosition() - camera->GetEntity()->GetPosition()).Length();

									if (mesh->GetLodLevel() < 3)
									{
										// always allow level 3
										if (distance < minDistance || distance > maxDistance)
										{
											//LOG_DEBUG("Entity %p failed LOD test at level %d because distance %.1f is greated then max allowed (%.1f) for this level", entity, mesh->GetLodLevel(), distance, maxDistance);
											continue;
										}
									}
									else
									{
										if (distance < minDistance)
										{
											continue;
										}
									}
								}

								auto meshInstance = mesh->GetInstance();

								if (it == visMapEnd)
								{
									RenderState rs;

									auto& newEntry = map[meshInstance];

									newEntry.reserve(256);
									newEntry.push_back({ mesh,entity });

									map[meshInstance].push_back({ mesh,entity });
								}
								else
								{
									bool found = false;
									for (auto&& pair : it->second)
									{
										if (pair.second == entity)
										{
											found = true;
											break;
										}
									}
									if (!found)
										it->second.push_back({ mesh,entity });
								}
							}
						}
					}
				}
			}
		}
	}

	void ChunkManager::CalculateShadowVisibility(const dx::BoundingSphere& cascadeSphere, std::unordered_map<MeshInstance*, std::vector<std::pair<Mesh*, Entity*>>>& visMap)
	{
		for (int32_t i = 0; i < _numChunks; ++i)
		{
			for (int32_t j = 0; j < _numChunks; ++j)
			{
				const auto& boundingVolume = _chunks[i][j]->GetBoundingSphere();

				if (cascadeSphere.Intersects(boundingVolume))
				{
					const auto& entities = _chunks[i][j]->GetChunkChildren();

					//MeshInstance* lastMeshInstance = nullptr;

					auto it = visMap.end();
					const auto& visMapEnd = visMap.end();

					for (auto& entity : entities)
					{
						if (entity->IsPendingDeletion())
							continue;

						if (entity->GetCastsShadows() == false)
							continue;

						if (entity->GetLayer() == Layer::Invisible || entity->GetLayer() == Layer::Trigger)
							continue;

						auto meshComponent = entity->GetCachedMeshRenderer();

						if (meshComponent)
						{
							bool visible = cascadeSphere.Intersects(entity->GetWorldAABB());

							if (!visible)
								continue;

							float distance = (entity->GetPosition() - g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera()->GetEntity()->GetPosition()).Length();

							for (auto& mesh : meshComponent->GetMeshes())
							{
								if (!mesh)
									continue;

								if (auto lod = mesh->GetLodLevel(); lod != -1)
								{
									if (lod < r_shadowMinimumLodThreshold._val.i32)
										continue;

									//if (mesh->GetLodLevel() != 1)
									//	continue;

									const float lodPartitions = r_lodPartition._val.f32;

									float minDistance = lodPartitions * (float)(mesh->GetLodLevel());
									float maxDistance = lodPartitions * (float)(mesh->GetLodLevel() + 1);									

									if (mesh->GetLodLevel() < 3)
									{
										// always allow level 3
										if (distance < minDistance || distance > maxDistance)
										{
											//LOG_DEBUG("Entity %p failed LOD test at level %d because distance %.1f is greated then max allowed (%.1f) for this level", entity, mesh->GetLodLevel(), distance, maxDistance);
											continue;
										}
									}
									else
									{
										if (distance < minDistance)
										{
											continue;
										}
									}
								}

								auto meshInstance = mesh->GetInstance();

								//auto& vec = visMap[meshInstance];

								//if (vec.capacity() == 0)
								//	vec.resize(_visMapSizeIncrease);

#if 0
								int lastNullIdx = -1;

								while (true)
								{
									const auto vecSize = vec.size();
									for (int x = 0; x < vecSize; ++x)
									{
										if (vec[x].first == nullptr)
										{
											lastNullIdx = x;
											break;
										}
									}
									/*const auto& end = vec.end();
									for (auto it = vec.begin(); it != end; it++)
									{
										if (it->first == nullptr)
										{
											lastNullIdx = std::distance(vec.begin(), it);
											break;
										}
									}*/

									// we ran out of space
									if (lastNullIdx == -1)
										vec.resize(vec.size() + _visMapSizeIncrease);
									else
										break;
								}
									
								vec[lastNullIdx] = std::make_pair(mesh, entity);
#endif
									//vec.emplace_back(mesh, entity);
								//vec[vec.size()] = std::make_pair(mesh,entity);

								//if (meshInstance != lastMeshInstance)
								{
									it = visMap.find(meshInstance);
									//lastMeshInstance = meshInstance;
								}

								if (it == visMapEnd)
								{
									RenderState rs;

									auto& newEntry = visMap[meshInstance];

									newEntry.reserve(256);
									newEntry.push_back({ mesh,entity });

									visMap[meshInstance].push_back({ mesh,entity });
								}
								else
								{
									bool found = false;
									for (auto&& pair : it->second)
									{
										if (pair.second == entity)
										{
											found = true;
											break;
										}
									}
									if (!found)
									it->second.push_back({ mesh,entity });
								}
							}
						}
					}
				}
			}
		}
	}
#endif

}