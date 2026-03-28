

#include "Scene.hpp"
#include "../HexEngine.hpp"

#include "../Entity/Component/Transform.hpp"
#include "../Entity/Component/StaticMeshComponent.hpp"
#include "../Entity/Component/PointLight.hpp"
#include "../Entity/Component/FirstPersonCameraController.hpp"
#include "../Entity/Component/InstancedStaticMeshComponent.hpp"
#include "PVS.hpp"

namespace HexEngine
{

	extern HVar r_debugScene;
	extern HVar r_interpolate;
	extern HVar r_lodPartition;

	HVar r_profileDisableShadowSampling("r_profileDisableShadowSampling", "Disable shadow-map sampling in static mesh materials for profiling", false, false, true);
	HVar r_profileDisableNormalMaps("r_profileDisableNormalMaps", "Disable normal map bindings in static mesh materials for profiling", false, false, true);
	HVar r_profileDisableSurfaceMaps("r_profileDisableSurfaceMaps", "Disable roughness, metallic, AO, height, emission and opacity map bindings in static mesh materials for profiling", false, false, true);
	HVar phys_debug("phys_debug", "Enable the physics debugger (very slow)", false, false, true);
	HVar r_debugRenderSkips("r_debugRenderSkips", "Log per-pass render skip counters for scene entity rendering", false, false, true);

	void Scene::Create(bool createSkySphere, IEntityListener* listener)
	{
		if (listener)
			AddEntityListener(listener);

#if 1//def _DEBUG
		g_pEnv->_debugGui->AddCallback(this);
#endif

		// Create the main camera
		//
		auto cameraEntity = CreateEntity("MainCamera");
		cameraEntity->SetLayer(Layer::Camera);
		
		_mainCamera = cameraEntity->AddComponent<Camera>();

		// Add an environment light (sun)
		//
		CreateDefaultSunLight();

		SetFogColour(math::Color(HEX_RGB_TO_FLOAT3(95, 241, 242)));

		_ambientLight = math::Vector4(0.14f, 0.14f, 0.145f, 1.0f);

		if (createSkySphere)
		{
			if (auto skyEnt = GetEntityByName("SkySphere"); skyEnt != nullptr)
			{
				_skySphere = skyEnt;
			}
			else
			{
				_skySphere = CreateEntity("SkySphere", math::Vector3::Zero, math::Quaternion::Identity, math::Vector3(2.0f));
				_skySphere->SetLayer(Layer::Sky);
				_skySphere->SetFlag(EntityFlags::ExcludeFromHLOD);
				auto sphereMesh = Mesh::Create("EngineData.Models/Primitives/sphere.hmesh");

				auto skyRenderer = _skySphere->AddComponent<StaticMeshComponent>();

				skyRenderer->SetMesh(sphereMesh);

				//auto material = Material::Create("Materials/SkySphere.hmat"); skyRenderer->GetMesh(0)->GetMaterial();

				//material->SetCullMode(CullingMode::FrontFace);
				//material->SetDepthState(DepthBufferState::DepthNone);
				skyRenderer->SetMaterial(Material::Create("EngineData.Materials/SkySphere.hmat"));
			}
		}
	}

	void Scene::CreateEmpty(bool createSkySphere, IEntityListener* listener)
	{
		if (listener)
			AddEntityListener(listener);

#if 1//def _DEBUG
		g_pEnv->_debugGui->AddCallback(this);
#endif

		SetFogColour(math::Color(HEX_RGB_TO_FLOAT3(95, 241, 242)));

		_ambientLight = math::Vector4(0.14f, 0.14f, 0.145f, 1.0f);

		if (createSkySphere)
		{
			if (auto skyEnt = GetEntityByName("SkySphere"); skyEnt != nullptr)
			{
				_skySphere = skyEnt;
			}
			else
			{
				_skySphere = CreateEntity("SkySphere", math::Vector3::Zero, math::Quaternion::Identity, math::Vector3(2.0f));
				_skySphere->SetLayer(Layer::Sky);
				_skySphere->SetFlag(EntityFlags::ExcludeFromHLOD);

				auto sphereMesh = Mesh::Create("EngineData.Models/Primitives/sphere.hmesh");

				auto skyRenderer = _skySphere->AddComponent<StaticMeshComponent>();

				skyRenderer->SetMesh(sphereMesh);
				skyRenderer->SetMaterial(Material::Create("EngineData.Materials/SkySphere.hmat"));
			}
		}
	}


	void Scene::CreateDefaultSunLight()
	{
		// look down ish
		//
		float pitch = -30.0f;

		auto rot = math::Quaternion::CreateFromYawPitchRoll(ToRadian(0.0f), ToRadian(pitch), 0.0f);

		auto lookDir = math::Vector3::Transform(math::Vector3::Forward, rot);
		lookDir.Normalize();

		auto lightPosCenter = math::Vector3(0, 0, 0);

		auto newPosition = lightPosCenter - (lookDir * 550.0f);

		auto sunEntity = CreateEntity("MainSun", newPosition, rot);
		_sunLight = sunEntity->AddComponent<DirectionalLight>();


	}

	void Scene::Clear()
	{
		_entities.clear();

		_pendingAdditions.clear();
		_pendingRemovals.clear();

		_mainCamera = nullptr;
	}

	void Scene::Destroy()
	{
		std::unique_lock lock(_lock);

		HandlePendingRemovals();

		// Safely delete all the entities
		//
		while (_entities.size() > 0)
		{
			bool breakout = false;
			for (auto it = _entities.begin(); it != _entities.end(); it++)
			{
				for (auto&& entity : it->second)
				{
					DestroyEntity(entity);
					breakout = true;
					break;

					//entity->Destroy();
					//delete entity;
				}

				if (breakout)
					break;
			}
		}

		// Clear the list of entities
		//
		_entities.clear();
		_components.clear();

		_pendingAdditions.clear();

		_sunLight = nullptr;

		g_pEnv->_debugGui->RemoveCallback(this);
	}

	Entity* Scene::CreateEntity(const std::string& name, const math::Vector3& position, const math::Quaternion& rotation, const math::Vector3& scale)
	{
		//std::unique_lock lock(_lock);

		PROFILE();

		std::string entityName = name;

		if (auto existingEnt = GetEntityByName(name); existingEnt != nullptr)
		{
			if (_namingPolicy == EntityNamingPolicy::AutoRename)
			{
				while (true)
				{
					if (auto p = entityName.find_last_not_of("0123456789"); p != entityName.npos)
					{
						int32_t num = p < entityName.length() - 1 ? std::stoi(entityName.substr(p + 1)) : 1;
						std::string nameWithoutNum = entityName.substr(0, p + 1);

						entityName = nameWithoutNum + std::to_string(num + 1);

						existingEnt = GetEntityByName(entityName);

						if (existingEnt == nullptr)
							break;
					}
				}
			}
			else
			{
				LOG_WARN("Cannot create an entity named '%s', an existing entity already exists with this name", name.c_str());
				return existingEnt;
			}
		}

		Entity* entity = new Entity(this);

		// Set the entity name
		//
		entity->SetName(entityName);

		// Create a transform component
		//
		entity->AddComponent<Transform>();
		entity->SetPosition(position);
		entity->SetRotation(rotation);
		entity->SetScale(scale);

		
		if (_insideEntityIteration)
		{
			_lock.lock();
			_pendingAdditions.insert(entity);
			_lock.unlock();
		}
		else
		{
			//LOG_DEBUG("Adding entity [%p] %s", entity, entity->GetName().c_str());

			AddEntityInternal(entity);
		}

		if (entity->IsCreated() == false)
			entity->Create();


		return entity;
	}

