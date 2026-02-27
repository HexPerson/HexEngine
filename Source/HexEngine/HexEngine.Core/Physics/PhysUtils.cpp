
#include "PhysUtils.hpp"
#include "../Scene/SceneManager.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../Terrain/ChunkManager.hpp"
#include "../Entity/Component/RigidBody.hpp"

namespace HexEngine
{
	namespace PhysUtils
	{
		bool HEX_API RayCast(const math::Vector3& from, const math::Vector3& to, LayerMask mask, RayHit* hitInfo, const std::vector<Entity*>& entsToIgnore)
		{
			math::Vector3 direction = (to - from);
			float maxDistance = direction.Length();
			direction.Normalize();

			// sanity checks
			if (to == from || maxDistance <= FLT_EPSILON)
				return false;

			math::Ray ray;
			ray.position = from;
			ray.direction = direction;

			return RayCast(ray, maxDistance, mask, hitInfo, entsToIgnore);
		}

		bool HEX_API RayCast(const math::Ray& ray, float maxDistance, LayerMask mask, RayHit* hitInfo, const std::vector<Entity*>& entsToIgnore)
		{
			g_pEnv->_sceneManager->GetCurrentScene()->Lock();

			//PROFILE();

			std::vector<RayHit> hits;

			Entity* pickedEnt = nullptr;

#if 0
			if (g_pEnv->_chunkManager->HasActiveChunks())
			{
				auto chunk = g_pEnv->_chunkManager->GetChunkByPosition(ray.position);

				if (chunk)
				{
					for (auto& entity : chunk->GetChunkChildren())
					{
						if ((mask & (1 << (uint32_t)entity->GetLayer())) == 0)
							continue;

						if (entity->IsPendingDeletion())
							continue;

						const auto& entityPosition = entity->GetComponent<Transform>()->GetPosition();

						const auto& worldBB = entity->GetWorldAABB();

						float dist = maxDistance;
						float originalDist = dist;

						if (ray.Intersects(worldBB, dist) && dist != 0.0f)
							//if (traceRay.Intersects(worldBB, dist) && dist > 0.0f)
						{
							if (maxDistance != 0.0f)
							{
								if (dist > maxDistance)
									continue;
							}

							// use new physx raycast instead of manually performing the trace

							if (auto rigidBody = entity->GetComponent<RigidBody>(); rigidBody != nullptr)
							{
								RayHit tempHit;
								if (g_pEnv->_physicsSystem->RayCast(ray.position, ray.direction, originalDist, rigidBody->GetIRigidBody(), &tempHit) > 0)
								{
									hits.push_back(tempHit);
								}
							}
							else
							{
								// if it has a mesh renderer, perform a ray triangle intersection on the mesh to get correct hitpos
								if (auto meshRenderer = entity->GetComponent<MeshRenderer>(); meshRenderer != nullptr)
								{
									for (auto& mesh : meshRenderer->GetMeshes())
									{
										auto& vertices = mesh->GetTransformedVertices();
										auto& indices = mesh->GetIndices();

										for (int i = 0; i < mesh->GetNumFaces(); ++i)
										{
											uint32_t i0 = indices[i * 3 + 0];
											uint32_t i1 = indices[i * 3 + 1];
											uint32_t i2 = indices[i * 3 + 2];

											math::Vector4 v0 = vertices[i0]._position + entityPosition;
											math::Vector4 v1 = vertices[i1]._position + entityPosition;
											math::Vector4 v2 = vertices[i2]._position + entityPosition;

											if (ray.Intersects(math::Vector3(v0), math::Vector3(v1), math::Vector3(v2), dist))
											{
												if (maxDistance != 0.0f)
												{
													if (dist > maxDistance)
														continue;
												}

												RayHit tempHit;
												tempHit.start = ray.position;
												tempHit.distance = dist;
												tempHit.position = ray.position + ray.direction * dist;
												tempHit.entity = entity;
												tempHit.normal = (vertices[i0]._normal + vertices[i1]._normal + vertices[i2]._normal) / 3.0f;
												tempHit.normal.Normalize();

												hits.push_back(tempHit);
												break;
											}
										}
									}
								}
								else
								{
									RayHit tempHit;
									tempHit.start = ray.position;
									tempHit.distance = dist;
									tempHit.position = ray.position + ray.direction * dist;
									tempHit.entity = entity;
									tempHit.normal = math::Vector3::Up;

									hits.push_back(tempHit);
								}
							}
						}
					}
				}
			}
			else
#endif
			{
				// use the list of renderables, because we know they have already been tested for visibility
				//
				std::vector<StaticMeshComponent*> entities;
				g_pEnv->_sceneManager->GetCurrentScene()->GetComponents<StaticMeshComponent>(entities);

				for (auto&& component : entities)
				{
					auto entity = component->GetEntity();

					if ((mask & (1 << (uint32_t)entity->GetLayer())) == 0)
						continue;

					if (entity->HasFlag(EntityFlags::DoNotRender))
						continue;

					bool skipThisEnt = false;

					for (auto&& ignore : entsToIgnore)
					{
						if (entity == ignore)
						{
							skipThisEnt = true;
							break;
						}
					}

					if (skipThisEnt)
						continue;

					auto entityPosition = entity->GetComponent<Transform>()->GetPosition();

					const auto& worldMatrix = entity->GetWorldTM();

					const auto& worldBB = entity->GetWorldAABB();

					float dist = maxDistance == 0.0f ? 1024.0f : maxDistance;
					float originalDist = dist;

					if (ray.Intersects(worldBB, dist) && dist != 0.0f)
						//if (traceRay.Intersects(worldBB, dist) && dist > 0.0f)
					{
						if (maxDistance != 0.0f)
						{
							if (dist > maxDistance)
								continue;
						}

						// use new physx raycast instead of manually performing the trace

						if (auto rigidBody = entity->GetComponent<RigidBody>(); rigidBody != nullptr)
						{
							RayHit tempHit;
							if (g_pEnv->_physicsSystem->RayCast(ray.position, ray.direction, originalDist, rigidBody->GetIRigidBody(), &tempHit) > 0)
							{
								tempHit.material = ((StaticMeshComponent*)component)->GetMaterial();

								hits.push_back(tempHit);
							}
						}
						else
						{

							// if it has a mesh renderer, perform a ray triangle intersection on the mesh to get correct hitpos
							if (auto meshRenderer = (StaticMeshComponent*)component; meshRenderer != nullptr)
							{
								auto mesh = meshRenderer->GetMesh();
								if(mesh)
								{
									if (mesh->HasAnimations())
										continue;

									auto& vertices = mesh->GetVertices();
									auto& indices = mesh->GetIndices();

									if (indices.size() < mesh->GetNumFaces() * 3)
										continue;

									for (uint32_t i = 0; i < mesh->GetNumFaces(); ++i)
									{
										uint32_t i0 = indices[i * 3 + 0];
										uint32_t i1 = indices[i * 3 + 1];
										uint32_t i2 = indices[i * 3 + 2];

										math::Vector4 v0 = vertices[i0]._position; //+entityPosition;
										math::Vector4 v1 = vertices[i1]._position; //+entityPosition;
										math::Vector4 v2 = vertices[i2]._position; //+entityPosition;

										v0 = math::Vector4::Transform(v0, worldMatrix);
										v1 = math::Vector4::Transform(v1, worldMatrix);
										v2 = math::Vector4::Transform(v2, worldMatrix);

										if (ray.Intersects(math::Vector3(v0), math::Vector3(v1), math::Vector3(v2), dist))
										{
											RayHit tempHit;
											tempHit.start = ray.position;
											tempHit.distance = dist;
											tempHit.position = ray.position + ray.direction * dist;
											tempHit.entity = entity;
											tempHit.normal = (vertices[i0]._normal + vertices[i1]._normal + vertices[i2]._normal) / 3.0f;
											tempHit.normal.Normalize();
											tempHit.material = meshRenderer->GetMaterial();

											hits.push_back(tempHit);
											break;
										}
									}
								}
							}
							else
							{
								RayHit tempHit;
								tempHit.start = ray.position;
								tempHit.distance = dist;
								tempHit.position = ray.position + ray.direction * dist;
								tempHit.entity = entity;
								tempHit.normal = -ray.direction;

								hits.push_back(tempHit);
							}
						}
					}
				}
			}

			std::sort(hits.begin(), hits.end(),
				[](const RayHit& lhs, const RayHit& rhs) {
					return fabs(lhs.distance) < fabs(rhs.distance);
				});

			if (hits.size() > 0)
			{
				*hitInfo = hits.at(0);
				return true;
			}

			return false;
		}


#if 0
		bool Scene::CameraPickEntity(const math::Vector3& ray, RayHit& hit, uint32_t layerMask)
		{
			std::lock_guard<std::recursive_mutex> lock(_lock);

			PROFILE();

			if (auto camera = GetMainCamera(); camera != nullptr)
			{
				auto transform = camera->GetEntity()->GetComponent<Transform>();

				auto startPos = transform->GetPosition();

				//auto end			

				std::vector<RayHit> pickedEntities;

				Entity* pickedEnt = nullptr;

				// use the list of renderables, because we know they have already been tested for visibility
				//
				for (auto&& renderable : _renderables[6])
				{
					for (auto&& pair : renderable.second)
					{
						auto entity = pair.second;

						if (entity->IsPendingDeletion())
							continue;

						if (camera->GetEntity() == entity)
							continue;

						if ((layerMask & (1 << (uint32_t)entity->GetLayer())) == 0)
							continue;

						auto entityPosition = entity->GetComponent<Transform>()->GetPosition();

						auto worldBB = entity->GetWorldAABB();

						if (camera->IsVisibleInFrustum(worldBB))
						{
							math::Ray traceRay(startPos, ray);

							float dist = 1024.0f;

							if (worldBB.Intersects(startPos, ray, dist) && dist != 0.0f)
								//if (traceRay.Intersects(worldBB, dist) && dist > 0.0f)
							{
								// if it has a mesh renderer, perform a ray triangle intersection on the mesh to get correct hitpos
								if (auto meshRenderer = entity->GetComponent<MeshRenderer>(); meshRenderer != nullptr)
								{
									math::Ray meshRay;
									meshRay.direction = ray;
									meshRay.position = startPos;

									for (auto& mesh : meshRenderer->GetMeshes())
									{
										auto& vertices = mesh->GetVertices();
										auto& indices = mesh->GetIndices();

										for (int i = 0; i < mesh->GetNumFaces(); ++i)
										{
											uint32_t i0 = indices[i * 3 + 0];
											uint32_t i1 = indices[i * 3 + 1];
											uint32_t i2 = indices[i * 3 + 2];

											math::Vector4 v0 = vertices[i0]._position + entityPosition;
											math::Vector4 v1 = vertices[i1]._position + entityPosition;
											math::Vector4 v2 = vertices[i2]._position + entityPosition;

											if (meshRay.Intersects(math::Vector3(v0), math::Vector3(v1), math::Vector3(v2), dist))
											{
												RayHit tempHit;
												tempHit.start = startPos;
												tempHit.distance = dist;
												tempHit.position = startPos + ray * dist;
												tempHit.entity = entity;
												tempHit.normal = (vertices[i0]._normal + vertices[i1]._normal + vertices[i2]._normal) / 3.0f;
												tempHit.normal.Normalize();

												pickedEntities.push_back(tempHit);
												break;
											}
										}
									}
								}
								else
								{
									RayHit tempHit;
									tempHit.start = startPos;
									tempHit.distance = dist;
									tempHit.position = startPos + ray * dist;
									tempHit.entity = entity;
									tempHit.normal = math::Vector3::Up;

									pickedEntities.push_back(tempHit);
								}
							}
						}
					}
				}

				std::sort(pickedEntities.begin(), pickedEntities.end(),
					[](const RayHit& lhs, const RayHit& rhs) {
						return fabs(lhs.distance) < fabs(rhs.distance);
					});

				if (pickedEntities.size() > 0)
				{
					hit = pickedEntities.at(0);

					_hasLastHit = true;
					_lastHit = hit;

					return true;
				}
			}

			return false;
		}

