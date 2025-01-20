

#include "Scene.hpp"
#include "../HexEngine.hpp"

#include "../Entity/Component/Transform.hpp"
#include "../Entity/Component/StaticMeshComponent.hpp"
#include "../Entity/Component/PointLight.hpp"
#include "../Entity/Component/FirstPersonCameraController.hpp"
#include "PVS.hpp"

namespace HexEngine
{

	extern HVar r_debugScene;
	extern HVar r_interpolate;

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

		_ambientLight = math::Vector4(0.25f, 0.24f, 0.24f, 1.0f);

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

		_ambientLight = math::Vector4(0.25f, 0.24f, 0.24f, 1.0f);

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
		std::unique_lock lock(_lock);

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
			_pendingAdditions.insert(entity);
		else
		{
			LOG_DEBUG("Adding entity [%p] %s", entity, entity->GetName().c_str());

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
				LOG_CRIT("Could not find an corresponding entry in the class registry for '%s'", comp->GetComponentName().c_str());
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

	void Scene::MergeFrom(Scene* scene)
	{
		std::vector<std::tuple<Entity*, Entity*, std::string, std::string>> renamedEnts;

		for (auto& map : scene->GetEntities())
		{
			for (auto& ent : map.second)
			{
				static auto& ptr = map.second;

				auto newEnt = CloneEntity(ent, false);

				renamedEnts.push_back({ newEnt, ent, newEnt->GetName(), ent->GetName() });
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

		_mainCamera->GetPVS()->ClearPVS();
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
			TransformChangedMessage message;
			message._flags = TransformChangedMessage::ChangeFlags::PositionChanged;
			message._position = camera->GetEntity()->GetPosition();

			camera->OnMessage(&message, nullptr);
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

	void Scene::SendMessageToEntities(Message* message, LayerMask layerMask)
	{
		const auto& entities = GetEntities();

		for (auto it = entities.begin(); it != entities.end(); it++)
		{
			for (auto& entity : it->second)
			{
				if ((LAYERMASK(entity->GetLayer()) & layerMask) == 0)
					continue;

				entity->OnMessage(message, nullptr);
			}
		}
	}

	void Scene::RemoveEntityInternal(Entity* entity)
	{
		std::unique_lock lock(_lock);

		// Notify all entities first
		EntityDestroyedMessage message(entity);
		SendMessageToEntities(&message);

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

	void Scene::DestroyEntity(Entity* entity)
	{
		std::unique_lock lock(_lock);

		PROFILE();

		assert(entity != nullptr && "Trying to remove a NULL entity!");

		LOG_DEBUG("Removing entity [%p] %s. There will be %d entities remaining in the scene", entity, entity->GetName().c_str(), GetTotalNumberOfEntities()-1);

		/*if (g_pEnv->_chunkManager->HasActiveChunks())
		{
			Chunk* chunkAtPos = g_pEnv->_chunkManager->GetChunkByPosition(entity->GetPosition());

			if (chunkAtPos)
			{
				chunkAtPos->RemoveChunkChild(entity);
			}
		}*/

		if (entity == _skySphere)
		{
			_skySphere = nullptr;
		}

		if (_insideEntityIteration)
		{
			LOG_DEBUG("Entity [%p] '%s' cannot be removed immediately because iteration is in progress, but will be removed next tick", entity, entity->GetName().c_str());

			_pendingRemovals.insert(entity);
			entity->DeleteMe();
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
			auto it = _components.find(previousSignature);

			if (it != _components.end())
			{
				it->second.erase(std::remove(it->second.begin(), it->second.end(), component), it->second.end());
			}

			ComponentSignature newSignature = entity->GetComponentSignature();

			// Now add it to a new set, or create one
			EntityComponentVector* entList = nullptr;

			for (auto& ent : _components)
			{
				if ((ent.first & newSignature) == newSignature)
				{
					entList = &ent.second;
					break;
				}
			}

			if (!entList)
				_components[newSignature].push_back(component);
			else
			{
				if (std::find(entList->begin(), entList->end(), component) == entList->end())
					entList->push_back(component);
			}

			/*it = _components.find(newSignature);

			if (it == _components.end())
			{
				_components[newSignature].push_back(component);
			}
			else
			{
				it->second.push_back(component);
			}*/
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

		{
			// Remove it from its old set first
			auto it = _entities.find(previousSignature);

			if (it != _entities.end())
			{
				it->second.erase(std::remove(it->second.begin(), it->second.end(), entity), it->second.end());
			}
		}

		// now add to the component list
		{
			// Remove it from its old set first
			auto it = _components.find(previousSignature);

			if (it != _components.end())
			{
				it->second.erase(std::remove(it->second.begin(), it->second.end(), component), it->second.end());
			}
		}

		for (auto&& listener : _entityListeners)
		{
			listener->OnRemoveComponent(entity, component);
		}

		if (component->GetComponentId() == Camera::_GetComponentId())
		{
			_cameras.erase(std::remove(_cameras.begin(), _cameras.end(), component->CastAs<Camera>()), _cameras.end());
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

		std::vector<BaseComponent*> updateSet;

		if (GetComponents(1 << UpdateComponent::_GetComponentId(), updateSet))
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

		PROFILE();

		HandlePendingRemovals();
		HandlePendingAdditions();

		_insideEntityIteration = true;
		
		std::vector<BaseComponent*> updateSet;

		if (GetComponents(1 << UpdateComponent::_GetComponentId(), updateSet))
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

				if(updateComponent->CanUpdate())
					updateComponent->Update(frameTime);
			}
		}

		std::vector<BaseComponent*> meshSet;

		if (GetComponents(1 << StaticMeshComponent::_GetComponentId(), meshSet))
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

		std::vector<BaseComponent*> updateSet;

		if (GetComponents(1 << UpdateComponent::_GetComponentId(), updateSet))
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

		_updateFlags = SceneUpdateNone;

		if(GetMainCamera())
			GetMainCamera()->ResetHasMovedThisFrame();

		//_didDeleteEnts = false;
		//_flushEnts = false;
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
			_skySphere->SetPosition(_mainCamera->GetEntity()->GetPosition());
	}

	void Scene::OnDebugGUI()
	{
		if (r_debugScene._val.b && _mainCamera)
		{
			auto renderer = g_pEnv->_uiManager->GetRenderer();
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

			renderer->PrintText(font.get(), 14, x, y, math::Color(1, 1, 1, 1), FontAlign::Right, std::format(L"Drawn entities {:d} Draw calls {:d}", _drawnEntities, _drawCalls)); y += 15;
		}
	}

	void Scene::RenderEntities(PVS* pvs, LayerMask layerMask, MeshRenderFlags renderFlags)
	{
		std::unique_lock lock(_lock);

		PROFILE();

		const auto& renderableSet = pvs->GetRenderables();

		_drawnEntities = 0;

		for (auto it = renderableSet.begin(); it != renderableSet.end(); it++)
		{
			auto material = it->first;

			bool rendered = false;

			int drawnInstances = 0;		
			MeshInstance* currentInstance = nullptr;
			MeshInstance* lastInstance = nullptr;
			StaticMeshComponent* renderer = nullptr;

			auto RenderInstance = [&rendered](MeshInstance* instance, uint32_t numInstances, StaticMeshComponent* renderer)
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
			};

			for (auto&& meshEntityPair : it->second)
			{
				auto mesh = meshEntityPair.first;
				auto entity = meshEntityPair.second;	
				auto instance = mesh->GetInstance();				

				if (!mesh || !entity || !instance)
					continue;

				renderer = entity->GetComponent<StaticMeshComponent>();
				currentInstance = instance;

				if ((renderFlags & MeshRenderTransparency) == 0)
				{
					if (auto material = renderer->GetMaterial(); material != nullptr)
					{
						if (material->_properties.hasTransparency == 1 || material->_properties.isWater == 1)
							continue;
					}
					else
						continue;
				}

				if (currentInstance != lastInstance && lastInstance != nullptr)
				{
					RenderInstance(lastInstance, drawnInstances, renderer);
					
					drawnInstances = 0;
				}

				// check this entity is in the layer mask we want
				if ((layerMask & LAYERMASK(entity->GetLayer())) == 0)
					continue;

				// Check for LOD
#if 0
				if (mesh->GetLodLevel() != -1)
				{
					//if (mesh->GetLodLevel() != 1)
					//	continue;

					const float lodPartitions = r_lodPartition._val.f32;

					float minDistance = lodPartitions * (float)(mesh->GetLodLevel());
					float maxDistance = lodPartitions * (float)(mesh->GetLodLevel() + 1);

					float distance = (entity->GetPosition() - GetMainCamera()->GetEntity()->GetPosition()).Length();

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
#endif

				if (!rendered)
				{
					instance->Start();					

					if (!renderer)
						continue;

					if (renderer->RenderMesh(mesh.get(), renderFlags, _drawnEntities) == false)
					{
						LOG_WARN("Failed to RenderMesh, ignoring this entity");
						continue;
					}
					rendered = true;
				}				

				// TODO: is this needed
				//if(entity->GetLayer() == Layer::DynamicGeometry)
				//	entity->GetComponent<Transform>()->UpdateInterpolatedPosition();				

				drawnInstances++;
				lastInstance = currentInstance;				

				instance->Render(
					entity->GetWorldTM(),
					entity->GetWorldTMTranspose() /** mesh->_modelTransform.Transpose()*/,
					entity->GetWorldTMPrev().Transpose(),
					material->_properties.diffuseColour,
					renderer->GetUVScale());

				++_drawCalls;
				_drawnEntities += 1;// drawnInstances;
			}

			if (currentInstance && drawnInstances > 0)
			{
				RenderInstance(currentInstance, drawnInstances, renderer);
			}

			
		}
	}

	void Scene::RenderDebug(PVS* pvs)
	{
		std::unique_lock lock(_lock);

		PROFILE();

		g_pEnv->_graphicsDevice->SetDepthBufferState(DepthBufferState::DepthNone);

		EntityComponentVector entities;
		GetComponents(1 << StaticMeshComponent::_GetComponentId(), entities);

		for (auto&& ent : entities)
		{
			//if (ent == GetMainCamera())
			//	continue;

			ent->GetEntity()->DebugRender();
		}

		// Allow the game extension to render the debug info (if they want)
		for (auto& extension : g_pEnv->_gameExtensions)
		{
			extension->OnDebugRender();
		}

		/*if (_hasLastHit)
		{
			g_pEnv->_debugRenderer->DrawLine(_lastHit.position, _lastHit.position + _lastHit.normal * 5.0f, math::Color(1.0f, 0.0f, 0.0f, 1.0f));

			g_pEnv->_debugRenderer->DrawLine(_lastHit.start, _lastHit.position, math::Color(0.0f, 1.0f, 0.0f, 1.0f));
		}*/

		g_pEnv->_physicsSystem->DebugRender();

		g_pEnv->_debugRenderer->FlushBuffers();

		g_pEnv->_graphicsDevice->SetDepthBufferState(DepthBufferState::DepthDefault);
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

		for (auto& ent : _entities)
		{
			for (auto& entp : ent.second)
			{
				if (entp->GetName() == name)
					return entp;
			}
		}

		return nullptr;
	}

	uint32_t Scene::GetNumberOfComponentsOfType(const ComponentId id)
	{
		EntityComponentVector components;

		GetComponents((1 << id), components);

		return components.size();
	}

	bool Scene::GetComponents(const ComponentSignature signature, std::vector<BaseComponent*>& components)
	{
		std::unique_lock lock(_lock);

		for (auto& ent : _components)
		{
			if ((ent.first & signature) != 0)
			{
				components.insert(components.end(), ent.second.begin(), ent.second.end());
			}
		}

		return components.size() > 0;
	}

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

	void Scene::Save(SceneSaveFile* file)
	{
		/*file->Write<int>(_terrainParams.size());

		for (auto& terrain : _terrainParams)
		{
			terrain.Save(file);
		}*/
	}

	void Scene::Load(SceneSaveFile* file)
	{
		//int num = file->Read<int>();
		//for (int32_t i = 0; i < num; ++i)
		//{
		//	//HeightMapGenerationParams first;
		//	//first.Load(file);

		//	TerrainGenerationParams second;
		//	second.Load(file);

		//	_terrainParams.push_back(second);
		//}
	}

	/*void Scene::ClearTerrainParams()
	{
		_terrainParams.clear();
	}*/
}