	Entity* Scene::CloneEntity(Entity* entity, const std::string& name, const math::Vector3& position, const math::Quaternion& rotation, const math::Vector3& scale, bool retainHierarchy)
	{
		std::unique_lock lock(_lock);

		Entity* clone = CreateEntity(name /*+ " (Clone)"*/, position, rotation, scale);

		clone->SetLayer(entity->GetLayer());
		if (entity->IsPrefabInstance())
		{
			clone->SetPrefabSource(entity->GetPrefabSourcePath(), entity->GetPrefabRootEntityName(), entity->IsPrefabInstanceRoot());
			clone->SetPrefabNodeId(entity->EnsurePrefabNodeId());
			clone->SetPrefabPropertyOverrides(entity->GetPrefabPropertyOverrides());
			clone->SetPrefabOverridePatches(entity->GetPrefabOverridePatches());
		}
		else
		{
			clone->ClearPrefabSource();
		}

		if (retainHierarchy && entity->GetParent())
			clone->SetParent(entity->GetParent());

		for (auto& comp : entity->GetAllComponents())
		{
			// already has a transform so skip
			if (comp->GetComponentId() == Transform::_GetComponentId())
				continue;

			auto cls = g_pEnv->_classRegistry->Find(comp->GetComponentName());

			if (!cls)
			{
				LOG_CRIT("Could not find an corresponding entry in the class registry for '%s'", comp->GetComponentName());
				DestroyEntity(clone);
				return nullptr;
			}

			auto clonedComponent = cls->cloneInstanceFn(clone, comp);

			clone->AddComponent(clonedComponent);
		}

		// Even though the PVS was already flushed in CreateEntity we have to flush it again because it has had all its components added now
		FlushPVS(entity);

		return clone;
	}

	Entity* Scene::CloneEntity(Entity* entity, bool retainHierarchy)
	{
		return CloneEntity(entity, entity->GetName(), entity->GetPosition(), entity->GetRotation(), entity->GetScale(), retainHierarchy);
	}

	std::vector<Entity*> Scene::MergeFrom(Scene* scene, std::vector<std::pair<Entity*, Entity*>>* outSourceToMerged)
	{
		std::vector<std::tuple<Entity*, Entity*, std::string, std::string>> renamedEnts;

		std::vector<Entity*> newEnts;

		for (auto& map : scene->GetEntities())
		{
			for (auto& ent : map.second)
			{
				auto newEnt = CloneEntity(ent, false);

				renamedEnts.push_back({ newEnt, ent, newEnt->GetName(), ent->GetName() });
				if (outSourceToMerged != nullptr)
				{
					outSourceToMerged->push_back({ ent, newEnt });
				}

				newEnts.push_back(newEnt);
			}
		}

		auto findRenamedParent = [renamedEnts](const std::string& name)
		{
			for (auto& r : renamedEnts)
			{
				auto clonedEnt = std::get<0>(r);
				auto originalEnt = std::get<1>(r);
				auto clonedName = std::get<2>(r);
				auto originalName = std::get<3>(r);

				if (originalName == name)
					return clonedEnt;
			}
			return (Entity*)nullptr;
		};

		// manually run back through and fix the parenting
		for (auto& r : renamedEnts)
		{
			auto clonedEnt = std::get<0>(r);
			auto originalEnt = std::get<1>(r);
			auto clonedName = std::get<2>(r);
			auto originalName = std::get<3>(r);

			if (auto parent = originalEnt->GetParent(); parent != nullptr)
			{
				auto clonedParent = findRenamedParent(parent->GetName());

				if (clonedParent)
				{
					clonedEnt->SetParent(clonedParent);
				}
			}
		}

		ForceRebuildPVS();
		return newEnts;
	}

	void Scene::AddEntityInternal(Entity* entity)
	{
		std::unique_lock lock(_lock);

		ComponentSignature signature = entity->GetComponentSignature();

		{
			auto it = _entities.find(signature);

			if (it == _entities.end())
				_entities[signature].push_back(entity);
			else
			{
				if (std::find(it->second.begin(), it->second.end(), entity) == it->second.end())
					it->second.push_back(entity);
			}
		}

		for (auto&& listener : _entityListeners)
		{
			listener->OnAddEntity(entity);
		}

		//_flushEnts = true;
		_updateFlags |= SceneUpdateAddedEntity;

		_entNameMap[entity->GetName()] = entity;;

		FlushPVS(entity);
	}

	void Scene::FlushPVS(Entity* entity, bool remove)
	{
		for (auto& camera : _cameras)
		{
			if(remove)
				camera->GetPVS()->RemoveEntity(entity);
			else
				camera->GetPVS()->AddEntity(entity);

			// We have to make the camera think it moved otherwise it won't refresh the PVS
			/*TransformChangedMessage message;
			message._flags = TransformChangedMessage::ChangeFlags::PositionChanged;
			message._position = camera->GetEntity()->GetPosition();

			camera->OnMessage(&message, nullptr);*/
		}

		for (auto& caster : g_pEnv->_sceneRenderer->GetShadowCasters())
		{
			if (caster->GetEntity() == entity)
				continue;

			for (auto i = 0; i < caster->GetMaxSupportedShadowCascades(); ++i)
			{
				if(remove)
					caster->GetPVS(i)->RemoveEntity(entity);
				else
					caster->GetPVS(i)->AddEntity(entity);
			}
		}
	}

	void Scene::OnGUI()
	{
		if (_mainCamera)
		{
			std::unique_lock lock(_lock);

			for (auto& renderable : _mainCamera->GetPVS()->GetRenderables())
			{
				for (auto& tuple : renderable.second)
				{
					auto ent = std::get<1>(tuple);

					ent->OnGUI();
				}
			}
		}
	}

	void Scene::ForceRebuildPVS()
	{
		for (auto& camera : _cameras)
		{
			camera->GetPVS()->ForceRebuild();
		}

		for (auto& caster : g_pEnv->_sceneRenderer->GetShadowCasters())
		{
			for (auto i = 0; i < caster->GetMaxSupportedShadowCascades(); ++i)
			{
				caster->GetPVS(i)->ForceRebuild();
			}
		}
	}