		bool Scene::PickEntity(const math::Vector3& from, const math::Vector3& ray, RayHit& hit, uint32_t layerMask, float maxDistance /*= 0.0f*/)
		{
			std::lock_guard<std::recursive_mutex> lock(_lock);

			PROFILE();

			std::vector<RayHit> pickedEntities;

			Entity* pickedEnt = nullptr;

			if (g_pEnv->_chunkManager->HasActiveChunks())
			{
				auto chunk = g_pEnv->_chunkManager->GetChunkByPosition(from);

				if (chunk)
				{
					for (auto& entity : chunk->GetChunkChildren())
					{
						if ((layerMask & (1 << (uint32_t)entity->GetLayer())) == 0)
							continue;

						if (entity->IsPendingDeletion())
							continue;

						const auto& entityPosition = entity->GetComponent<Transform>()->GetPosition();

						const auto& worldBB = entity->GetWorldAABB();

						float dist = maxDistance == 0.0f ? 1024.0f : maxDistance;
						float originalDist = dist;

						if (worldBB.Intersects(from, ray, dist) && dist != 0.0f)
							//if (traceRay.Intersects(worldBB, dist) && dist > 0.0f)
						{
							if (maxDistance != 0.0f)
							{
								if (dist > maxDistance)
									continue;
							}

#if 1
							// use new physx raycast instead of manually performing the trace

							if (auto rigidBody = entity->GetComponent<RigidBody>(); rigidBody != nullptr)
							{
								RayHit tempHit;
								if (g_pEnv->_physicsSystem->RayCast(from, ray, originalDist, rigidBody->GetIRigidBody(), &tempHit) > 0)
								{
									pickedEntities.push_back(tempHit);
								}
							}


#else
							// if it has a mesh renderer, perform a ray triangle intersection on the mesh to get correct hitpos
							if (auto meshRenderer = entity->GetComponent<MeshRenderer>(); meshRenderer != nullptr)
							{
								math::Ray meshRay;
								meshRay.direction = ray;
								meshRay.position = from;

								for (auto& mesh : meshRenderer->GetMeshes())
								{
									auto& vertices = mesh->GetTransformedVertices();
									auto& indices = mesh->GetIndices();

									for (int i = 0; i < mesh->GetNumFaces(); ++i)
									{
										uint32_t i0 = indices[i * 3 + 0];
										uint32_t i1 = indices[i * 3 + 1];
										uint32_t i2 = indices[i * 3 + 2];

										math::Vector4 v0 = vertices[i0]._position + entityPosition;
										math::Vector4 v1 = vertices[i1]._position + entityPosition;
										math::Vector4 v2 = vertices[i2]._position + entityPosition;

										// calculate dot
										auto dot = ray.Dot(math::Vector3(vertices[i0]._normal));

										//if (dot > 0.0f)
										//	continue;

										if (meshRay.Intersects(math::Vector3(v0), math::Vector3(v1), math::Vector3(v2), dist))
										{
											if (maxDistance != 0.0f)
											{
												if (dist > maxDistance)
													continue;
											}

											RayHit tempHit;
											tempHit.start = from;
											tempHit.distance = dist;
											tempHit.position = from + ray * dist;
											tempHit.entity = entity;
											tempHit.normal = (vertices[i0]._normal + vertices[i1]._normal + vertices[i2]._normal) / 3.0f;
											tempHit.normal.Normalize();

											pickedEntities.push_back(tempHit);
											break;
										}
									}
								}
							}
#endif
							else
							{
								RayHit tempHit;
								tempHit.start = from;
								tempHit.distance = dist;
								tempHit.position = from + ray * dist;
								tempHit.entity = entity;
								tempHit.normal = math::Vector3::Up;

								pickedEntities.push_back(tempHit);
							}
						}
					}
				}
			}
			else
			{
				// use the list of renderables, because we know they have already been tested for visibility
				//
				EntityComponentVector entities;
				GetComponents((ComponentSignature)(1 << MeshRenderer::_GetComponentId()), entities);

				for (auto&& component : entities)
				{
					auto entity = component->GetEntity();

					if ((layerMask & (1 << (uint32_t)entity->GetLayer())) == 0)
						continue;

					auto entityPosition = entity->GetComponent<Transform>()->GetPosition();

					const auto& worldMatrix = entity->GetWorldTM();

					const auto& worldBB = entity->GetWorldAABB();

					float dist = maxDistance == 0.0f ? 1024.0f : maxDistance;
					float originalDist = dist;

					if (worldBB.Intersects(from, ray, dist) && dist != 0.0f)
						//if (traceRay.Intersects(worldBB, dist) && dist > 0.0f)
					{
						if (maxDistance != 0.0f)
						{
							if (dist > maxDistance)
								continue;
						}

#if 1
						// use new physx raycast instead of manually performing the trace

						if (auto rigidBody = entity->GetComponent<RigidBody>(); rigidBody != nullptr)
						{
							RayHit tempHit;
							if (g_pEnv->_physicsSystem->RayCast(from, ray, originalDist, rigidBody->GetIRigidBody(), &tempHit) > 0)
							{
								pickedEntities.push_back(tempHit);
							}
						}


#else

						// if it has a mesh renderer, perform a ray triangle intersection on the mesh to get correct hitpos
						if (auto meshRenderer = (MeshRenderer*)component; meshRenderer != nullptr)
						{
							math::Ray meshRay;
							meshRay.direction = ray;
							meshRay.position = from;

							for (auto& mesh : meshRenderer->GetMeshes())
							{
								auto& vertices = mesh->GetVertices();
								auto& indices = mesh->GetIndices();

								if (indices.size() < mesh->GetNumFaces() * 3)
									continue;

								for (int i = 0; i < mesh->GetNumFaces(); ++i)
								{
									uint32_t i0 = indices[i * 3 + 0];
									uint32_t i1 = indices[i * 3 + 1];
									uint32_t i2 = indices[i * 3 + 2];

									math::Vector4 v0 = vertices[i0]._position; //+entityPosition;
									math::Vector4 v1 = vertices[i1]._position; //+entityPosition;
									math::Vector4 v2 = vertices[i2]._position; //+entityPosition;

									v0 = math::Vector4::Transform(v0, worldMatrix);
									v1 = math::Vector4::Transform(v1, worldMatrix);
									v2 = math::Vector4::Transform(v2, worldMatrix);

									if (meshRay.Intersects(math::Vector3(v0), math::Vector3(v1), math::Vector3(v2), dist))
									{
										RayHit tempHit;
										tempHit.start = from;
										tempHit.distance = dist;
										tempHit.position = from + ray * dist;
										tempHit.entity = entity;
										tempHit.normal = (vertices[i0]._normal + vertices[i1]._normal + vertices[i2]._normal) / 3.0f;
										tempHit.normal.Normalize();

										pickedEntities.push_back(tempHit);
										break;
									}
								}
							}
						}
						else
						{
							RayHit tempHit;
							tempHit.start = from;
							tempHit.distance = dist;
							tempHit.position = from + ray * dist;
							tempHit.entity = entity;
							tempHit.normal = math::Vector3::Up;

							pickedEntities.push_back(tempHit);
						}
#endif
					}
				}
			}

			std::sort(pickedEntities.begin(), pickedEntities.end(),
				[](const RayHit& lhs, const RayHit& rhs) {
					return fabs(lhs.distance) < fabs(rhs.distance);
				});

			if (pickedEntities.size() > 0)
			{
				hit = pickedEntities.at(0);

				_hasLastHit = true;
				_lastHit = hit;

				return true;
			}

			return false;
		}