	void Scene::BroadcastMessage(Message* message)
	{
		const auto& entities = GetEntities();

		for (auto& listener : _auxMessageListeners)
		{
			listener->OnMessage(message, nullptr);
		}
	}

	void Scene::RegisterMessageListener(MessageListener* listener)
	{
		_auxMessageListeners.push_back(listener);
	}

	void Scene::UnregisterMessageListener(MessageListener* listener)
	{
		_auxMessageListeners.erase(std::remove(_auxMessageListeners.begin(), _auxMessageListeners.end(), listener));
	}

	void Scene::RemoveEntityInternal(Entity* entity)
	{
		std::unique_lock lock(_lock);		

		if (entity->GetComponent<DirectionalLight>() == _sunLight)
		{
			_sunLight = nullptr;
		}

		ComponentSignature signature = entity->GetComponentSignature();

		for (auto& set : _entities)
		{
			set.second.erase(std::remove(set.second.begin(), set.second.end(), entity), set.second.end());

			
		}

		// Now we check to see if any of the entity vectors are empty, and remove them if so
		for (auto it = _entities.begin(); it != _entities.end(); )
		{
			if (it->second.size() == 0)
			{
				it = _entities.erase(it);
				//it--;
			}
			else
				it++;
		}

		for (auto& component : entity->GetAllComponents())
		{
			// remove the shadow caster
			if (auto light = component->CastAs<Light>(); light != nullptr)
			{
				if (light->GetDoesCastShadows() == true)
				{
					g_pEnv->_sceneRenderer->RemoveShadowCaster(light);
				}
			}

			for (auto& set : _components)
			{
				set.second.erase(std::remove(set.second.begin(), set.second.end(), component), set.second.end());
			}
		}

		// Now we check to see if any of the component vectors are empty, and remove them if so
		for (auto it = _components.begin(); it != _components.end();)
		{
			if (it->second.size() == 0)
			{
				it = _components.erase(it);
			}
			else
				it++;
		}

		for (auto&& listener : _entityListeners)
		{
			listener->OnRemoveEntity(entity);
		}

		_updateFlags |= SceneUpdateRemovedEntity;

		_entNameMap.erase(entity->GetName());

		//_flushEnts = true;
		//_didDeleteEnts = true;		

		// forcefully clear the list of renderables to ensure we didn't get any artifacts
		FlushPVS(entity, true);

		//entity->Destroy();
		delete entity;
	}

	uint32_t Scene::GetTotalNumberOfEntities()
	{
		std::unique_lock lock(_lock);

		uint32_t totalEnts = 0;

		for (auto& ents : _entities)
		{
			totalEnts += (uint32_t)ents.second.size();
		}

		return totalEnts;
	}

	void Scene::DestroyEntity(Entity* entity, bool broadcast)
	{
		std::unique_lock lock(_lock);

		PROFILE();

		assert(entity != nullptr && "Trying to remove a NULL entity!");

		LOG_DEBUG("Removing entity [%p] %s. There will be %d entities remaining in the scene", entity, entity->GetName().c_str(), GetTotalNumberOfEntities()-1);

		if (entity == _skySphere)
		{
			_skySphere = nullptr;
		}

		if(entity->HasFlag(EntityFlags::IsPendingRemoval) == false)
			entity->DeleteMe(broadcast);

		if (_insideEntityIteration)
		{
			LOG_DEBUG("Entity [%p] '%s' cannot be removed immediately because iteration is in progress, but will be removed next tick", entity, entity->GetName().c_str());

			_pendingRemovals.insert(entity);			
		}
		else
		{
			RemoveEntityInternal(entity);			
		}		
	}

	void Scene::OnEntityAddComponent(Entity* entity, ComponentSignature previousSignature, BaseComponent* component)
	{
		std::unique_lock lock(_lock);

		{
			// Remove it from its old set first
			auto it = _entities.find(previousSignature);

			if (it != _entities.end())
			{
				it->second.erase(std::remove(it->second.begin(), it->second.end(), entity), it->second.end());
			}

			ComponentSignature newSignature = entity->GetComponentSignature();

			// Now add it to a new set, or create one

			EntityVector* entList = nullptr;

			for (auto& ent : _entities)
			{
				if ((ent.first & newSignature) == newSignature)
				{
					entList = &ent.second;
					break;
				}
			}

			if (!entList)
				_entities[newSignature].push_back(entity);
			else
			{
				if (std::find(entList->begin(), entList->end(), entity) == entList->end())
					entList->push_back(entity);
			}

			/*it = _entities.find(newSignature);

			if (it == _entities.end())
			{
				_entities[newSignature].push_back(entity);
			}
			else
			{
				it->second.push_back(entity);
			}*/
		}

		// now add to the component list
		{
			// Remove it from its old set first
			/*auto it = _components.find(previousSignature);

			if (it != _components.end())
			{
				for (auto& oldComp : entity->GetAllComponents())
				{
					it->second.erase(std::remove(it->second.begin(), it->second.end(), oldComp), it->second.end());
				}
			}*/

			ComponentSignature newSignature = entity->GetComponentSignature();

			// Now add it to a new set, or create one
			/*EntityComponentVector* entList = nullptr;

			for (auto& ent : _components)
			{
				if ((ent.first & newSignature) == newSignature)
				{
					entList = &ent.second;
					break;
				}
			}

			if (!entList)*/			
				_components[component->GetComponentId()].push_back(component);
			/*else
			{
				if (std::find(entList->begin(), entList->end(), component) == entList->end())
					entList->push_back(component);
			}*/

			/*it = _components.find(newSignature);

			if (it == _components.end())
			{
				_components[newSignature].push_back(component);
			}
			else
			{
				it->second.push_back(component);
			}*/

				// add it to the update list too, its a special case where the component should exist in two lists
				if (entity->HasA<UpdateComponent>())
				{
					auto& uit = _components[UpdateComponent::_GetComponentId()];

					if (std::find(uit.begin(), uit.end(), component) == uit.end())
					{
						uit.push_back(component);
					}
				}
		}

		// attempt to automatigally set the main camera, if it hasn't already been set
		if (component->GetComponentId() == Camera::_GetComponentId())
		{
			if(_mainCamera == nullptr)
				_mainCamera = component->CastAs<Camera>();

			_cameras.push_back(component->CastAs<Camera>());
		}

		if (component->GetComponentId() == DirectionalLight::_GetComponentId() && _sunLight == nullptr)
		{
			_sunLight = component->CastAs<DirectionalLight>();
		}

		for (auto&& listener : _entityListeners)
		{
			listener->OnAddComponent(entity, component);
		}
	}

	void Scene::OnEntityRemoveComponent(Entity* entity, ComponentSignature previousSignature, BaseComponent* component)
	{
		std::unique_lock lock(_lock);

		if (entity == nullptr || component == nullptr)
			return;

		const bool isEntityPendingDeletion = entity->IsPendingDeletion();

		if (!isEntityPendingDeletion)
		{
			// Remove from the previous entity-signature bucket.
			if (auto it = _entities.find(previousSignature); it != _entities.end())
			{
				it->second.erase(std::remove(it->second.begin(), it->second.end(), entity), it->second.end());
			}

			// Reinsert into the new bucket after the component-signature change.
			const ComponentSignature newSignature = entity->GetComponentSignature();
			if (auto it = _entities.find(newSignature); it != _entities.end())
			{
				if (std::find(it->second.begin(), it->second.end(), entity) == it->second.end())
					it->second.push_back(entity);
			}
			else
			{
				_entities[newSignature].push_back(entity);
			}
		}
		else
		{
			// During entity teardown, never re-bucket the entity.
			for (auto it = _entities.begin(); it != _entities.end();)
			{
				it->second.erase(std::remove(it->second.begin(), it->second.end(), entity), it->second.end());
				if (it->second.empty())
					it = _entities.erase(it);
				else
					++it;
			}
		}

		// Remove from the per-component cache using the component id (not signature).
		if (auto it = _components.find(component->GetComponentId()); it != _components.end())
		{
			it->second.erase(std::remove(it->second.begin(), it->second.end(), component), it->second.end());
		}

		// Some components are mirrored in the UpdateComponent list, remove stale entries there too.
		if (auto it = _components.find(UpdateComponent::_GetComponentId()); it != _components.end())
		{
			it->second.erase(std::remove(it->second.begin(), it->second.end(), component), it->second.end());
		}

		// Remove any empty component buckets.
		for (auto it = _components.begin(); it != _components.end();)
		{
			if (it->second.empty())
				it = _components.erase(it);
			else
				++it;
		}

		if (component->GetComponentId() == Camera::_GetComponentId())
		{
			_cameras.erase(std::remove(_cameras.begin(), _cameras.end(), component->CastAs<Camera>()), _cameras.end());

			if (_mainCamera == component->CastAs<Camera>())
				_mainCamera = nullptr;
		}

		if (!isEntityPendingDeletion)
		{
			for (auto&& listener : _entityListeners)
			{
				listener->OnRemoveComponent(entity, component);
			}
		}
	}

	void Scene::HandlePendingRemovals()
	{
		std::unique_lock lock(_lock);

		if (_pendingRemovals.size() > 0)
		{
			for (auto& ent : _pendingRemovals)
			{
				LOG_DEBUG("Entity [%p] was deferred removed", ent);

				RemoveEntityInternal(ent);
			}

			_pendingRemovals.clear();
		}
	}

	void Scene::HandlePendingAdditions()
	{
		std::unique_lock lock(_lock);

		if (_pendingAdditions.size() > 0)
		{
			for (auto& pending : _pendingAdditions)
				AddEntityInternal(pending);

			_pendingAdditions.clear();
		}
	}

	void Scene::FixedUpdate(float frameTime)
	{
		std::unique_lock lock(_lock);

		PROFILE();

		HandlePendingRemovals();
		HandlePendingAdditions();		

		_insideEntityIteration = true;

		std::vector<UpdateComponent*> updateSet;

		if (GetComponents<UpdateComponent>(updateSet))
		{
			for (auto&& component : updateSet)
			{
				auto updateComponent = component->CastAs<UpdateComponent>();

				if (!updateComponent)
					continue;

				if (updateComponent->GetEntity()->IsPendingDeletion())
				{
					DestroyEntity(updateComponent->GetEntity());
					continue;
				}

				updateComponent->FixedUpdate(frameTime);
			}
		}

		_insideEntityIteration = false;

		HandlePendingRemovals();
	}

	void Scene::Update(float frameTime)
	{		
		std::unique_lock lock(_lock);

		_drawCalls = 0;
		_didAnyDrawnItemReflect = false;

		PROFILE();

		HandlePendingRemovals();
		HandlePendingAdditions();

		_insideEntityIteration = true;
		
		std::vector<UpdateComponent*> updateSet;

		if (GetComponents<UpdateComponent>(updateSet))
		{
			for (auto&& component : updateSet)
			{
				auto updateComponent = component->CastAs<UpdateComponent>();

				if (!updateComponent)
					continue;

				if (updateComponent->CanUpdate())
				{
					float timeOffset = updateComponent->GetTickRate() > 1 ? g_pEnv->_timeManager->_currentTime - updateComponent->GetLastUpdateTime() : 0.0f;
					updateComponent->Update(frameTime + timeOffset);
				}
			}
		}

		std::vector<StaticMeshComponent*> meshSet;

		if (GetComponents<StaticMeshComponent>(meshSet))
		{
			for (auto&& component : meshSet)
			{
				component->GetEntity()->GetComponent<Transform>()->UpdateInterpolatedPosition(r_interpolate._val.b);
			}
		}

		if (GetMainCamera() && GetMainCamera()->HasMovedThisFrame())
		{
			_updateFlags |= SceneUpdateCameraMoved;

			UpdateSkySphereMatrix();
		}

		_insideEntityIteration = false;

		HandlePendingRemovals();
	}

	void Scene::LateUpdate(float frameTime)
	{
		std::unique_lock lock(_lock);

		PROFILE();

		_insideEntityIteration = true;

		std::vector<UpdateComponent*> updateSet;

		if (GetComponents<UpdateComponent>(updateSet))
		{
			for (auto&& component : updateSet)
			{
				auto updateComponent = component->CastAs<UpdateComponent>();

				if (!updateComponent)
					continue;

				if (updateComponent->GetEntity()->IsPendingDeletion())
				{
					DestroyEntity(updateComponent->GetEntity());
					continue;
				}

				if (updateComponent->CanUpdate())
					updateComponent->LateUpdate(frameTime);
			}
		}

		_insideEntityIteration = false;
		_wasPvsReset = false;

		_updateFlags = SceneUpdateNone;

		if(GetMainCamera())
			GetMainCamera()->ResetHasMovedThisFrame();

		//_didDeleteEnts = false;
		//_flushEnts = false;

		g_pEnv->_chunkManager->ChunkLoader();
	}

	void Scene::SetFogColour(const math::Color& colour)
	{
		_fogColour = colour;
	}

	void Scene::SetAmbientLight(const math::Vector4& ambient)
	{
		_ambientLight = ambient;
	}

	const math::Color& Scene::GetFogColour() const
	{
		return _fogColour;
	}

	const math::Vector4& Scene::GetAmbientColour() const
	{
		return _ambientLight;
	}

	void Scene::Lock()
	{
		_lock.lock();
	}

	void Scene::Unlock()
	{
		_lock.unlock();
	}

	bool Scene::TryLock()
	{
		return _lock.try_lock();
	}

	const std::wstring& Scene::GetName() const
	{
		return _name;
	}

	void Scene::SetName(const std::wstring& name)
	{
		_name = name;
	}

	void Scene::SetSkySphere(Entity* skySphere)
	{
		_skySphere = skySphere;
	}