		Entity* Scene::RayCast(const math::Ray& ray, float traceLength, math::Vector3& worldEndPos, uint32_t signature, Entity* ignore)
		{
			std::lock_guard<std::recursive_mutex> lock(_lock);

			PROFILE();

			auto startPos = ray.position;

			std::vector<std::pair<Entity*, float>> pickedEntities;

			Entity* pickedEnt = nullptr;

			// Gather the chunks that this trace falls within
			std::vector<Chunk*> traceChunks;

			/*float chunkTraceDistRemaining = traceLength;
			float traceCurrent = 0.0f;

			while (chunkTraceDistRemaining > 0.0f)
			{
				auto chunk = g_pEnv->_chunkManager->GetChunkByPosition(ray.position + ray.direction * traceCurrent);

				if (chunk)
					traceChunks.push_back(chunk);
				else
					break;

				float stepSize = chunkTraceDistRemaining;

				if (stepSize > g_pEnv->_chunkManager->_chunkWidth)
					stepSize = g_pEnv->_chunkManager->_chunkWidth;

				traceCurrent += stepSize;
				chunkTraceDistRemaining -= stepSize;
			}*/

			//for (auto& chunk : traceChunks)

			EntityComponentVector components;

			if (GetComponents(signature, components) == false)
				return nullptr;

			for (auto&& component : components)
			{
				Entity* entity = component->GetEntity();

				if (entity == ignore)
					continue;

				if ((entity->GetPosition() - ray.position).Length() > traceLength + entity->GetBoundingSphere().Radius)
					continue;

				float dist = traceLength;

				if (entity->GetWorldOBB().Intersects(ray.position, ray.direction, dist) && dist > 0.0f && dist <= traceLength)
					//if (ray.Intersects(entity->GetWorldOBB()., dist) && dist > 0.0f && dist <= traceLength)
				{
					pickedEntities.push_back({ entity, dist });
				}
			}

			std::sort(pickedEntities.begin(), pickedEntities.end(),
				[](const std::pair<Entity*, float>& lhs, const std::pair<Entity*, float>& rhs) {
					return lhs.second < rhs.second;
				});

			if (pickedEntities.size() > 0)
			{
				worldEndPos = startPos + ray.direction * pickedEntities.at(0).second;
				return pickedEntities.at(0).first;
			}

			return nullptr;
		}
	}
#endif
	}
}