	void Scene::UpdateSkySphereMatrix()
	{
		//_skySphereMatrix = math::Matrix::CreateScale(math::Vector3(2.0f)) * math::Matrix::CreateTranslation(_mainCamera->GetEntity()->GetPosition());

		if(_skySphere)
			_skySphere->SetPosition(_mainCamera->GetEntity()->GetPosition() + _mainCamera->GetViewOffset());
	}

	void Scene::OnDebugGUI()
	{
		if (r_debugScene._val.b && _mainCamera)
		{
			auto renderer = g_pEnv->GetUIManager().GetRenderer();
			auto width = g_pEnv->GetScreenWidth();
			int32_t x = width - 20;
			int32_t y = 20;
			auto font = g_pEnv->_commandManager->GetConsole()->GetFont();
			auto cameraTransform = _mainCamera->GetEntity()->GetComponent<Transform>();
			const auto& cameraPos = cameraTransform->GetPosition();
			auto pvs = _mainCamera->GetPVS();
			const auto& optimisedPvs = pvs->GetOptimisedParams();
			const auto& frustum = _mainCamera->GetFrustum();
			const auto& frustumSphere = optimisedPvs.shape.sphere;// _mainCamera->GetFrustumSphere();

			if (_updateFlags != 0)
			{
				renderer->PrintText(font.get(), 14, x, y, math::Color(1, 0, 0, 1), FontAlign::Right, L"Flushing entities"); y += 15;
			}
			else
			{
				renderer->PrintText(font.get(), 14, x, y, math::Color(0, 01, 0, 1), FontAlign::Right, L"Not flushing entities"); y += 15;
			}

			renderer->PrintText(font.get(), 14, x, y, math::Color(1, 1, 0.5f, 1), FontAlign::Right, std::format(L"Camera pos {:.2f} {:.2f} {:.2f} ", cameraPos.x, cameraPos.y, cameraPos.z));
			y += 15;

			renderer->PrintText(font.get(), 14, x, y, math::Color(1, 1, 0.5f, 1), FontAlign::Right, std::format(L"Frustum centre pos {:.2f} {:.2f} {:.2f}", frustum.Origin.x, frustum.Origin.y, frustum.Origin.z));
			y += 15;

			renderer->PrintText(font.get(), 14, x, y, math::Color(1, 1, 0.5f, 1), FontAlign::Right, std::format(L"Frustum sphere pos {:.2f} {:.2f} {:.2f} Radius {:.2f}", frustumSphere.Center.x, frustumSphere.Center.y, frustumSphere.Center.z, frustumSphere.Radius));
			y += 15;

#if 0
			for (int32_t i = 0; i < 7; ++i)
			{
				int32_t drawn = 0;
				for (auto& renderSet : _renderables[i])
				{
					drawn += renderSet.second.size();
				}
				renderer->PrintText(font, 14, x, y, math::Color(1, 1, 1, 1), FontAlign::Right, std::format(L"VisShape[{:d}] pos {:.2f} {:.2f} {:.2f} Ents {:d}", i, _visSpheres[i].shape.Center.x, _visSpheres[i].shape.Center.y, _visSpheres[i].shape.Center.z, drawn));
				y += 15;
			}
#endif

			renderer->PrintText(font.get(), 14, x, y, math::Color(1, 1, 1, 1), FontAlign::Right, std::format(L"Drawn entities {:d}", pvs->GetTotalNumberOfEnts())); y += 15;
			renderer->PrintText(font.get(), 14, x, y, math::Color(1, 1, 1, 1), FontAlign::Right, std::format(L"Drawn skeletal animators {:d}", pvs->GetTotalSkeletalAnimators())); y += 15;
			renderer->PrintText(font.get(), 14, x, y, math::Color(1, 1, 1, 1), FontAlign::Right, std::format(L"Draw calls {:d}", _drawCalls)); y += 15;
			renderer->PrintText(font.get(), 14, x, y, math::Color(1, 1, 1, 1), FontAlign::Right, std::format(L"Chunks visible {:d}", g_pEnv->_chunkManager->GetNumChunksVisible())); y += 15;
		}
	}

	namespace
	{
		

		bool PrepareMeshRender(Mesh* mesh, Material* material, MeshRenderFlags flags, int32_t instanceId, CullingMode shadowCullMode)
		{
			if (!mesh || !material)
				return false;

			auto shader = material->GetStandardShader();
			const bool isShadowMap = (flags & MeshRenderFlags::MeshRenderShadowMap) != 0;
			const bool disableShadowSampling = r_profileDisableShadowSampling._val.b;
			const bool disableNormalMaps = r_profileDisableNormalMaps._val.b;
			const bool disableSurfaceMaps = r_profileDisableSurfaceMaps._val.b;
			static auto defaultTexture = ITexture2D::GetDefaultTexture();

			if (isShadowMap)
			{
				auto shadowShader = material->GetShadowMapShader();
				if (!shadowShader)
					shadowShader = IShader::Create("EngineData.Shaders/ShadowMapGeometry.hcs"); // it should never get here, hard fallback
				if (shadowShader)
					shader = shadowShader;
			}

			if (!shader)
			{
				LOG_WARN("Cannot render a mesh without a valid shader, please check the material has a shader applied!");
				return false;
			}

			auto graphicsDevice = g_pEnv->_graphicsDevice;
			graphicsDevice->SetPixelShader(shader->GetShaderStage(ShaderStage::PixelShader));
			graphicsDevice->SetVertexShader(shader->GetShaderStage(ShaderStage::VertexShader));
			graphicsDevice->SetInputLayout(shader->GetInputLayout());

			material->SaveRenderState();
			graphicsDevice->SetBlendState(material->GetBlendState());
			graphicsDevice->SetDepthBufferState(material->GetDepthState());
			graphicsDevice->SetCullingMode(isShadowMap ? shadowCullMode : material->GetCullMode());

			mesh->UpdateConstantBuffer(nullptr, math::Matrix::Identity, material, instanceId);

			auto requirements = shader->GetRequirements();
			if (HEX_HASFLAG(requirements, ShaderRequirements::RequiresGBuffer))
				g_pEnv->_sceneRenderer->GetGBuffer()->BindAsShaderResource();

			if (HEX_HASFLAG(requirements, ShaderRequirements::RequiresShadowMaps))
			{
				if (disableShadowSampling)
					graphicsDevice->SetTexture2D(nullptr);
				else
					g_pEnv->_sceneRenderer->GetCurrentShadowMap()->BindAsShaderResource();
			}

			if (HEX_HASFLAG(requirements, ShaderRequirements::RequiresBeauty))
				graphicsDevice->SetTexture2D(g_pEnv->_sceneRenderer->GetBeautyTexture());

			uint32_t slotIdx = graphicsDevice->GetBoundResourceIndex();

			std::vector<ITexture2D*> textures = {
				material->GetTexture(MaterialTexture::Albedo).get(),
				disableNormalMaps ? defaultTexture.get() : material->GetTexture(MaterialTexture::Normal).get(),
				disableSurfaceMaps ? defaultTexture.get() : material->GetTexture(MaterialTexture::Roughness).get(),
				disableSurfaceMaps ? defaultTexture.get() : material->GetTexture(MaterialTexture::Metallic).get(),
				disableSurfaceMaps ? defaultTexture.get() : material->GetTexture(MaterialTexture::Height).get(),
				disableSurfaceMaps ? defaultTexture.get() : material->GetTexture(MaterialTexture::Emission).get(),
				disableSurfaceMaps ? defaultTexture.get() : material->GetTexture(MaterialTexture::Opacity).get(),
				disableSurfaceMaps ? defaultTexture.get() : material->GetTexture(MaterialTexture::AmbientOcclusion).get()
			};

			graphicsDevice->SetTexture2DArray(slotIdx, textures);

			mesh->SetBuffers(isShadowMap);
			return true;
		}
	}

	template <typename T>
	void RenderInstance(T* instance, uint32_t numInstances, Material* material, bool& rendered)
	{
		if (instance)
		{
			instance->Finish();

			if (numInstances > 0)
			{
				g_pEnv->_graphicsDevice->DrawIndexedInstanced(instance->GetMesh()->GetNumIndices(), (uint32_t)numInstances);

				if (material)
					material->RestoreRenderState();

				rendered = false;
			}
		}
	}

	void Scene::RenderEntities(PVS* pvs, LayerMask layerMask, MeshRenderFlags renderFlags)
	{
		PROFILE();

		auto& snapshot = pvs->GetRenderableSnapshot();
		uint32_t totalCandidates = 0;
		uint32_t skippedNullMeshOrInstance = 0;
		uint32_t skippedTransparencyGate = 0;
		uint32_t skippedLayerMask = 0;
		uint32_t skippedLod = 0;
		uint32_t skippedPrepareRender = 0;
		uint32_t drawnInstancesTotal = 0;

		if(pvs->DidRebuild())
		{
			std::unique_lock lock(_lock);
			const auto& pvsRenderables = pvs->GetRenderables();			
			snapshot.clear();
			snapshot.reserve(pvsRenderables.size());

			for (const auto& renderableBatch : pvsRenderables)
			{
				auto material = renderableBatch.first;
				if (!material)
					continue;

				auto& batch = snapshot.emplace_back(material, std::vector<RenderableSnapshot>()).second;
				batch.reserve(renderableBatch.second.size());

				for (const auto& meshEntityPair : renderableBatch.second)
				{
					auto mesh = std::get<0>(meshEntityPair);
					auto entity = std::get<1>(meshEntityPair);
					auto meshComponent = (StaticMeshComponent*)std::get<2>(meshEntityPair);
					if (!mesh || !entity || !meshComponent)
						continue;

					auto instance = mesh->GetInstance();
					if (!instance)
						continue;

					RenderableSnapshot snapshot;
					snapshot.mesh = mesh;
					snapshot.material = material;
					snapshot.instance = instance;
					snapshot.simpleInstance = instance->GetSimpleInstance();
					snapshot.layer = entity->GetLayer();
					snapshot.hasAnimations = mesh->HasAnimations();
					snapshot.isBoundToBone = meshComponent->IsBoundToBone();
					snapshot.shadowCullMode = meshComponent->GetShadowCullMode();
					snapshot.entity = entity;

					if (snapshot.isBoundToBone)
					{
						snapshot.shadowInstanceData.worldMatrix = entity->GetWorldTMTranspose() * meshComponent->GetOffsetMatrixTranspose();
						snapshot.instanceData.worldMatrix = snapshot.shadowInstanceData.worldMatrix;
						snapshot.instanceData.worldMatrixPrev = entity->GetWorldTMPrevTranspose();
						snapshot.instanceData.worldMatrixInverseTranspose = entity->GetWorldTMInvert();
						snapshot.instanceData.colour = material->_properties.diffuseColour;
						snapshot.instanceData.uvscale = meshComponent->GetUVScale();
					}
					else
					{
						snapshot.shadowInstanceData = meshComponent->GetCachedShadowInstanceData();
						snapshot.instanceData = meshComponent->GetCachedInstanceData(material.get());
					}

					batch.push_back(snapshot);
				}
			}

			_wasPvsReset = true;
			pvs->ResetDidRebuild();
		}

		bool isShadowMap = (renderFlags & MeshRenderFlags::MeshRenderShadowMap) != 0;
		bool isTransparency = (renderFlags & MeshRenderFlags::MeshRenderTransparency) != 0;
		bool isNormalRender = !isShadowMap && !isTransparency;

		if (isNormalRender)
		{
			_drawnEntities = 0;
			_drawCalls = 0;
		}		

		for (auto it = snapshot.begin(); it != snapshot.end(); it++)
		{
			auto material = it->first;

			bool rendered = false;

			int drawnInstances = 0;		
			MeshInstance* currentInstance = nullptr;
			MeshInstance* lastInstance = nullptr;

			/*auto RenderInstance = [&rendered](MeshInstance* instance, uint32_t numInstances, StaticMeshComponent* renderer)
			{
				if (instance)
				{
					instance->Finish();

					if (numInstances > 0)
					{
						g_pEnv->_graphicsDevice->DrawIndexedInstanced(instance->GetMesh()->GetNumIndices(), (uint32_t)numInstances);

						renderer->GetMaterial()->RestoreRenderState();

						rendered = false;
					}
				}
			};*/

			for (auto&& renderable : it->second)
			{
				totalCandidates++;

				auto mesh = renderable.mesh;
				auto instance = renderable.instance;
				SimpleMeshInstance* simpleInstance = renderable.simpleInstance;

				if (!mesh || !instance)
				{
					skippedNullMeshOrInstance++;
					continue;
				}

				currentInstance = instance;

				if (isShadowMap)
					currentInstance = (MeshInstance*)simpleInstance;

				if ((renderFlags & MeshRenderTransparency) == 0)
				{
					if (material->_properties.hasTransparency == 1 || material->_properties.isWater == 1)
					{
						skippedTransparencyGate++;
						continue;
					}

					if (material->DoesHaveAnyReflectivity())
						_didAnyDrawnItemReflect = true;
				}
				else
				{
					if (material->_properties.hasTransparency == 0 && material->_properties.isWater == 0)
					{
						skippedTransparencyGate++;
						continue;
					}
				}

				if (currentInstance != lastInstance && lastInstance != nullptr || renderable.hasAnimations)
				{
					if (isNormalRender)
						++_drawCalls;

					if(isShadowMap)
						RenderInstance((SimpleMeshInstance*)lastInstance, drawnInstances, material.get(), rendered);
					else
						RenderInstance(lastInstance, drawnInstances, material.get(), rendered);
					
					drawnInstances = 0;

					rendered = false;
				}

				// check this entity is in the layer mask we want
				if ((layerMask & LAYERMASK(renderable.layer)) == 0)
				{
					skippedLayerMask++;
					continue;
				}

				// Check for LOD
#if 1
				if (mesh->GetLodLevel() != -1)
				{
					//if (mesh->GetLodLevel() != 1)
					//	continue;

					const float lodPartitions = r_lodPartition._val.f32;

					float minDistance = lodPartitions * (float)(mesh->GetLodLevel());
					float maxDistance = lodPartitions * (float)(mesh->GetLodLevel() + 1);

					float distance = (renderable.instanceData.worldMatrix.Translation() - GetMainCamera()->GetEntity()->GetPosition()).Length();

					if (mesh->GetLodLevel() < 3)
					{
						// always allow level 3
						if (distance < minDistance || distance > maxDistance)
						{
							//LOG_DEBUG("Entity %p failed LOD test at level %d because distance %.1f is greated then max allowed (%.1f) for this level", entity, mesh->GetLodLevel(), distance, maxDistance);
							skippedLod++;
							continue;
						}
					}
					else
					{
						if (distance < minDistance)
						{
							skippedLod++;
							continue;
						}
					}
				}
#endif

				if (!rendered)
				{
					if (isShadowMap)
						simpleInstance->Start();
					else
						instance->Start();

					if (PrepareMeshRender(mesh.get(), material.get(), renderFlags, _drawnEntities, renderable.shadowCullMode) == false)
					{
						LOG_WARN("Failed to prepare mesh render state, ignoring this entity");
						skippedPrepareRender++;
						continue;
					}
					rendered = true;
				}		

				drawnInstances++;
				drawnInstancesTotal++;
				lastInstance = currentInstance;		

				if (isShadowMap)
				{
					simpleInstance->Render(renderable.shadowInstanceData);
				}
				else
				{
					// Fix so sky doesn't used cached position
					if (renderable.layer == Layer::Sky)
					{
						renderable.instanceData.worldMatrix = renderable.entity->GetWorldTMTranspose();
						renderable.instanceData.worldMatrixPrev = renderable.entity->GetWorldTMPrevTranspose();
						renderable.instanceData.worldMatrixInverseTranspose = renderable.entity->GetWorldTMInvert();
					}

					instance->Render(renderable.instanceData);
				}

				if (isNormalRender)
					_drawnEntities += 1;
			}

			if (currentInstance && drawnInstances > 0)
			{
				if (isNormalRender)
					++_drawCalls;

				if (isShadowMap)
					RenderInstance((SimpleMeshInstance*)currentInstance, drawnInstances, material.get(), rendered);
				else
					RenderInstance(currentInstance, drawnInstances, material.get(), rendered);
			}

			
		}

		if (r_debugRenderSkips._val.b)
		{
			static std::unordered_map<uint64_t, uint64_t> lastLoggedFrameByPass;
			const uint64_t frame = g_pEnv && g_pEnv->_timeManager ? g_pEnv->_timeManager->_frameCount : 0;
			const uint64_t passKey = (static_cast<uint64_t>(static_cast<uint32_t>(renderFlags)) << 32) | static_cast<uint64_t>(layerMask);
			const auto lastLogged = lastLoggedFrameByPass.find(passKey);
			if ((lastLogged == lastLoggedFrameByPass.end() || lastLogged->second != frame) && (frame % 60) == 0)
			{
				lastLoggedFrameByPass[passKey] = frame;
				LOG_INFO(
					"RenderEntities pass flags=%u layerMask=0x%08X candidates=%u drawn=%u skip(null=%u,trans=%u,layer=%u,lod=%u,prep=%u) pvsRebuilt=%d snapshotBatches=%zu",
					(uint32_t)renderFlags,
					layerMask,
					totalCandidates,
					drawnInstancesTotal,
					skippedNullMeshOrInstance,
					skippedTransparencyGate,
					skippedLayerMask,
					skippedLod,
					skippedPrepareRender,
					pvs->DidRebuild() ? 1 : 0,
					snapshot.size());
			}
		}
	}

	void Scene::RenderDebug(PVS* pvs)
	{
		std::unique_lock lock(_lock);

		PROFILE();

		for (auto& set : GetEntities())
		{
			for (auto& ent : set.second)
			{
				ent->DebugRender();
			}
		}

		// Allow the game extension to render the debug info (if they want)
		for (auto& extension : g_pEnv->GetGameExtensions())
		{
			extension->OnDebugRender();
		}

		g_pEnv->_chunkManager->DebugRender();

		if(phys_debug._val.b)
			g_pEnv->_physicsSystem->DebugRender();

		g_pEnv->_debugRenderer->FlushBuffers();

		//g_pEnv->_graphicsDevice->SetDepthBufferState(DepthBufferState::DepthDefault);
	}

	SceneFlags Scene::GetFlags()
	{
		return _flags;
	}

	uint32_t Scene::GetNumberOfEntitiesDrawn()
	{
		return _drawnEntities;
	}

	uint32_t Scene::GetDrawCalls()
	{
		return _drawCalls;
	}

	DirectionalLight* Scene::GetSunLight()
	{
		return _sunLight;
	}

	/*void Scene::AddCamera(Camera* camera)
	{
		_cameras.push_back(camera);
		AddEntity(camera);
	}*/

	/*Camera* Scene::GetCameraAtIndex(uint32_t index)
	{
		return _cameras.at(index);
	}*/

	Camera* Scene::GetMainCamera()
	{
		return _mainCamera;
	}

	void Scene::SetMainCamera(Camera* camera)
	{
		_mainCamera = camera;
	}


	const Scene::EntityMap& Scene::GetEntities() const
	{
		return _entities;
	}

	bool Scene::GetEntities(const ComponentSignature signature, std::vector<Entity*>& entities)
	{
		std::unique_lock lock(_lock);

		for (auto& ent : _entities)
		{
			if ((ent.first & signature) != 0)
			{
				entities.insert(entities.end(), ent.second.begin(), ent.second.end());
			}
		}
		
		return entities.size() > 0;
	}

	Entity* Scene::GetEntityByName(const std::string& name)
	{
		std::unique_lock lock(_lock);

		auto it = _entNameMap.find(name);
		if (it != _entNameMap.end())
			return it->second;

		/*for (auto& ent : _entities)
		{
			for (auto& entp : ent.second)
			{
				if (entp->GetName() == name)
					return entp;
			}
		}*/

		return nullptr;
	}

	bool Scene::RenameEntity(Entity* entity, const std::string& desiredName, std::string* outFinalName)
	{
		std::unique_lock lock(_lock);

		if (entity == nullptr || entity->GetScene() != this || desiredName.empty())
			return false;

		const std::string oldName = entity->GetName();
		if (oldName == desiredName)
		{
			if (outFinalName != nullptr)
				*outFinalName = oldName;
			return true;
		}

		auto oldNameIt = _entNameMap.find(oldName);
		if (oldNameIt != _entNameMap.end() && oldNameIt->second == entity)
		{
			_entNameMap.erase(oldNameIt);
		}

		std::string resolvedName = desiredName;
		auto existingIt = _entNameMap.find(resolvedName);
		if (existingIt != _entNameMap.end() && existingIt->second != entity)
		{
			if (_namingPolicy == EntityNamingPolicy::AutoRename)
			{
				std::string baseName = resolvedName;
				int32_t suffix = 1;

				if (auto p = baseName.find_last_not_of("0123456789"); p != baseName.npos)
				{
					if (p < baseName.length() - 1)
					{
						suffix = std::stoi(baseName.substr(p + 1));
						baseName = baseName.substr(0, p + 1);
					}
				}

				do
				{
					resolvedName = baseName + std::to_string(++suffix);
					existingIt = _entNameMap.find(resolvedName);
				} while (existingIt != _entNameMap.end() && existingIt->second != entity);
			}
			else
			{
				_entNameMap[oldName] = entity;
				return false;
			}
		}

		entity->SetName(resolvedName);
		_entNameMap[resolvedName] = entity;

		if (outFinalName != nullptr)
			*outFinalName = resolvedName;

		return true;
	}

	uint32_t Scene::GetNumberOfComponentsOfType(const ComponentId id)
	{
		EntityComponentVector components;

		//GetComponents((1 << id), components);

		return (uint32_t)components.size();
	}

	/*bool Scene::GetComponents(ComponentId id, std::vector<BaseComponent*>& components)
	{
		std::unique_lock lock(_lock);

		components = _components[id];

		return components.size() > 0;
	}*/

	void Scene::AddEntityListener(IEntityListener* listener)
	{
		std::unique_lock lock(_lock);

		_entityListeners.push_back(listener);
	}

	void Scene::RemoveEntityListener(IEntityListener* listener)
	{
		std::unique_lock lock(_lock);

		_entityListeners.erase(std::remove(_entityListeners.begin(), _entityListeners.end(), listener), _entityListeners.end());
	}

	void Scene::SetFlags(SceneFlags flags)
	{
		_flags = flags;
	}

	/*void Scene::PushTerrainParams(const TerrainGenerationParams& params)
	{
		_terrainParams.push_back(params);
	}

	const std::vector<TerrainGenerationParams>& Scene::GetTerrainParams() const
	{
		return _terrainParams;
	}*/

	void Scene::Save(json& data, JsonFile* file)
	{
		file->Serialize<OceanSettings>(data, "_oceanSettings", _oceanSettings);
	}

	void Scene::Load(json& data, JsonFile* file)
	{
		if (data.find("_oceanSettings") != data.end())
		{
			file->Deserialize<OceanSettings>(data, "_oceanSettings", _oceanSettings);
		}
	}

	void Scene::CalculateBounds(math::Vector3& min, math::Vector3& max)
	{
		std::vector<Entity*> entity;

		min = math::Vector3(FLT_MAX);
		max = math::Vector3(FLT_MIN);

		if (GetEntities(1 << StaticMeshComponent::_GetComponentId(), entity))
		{
			for (auto& ent : entity)
			{
				const auto& worldAABB = ent->GetWorldAABB();

				math::Vector3 bbCentre(worldAABB.Center);
				math::Vector3 bbExtents(worldAABB.Extents);
				math::Vector3 bbMin = bbCentre - bbExtents;
				math::Vector3 bbMax = bbCentre + bbExtents;

				for (int i = 0; i < 3; ++i)
				{
					if (((float*)&bbMin.x)[i] < ((float*)&min.x)[i])
						((float*)&min.x)[i] = ((float*)&bbMin.x)[i];

					if (((float*)&bbMax.x)[i] > ((float*)&max.x)[i])
						((float*)&max.x)[i] = ((float*)&bbMax.x)[i];
				}
			}
		}
	}

	bool Scene::GatherStaticMeshesInBounds(const dx::BoundingBox& bounds, std::vector<StaticMeshComponent*>& outComponents, bool includeDynamic)
	{
		std::unique_lock lock(_lock);

		outComponents.clear();

		auto it = _components.find(StaticMeshComponent::_GetComponentId());
		if (it == _components.end())
			return false;

		outComponents.reserve(it->second.size());
		for (BaseComponent* base : it->second)
		{
			auto* component = static_cast<StaticMeshComponent*>(base);
			if (component == nullptr || component->GetMesh() == nullptr)
				continue;

			Entity* entity = component->GetEntity();
			if (entity == nullptr || entity->IsPendingDeletion())
				continue;

			if (entity->HasFlag(EntityFlags::DoNotRender))
				continue;

			if (!includeDynamic)
			{
				const Layer layer = entity->GetLayer();
				const bool isStaticLayer =
					layer == Layer::StaticGeometry ||
					layer == Layer::Decorative ||
					layer == Layer::Grass /*||
					layer == Layer::Sky*/;

				if (!isStaticLayer)
					continue;
			}

			const auto& worldAabb = entity->GetWorldAABB();
			if (!bounds.Intersects(worldAabb))
				continue;

			outComponents.push_back(component);
		}

		return !outComponents.empty();
	}

	void Scene::CalculateSceneStats(std::vector<math::Vector3>& vertices, std::vector<uint16_t>& indices, uint32_t& numFaces, EntityFlags excludeFlags)
	{
		numFaces = 0;

		std::vector<StaticMeshComponent*> smcs;
		if (GetComponents<StaticMeshComponent>(smcs))
		{
			for (auto& smc : smcs)
			{
				auto mesh = smc->GetMesh();

				if (!mesh)
					continue;

				auto entity = smc->GetEntity();

				if (!entity)
					continue;

				if (entity->HasFlag(excludeFlags))
					continue;

				auto verts = mesh->GetVertices();
				auto inds = mesh->GetIndices();

				numFaces += mesh->GetNumFaces();

				for (auto& v : verts)
				{
					vertices.push_back(*(math::Vector3*)&v._position.x);
				}

				indices.insert(indices.end(), inds.begin(), inds.end());
			}
		}
	}

	void Scene::CalculateSceneStats_UInt32(std::vector<math::Vector3>& vertices, std::vector<uint32_t>& indices, uint32_t& numFaces, EntityFlags excludeFlags)
	{
		numFaces = 0;

		std::vector<StaticMeshComponent*> smcs;
		if (GetComponents<StaticMeshComponent>(smcs))
		{
			uint32_t indexOffset = 0;

			for (auto& smc : smcs)
			{
				auto mesh = smc->GetMesh();

				if (!mesh)
					continue;

				auto entity = smc->GetEntity();

				if (!entity)
					continue;

				if (entity->HasFlag(excludeFlags))
					continue;

				auto verts = mesh->GetVertices();
				auto inds = mesh->GetIndices();

				numFaces += mesh->GetNumFaces();

				auto entPos = smc->GetEntity()->GetPosition();

				for (auto& v : verts)
				{
					vertices.push_back(entPos + *(math::Vector3*)&v._position.x);
				}				

				for (auto& i : inds)
				{
					indices.push_back((int)i + indexOffset);
				}

				indexOffset += (uint32_t)verts.size();
			}
		}
	}
}
