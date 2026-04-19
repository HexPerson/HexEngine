

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
#if defined(_DEBUG)
#define HEX_VALIDATE_SCENE_INVARIANTS() ValidateInvariants_NoLock()
#else
#define HEX_VALIDATE_SCENE_INVARIANTS() ((void)0)
#endif

	extern HVar r_debugScene;
	extern HVar r_interpolate;
	extern HVar r_lodPartition;

	HVar r_profileDisableShadowSampling("r_profileDisableShadowSampling", "Disable shadow-map sampling in static mesh materials for profiling", false, false, true);
	HVar r_profileDisableNormalMaps("r_profileDisableNormalMaps", "Disable normal map bindings in static mesh materials for profiling", false, false, true);
	HVar r_profileDisableSurfaceMaps("r_profileDisableSurfaceMaps", "Disable roughness, metallic, AO, height, emission and opacity map bindings in static mesh materials for profiling", false, false, true);
	HVar phys_debug("phys_debug", "Enable the physics debugger (very slow)", false, false, true);
	HVar r_debugRenderSkips("r_debugRenderSkips", "Log per-pass render skip counters for scene entity rendering", false, false, true);

	void Scene::ComponentPool::EnsureEntityCapacity(uint32_t slotCount)
	{
		if (sparseEntityToDense.size() < slotCount)
		{
			sparseEntityToDense.resize(slotCount, Scene::InvalidDenseIndex);
		}
	}

	BaseComponent* Scene::ComponentPool::Get(EntityId id) const
	{
		if (!id.IsValid() || id.index >= sparseEntityToDense.size())
			return nullptr;

		const uint32_t denseIndex = sparseEntityToDense[id.index];
		if (denseIndex == Scene::InvalidDenseIndex || denseIndex >= components.size())
			return nullptr;

		const EntityId owner = owners[denseIndex];
		if (owner != id)
			return nullptr;

		return components[denseIndex];
	}

	bool Scene::ComponentPool::Has(EntityId id) const
	{
		return Get(id) != nullptr;
	}

	uint32_t Scene::ComponentPool::Add(EntityId owner, BaseComponent* component)
	{
		EnsureEntityCapacity(owner.index + 1);

		const uint32_t existing = sparseEntityToDense[owner.index];
		if (existing != Scene::InvalidDenseIndex && existing < owners.size() && owners[existing] == owner)
		{
			components[existing] = component;
			return existing;
		}

		const uint32_t denseIndex = static_cast<uint32_t>(components.size());
		components.push_back(component);
		owners.push_back(owner);
		sparseEntityToDense[owner.index] = denseIndex;
		return denseIndex;
	}

	bool Scene::ComponentPool::Remove(EntityId owner, BaseComponent** outRemoved, EntityId* outMovedOwner, uint32_t* outMovedDenseIndex)
	{
		if (!owner.IsValid() || owner.index >= sparseEntityToDense.size())
			return false;

		const uint32_t denseIndex = sparseEntityToDense[owner.index];
		if (denseIndex == Scene::InvalidDenseIndex || denseIndex >= owners.size() || owners[denseIndex] != owner)
			return false;

		if (outRemoved != nullptr)
			*outRemoved = components[denseIndex];

		const uint32_t lastIndex = static_cast<uint32_t>(components.size() - 1);

		if (denseIndex != lastIndex)
		{
			components[denseIndex] = components[lastIndex];
			const EntityId movedOwner = owners[lastIndex];
			owners[denseIndex] = movedOwner;
			sparseEntityToDense[movedOwner.index] = denseIndex;

			if (outMovedOwner != nullptr)
				*outMovedOwner = movedOwner;
			if (outMovedDenseIndex != nullptr)
				*outMovedDenseIndex = denseIndex;
		}
		else
		{
			if (outMovedOwner != nullptr)
				*outMovedOwner = InvalidEntityId;
			if (outMovedDenseIndex != nullptr)
				*outMovedDenseIndex = InvalidDenseIndex;
		}

		components.pop_back();
		owners.pop_back();
		sparseEntityToDense[owner.index] = Scene::InvalidDenseIndex;
		return true;
	}

	Scene::ComponentPool* Scene::GetOrCreateComponentPool(ComponentId componentId)
	{
		auto [it, inserted] = _componentPools.try_emplace(componentId);
		ComponentPool& pool = it->second;
		if (inserted)
		{
			pool.EnsureEntityCapacity(static_cast<uint32_t>(_entitySlots.size()));
		}
		return &pool;
	}

	Scene::ComponentPool* Scene::TryGetComponentPool(ComponentId componentId)
	{
		auto it = _componentPools.find(componentId);
		if (it == _componentPools.end())
			return nullptr;
		return &it->second;
	}

	const Scene::ComponentPool* Scene::TryGetComponentPool(ComponentId componentId) const
	{
		auto it = _componentPools.find(componentId);
		if (it == _componentPools.end())
			return nullptr;
		return &it->second;
	}

	void Scene::EnsureSlotComponentCapacity(EntitySlot& slot, ComponentId componentId)
	{
		if (slot.componentDenseIndices.size() <= componentId)
		{
			slot.componentDenseIndices.resize(componentId + 1, InvalidDenseIndex);
		}
	}

	EntityId Scene::AllocateEntityId(Entity* entity)
	{
		uint32_t index = 0;

		if (!_freeEntitySlotIndices.empty())
		{
			index = _freeEntitySlotIndices.back();
			_freeEntitySlotIndices.pop_back();
		}
		else
		{
			index = static_cast<uint32_t>(_entitySlots.size());
			_entitySlots.emplace_back();
		}

		EntitySlot& slot = _entitySlots[index];
		slot.alive = true;
		slot.inLiveList = false;
		slot.denseEntityIndex = InvalidDenseIndex;
		slot.entity = entity;
		std::fill(slot.componentDenseIndices.begin(), slot.componentDenseIndices.end(), InvalidDenseIndex);
		for (auto& poolIt : _componentPools)
		{
			poolIt.second.EnsureEntityCapacity(static_cast<uint32_t>(_entitySlots.size()));
		}

		EntityId id;
		id.index = index;
		id.generation = slot.generation;
		return id;
	}

	void Scene::FreeEntityId(EntityId id)
	{
		if (!id.IsValid() || id.index >= _entitySlots.size())
			return;

		EntitySlot& slot = _entitySlots[id.index];
		slot.alive = false;
		slot.inLiveList = false;
		slot.denseEntityIndex = InvalidDenseIndex;
		slot.entity = nullptr;
		std::fill(slot.componentDenseIndices.begin(), slot.componentDenseIndices.end(), InvalidDenseIndex);
		++slot.generation;
		_freeEntitySlotIndices.push_back(id.index);
	}

	bool Scene::IsValid(EntityId id) const
	{
		if (!id.IsValid() || id.index >= _entitySlots.size())
			return false;

		const EntitySlot& slot = _entitySlots[id.index];
		return slot.alive && slot.generation == id.generation && slot.entity != nullptr;
	}

	Entity* Scene::TryGetEntity(EntityId id) const
	{
		if (!IsValid(id))
			return nullptr;

		return _entitySlots[id.index].entity;
	}

	void Scene::MarkEntityViewDirty()
	{
		_entityViewDirty = true;
	}

	void Scene::RebuildEntityViewCache() const
	{
		if (!_entityViewDirty)
			return;

		_entities.clear();

		for (const EntityId id : _liveEntities)
		{
			if (!IsValid(id))
				continue;

			Entity* entity = _entitySlots[id.index].entity;
			if (entity == nullptr)
				continue;

			_entities[entity->GetComponentSignature()].push_back(entity);
		}

		_entityViewDirty = false;
	}

	void Scene::ValidateInvariants_NoLock() const
	{
#if defined(_DEBUG)
		for (uint32_t denseIndex = 0; denseIndex < _liveEntities.size(); ++denseIndex)
		{
			const EntityId id = _liveEntities[denseIndex];
			HEX_ASSERT(IsValid(id));

			const EntitySlot& slot = _entitySlots[id.index];
			HEX_ASSERT(slot.inLiveList);
			HEX_ASSERT(slot.denseEntityIndex == denseIndex);
			HEX_ASSERT(slot.entity != nullptr);
			HEX_ASSERT(slot.entity->GetId() == id);
		}

		for (uint32_t slotIndex = 0; slotIndex < _entitySlots.size(); ++slotIndex)
		{
			const EntitySlot& slot = _entitySlots[slotIndex];
			if (slot.alive)
			{
				HEX_ASSERT(slot.entity != nullptr);

				// Legit transient state: entity slot is allocated and components are being added
				// before the entity is inserted into the dense live-entity list.
				if (slot.inLiveList)
				{
					HEX_ASSERT(slot.denseEntityIndex < _liveEntities.size());
					HEX_ASSERT(_liveEntities[slot.denseEntityIndex].index == slotIndex);
					HEX_ASSERT(_liveEntities[slot.denseEntityIndex].generation == slot.generation);
				}
				else
				{
					HEX_ASSERT(slot.denseEntityIndex == InvalidDenseIndex);
				}
			}
			else
			{
				HEX_ASSERT(slot.entity == nullptr);
				HEX_ASSERT(slot.inLiveList == false);
				HEX_ASSERT(slot.denseEntityIndex == InvalidDenseIndex);
			}
		}

		for (const auto& [componentId, pool] : _componentPools)
		{
			HEX_ASSERT(pool.components.size() == pool.owners.size());

			for (uint32_t denseIndex = 0; denseIndex < pool.components.size(); ++denseIndex)
			{
				HEX_ASSERT(pool.components[denseIndex] != nullptr);

				const EntityId owner = pool.owners[denseIndex];
				HEX_ASSERT(IsValid(owner));
				HEX_ASSERT(owner.index < pool.sparseEntityToDense.size());
				HEX_ASSERT(pool.sparseEntityToDense[owner.index] == denseIndex);

				const EntitySlot& ownerSlot = _entitySlots[owner.index];
				HEX_ASSERT(componentId < ownerSlot.componentDenseIndices.size());
				HEX_ASSERT(ownerSlot.componentDenseIndices[componentId] == denseIndex);
			}
		}

		for (uint32_t slotIndex = 0; slotIndex < _entitySlots.size(); ++slotIndex)
		{
			const EntitySlot& slot = _entitySlots[slotIndex];
			if (!slot.alive)
				continue;

			for (ComponentId componentId = 0; componentId < slot.componentDenseIndices.size(); ++componentId)
			{
				const uint32_t denseIndex = slot.componentDenseIndices[componentId];
				if (denseIndex == InvalidDenseIndex)
					continue;

				const auto poolIt = _componentPools.find(componentId);
				HEX_ASSERT(poolIt != _componentPools.end());

				const ComponentPool& pool = poolIt->second;
				HEX_ASSERT(denseIndex < pool.components.size());
				HEX_ASSERT(pool.owners[denseIndex].index == slotIndex);
				HEX_ASSERT(pool.owners[denseIndex].generation == slot.generation);
			}
		}

		HEX_ASSERT(_updateComponents.size() == _updateComponentIndices.size());
		for (uint32_t denseIndex = 0; denseIndex < _updateComponents.size(); ++denseIndex)
		{
			UpdateComponent* component = _updateComponents[denseIndex];
			HEX_ASSERT(component != nullptr);
			const auto indexIt = _updateComponentIndices.find(component);
			HEX_ASSERT(indexIt != _updateComponentIndices.end());
			HEX_ASSERT(indexIt->second == denseIndex);
		}
#endif
	}

	void Scene::AddUpdateComponent(UpdateComponent* component)
	{
		if (component == nullptr)
			return;

		if (auto it = _updateComponentIndices.find(component); it != _updateComponentIndices.end())
			return;

		const uint32_t index = static_cast<uint32_t>(_updateComponents.size());
		_updateComponents.push_back(component);
		_updateComponentIndices[component] = index;
		HEX_VALIDATE_SCENE_INVARIANTS();
	}

	void Scene::RemoveUpdateComponent(UpdateComponent* component)
	{
		if (component == nullptr)
			return;

		auto it = _updateComponentIndices.find(component);
		if (it == _updateComponentIndices.end())
			return;

		const uint32_t index = it->second;
		const uint32_t lastIndex = static_cast<uint32_t>(_updateComponents.size() - 1);
		if (index != lastIndex)
		{
			UpdateComponent* moved = _updateComponents[lastIndex];
			_updateComponents[index] = moved;
			_updateComponentIndices[moved] = index;
		}

		_updateComponents.pop_back();
		_updateComponentIndices.erase(it);
		HEX_VALIDATE_SCENE_INVARIANTS();
	}

	void Scene::Create(bool createSkySphere, IEntityListener* listener)
	{
		if (listener)
		{
			AddEntityListener(listener);
			if (auto* messageListener = dynamic_cast<MessageListener*>(listener); messageListener != nullptr)
			{
				RegisterMessageListener(messageListener);
			}
		}

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
		{
			AddEntityListener(listener);
			if (auto* messageListener = dynamic_cast<MessageListener*>(listener); messageListener != nullptr)
			{
				RegisterMessageListener(messageListener);
			}
		}

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
		_entityViewDirty = true;
		_componentPools.clear();
		_liveEntities.clear();
		_entitySlots.clear();
		_freeEntitySlotIndices.clear();
		_entNameMap.clear();
		_cameras.clear();
		_updateComponents.clear();
		_updateComponentIndices.clear();

		_pendingAdditions.clear();
		_pendingRemovals.clear();

		_mainCamera = nullptr;
		_sunLight = nullptr;
		_skySphere = nullptr;
		HEX_VALIDATE_SCENE_INVARIANTS();
	}

	void Scene::Destroy()
	{
		std::unique_lock lock(_lock);

		HandlePendingRemovals();
		const std::vector<EntityId> liveCopy = _liveEntities;
		for (const EntityId id : liveCopy)
		{
			DestroyEntity(id, false);
		}

		_entities.clear();
		_entityViewDirty = true;
		_componentPools.clear();
		_liveEntities.clear();
		_entitySlots.clear();
		_freeEntitySlotIndices.clear();
		_entNameMap.clear();
		_cameras.clear();
		_updateComponents.clear();
		_updateComponentIndices.clear();

		_pendingAdditions.clear();
		_pendingRemovals.clear();

		_sunLight = nullptr;
		_mainCamera = nullptr;
		_skySphere = nullptr;

		g_pEnv->_debugGui->RemoveCallback(this);
		HEX_VALIDATE_SCENE_INVARIANTS();
	}

	EntityId Scene::CreateEntityId(const std::string& name, const math::Vector3& position, const math::Quaternion& rotation, const math::Vector3& scale)
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
				return existingEnt->GetId();
			}
		}

		Entity* entity = new Entity(this);
		const EntityId id = AllocateEntityId(entity);
		entity->_entityId = id;

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
			_pendingAdditions.insert(entity);
		}
		else
		{
			AddEntityInternal(entity);
		}

		if (entity->IsCreated() == false)
			entity->Create();

		return id;
	}

	Entity* Scene::CreateEntity(const std::string& name, const math::Vector3& position, const math::Quaternion& rotation, const math::Vector3& scale)
	{
		const EntityId id = CreateEntityId(name, position, rotation, scale);
		return TryGetEntity(id);
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
		if (entity == nullptr)
			return;

		const EntityId id = entity->GetId();
		if (!IsValid(id))
			return;

		EntitySlot& slot = _entitySlots[id.index];
		if (slot.inLiveList)
			return;

		slot.denseEntityIndex = static_cast<uint32_t>(_liveEntities.size());
		_liveEntities.push_back(id);
		slot.inLiveList = true;

		for (auto&& listener : _entityListeners)
		{
			listener->OnAddEntity(entity);
		}

		_updateFlags |= SceneUpdateAddedEntity;

		_entNameMap[entity->GetName()] = entity;
		MarkEntityViewDirty();

		FlushPVS(entity);
		HEX_VALIDATE_SCENE_INVARIANTS();
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
		if (entity == nullptr)
			return;

		const EntityId id = entity->GetId();

		if (entity->GetComponent<DirectionalLight>() == _sunLight)
		{
			_sunLight = nullptr;
		}

		_pendingAdditions.erase(entity);
		_pendingRemovals.erase(entity);

		for (auto&& listener : _entityListeners)
		{
			listener->OnRemoveEntity(entity);
		}

		_updateFlags |= SceneUpdateRemovedEntity;

		_entNameMap.erase(entity->GetName());

		FlushPVS(entity, true);
		MarkEntityViewDirty();

		if (IsValid(id))
		{
			EntitySlot& slot = _entitySlots[id.index];
			if (slot.inLiveList && slot.denseEntityIndex < _liveEntities.size())
			{
				const uint32_t removeDenseIndex = slot.denseEntityIndex;
				const uint32_t lastDenseIndex = static_cast<uint32_t>(_liveEntities.size() - 1);
				if (removeDenseIndex != lastDenseIndex)
				{
					const EntityId movedId = _liveEntities[lastDenseIndex];
					_liveEntities[removeDenseIndex] = movedId;
					_entitySlots[movedId.index].denseEntityIndex = removeDenseIndex;
				}

				_liveEntities.pop_back();
				slot.inLiveList = false;
				slot.denseEntityIndex = InvalidDenseIndex;
			}
		}

		delete entity;

		if (id.IsValid())
		{
			FreeEntityId(id);
		}
		HEX_VALIDATE_SCENE_INVARIANTS();
	}

	uint32_t Scene::GetTotalNumberOfEntities()
	{
		std::unique_lock lock(_lock);
		return static_cast<uint32_t>(_liveEntities.size());
	}

	void Scene::DestroyEntity(EntityId entityId, bool broadcast)
	{
		std::unique_lock lock(_lock);
		Entity* entity = TryGetEntity(entityId);
		if (entity == nullptr)
			return;

		DestroyEntity(entity, broadcast);
	}

	void Scene::DestroyEntity(Entity* entity, bool broadcast)
	{
		std::unique_lock lock(_lock);

		PROFILE();

		if (entity == nullptr)
			return;

		LOG_DEBUG("Removing entity [%p] %s. There will be %d entities remaining in the scene", entity, entity->GetName().c_str(), GetTotalNumberOfEntities() > 0 ? GetTotalNumberOfEntities() - 1 : 0);

		if (entity == _skySphere)
		{
			_skySphere = nullptr;
		}

		if(entity->HasFlag(EntityFlags::IsPendingRemoval) == false)
			entity->DeleteMe(broadcast);

		const auto children = entity->GetChildren();
		for (auto* child : children)
		{
			DestroyEntity(child, broadcast);
		}

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
		(void)previousSignature;
		if (entity == nullptr || component == nullptr)
			return;

		const EntityId ownerId = entity->GetId();
		if (!IsValid(ownerId))
			return;

		const ComponentId componentId = component->GetComponentId();
		EntitySlot& slot = _entitySlots[ownerId.index];
		EnsureSlotComponentCapacity(slot, componentId);

		ComponentPool* pool = GetOrCreateComponentPool(componentId);
		pool->EnsureEntityCapacity(static_cast<uint32_t>(_entitySlots.size()));
		uint32_t denseIndex = pool->Add(ownerId, component);
		slot.componentDenseIndices[componentId] = denseIndex;
		MarkEntityViewDirty();

		if (auto* updateComponent = component->CastAs<UpdateComponent>(); updateComponent != nullptr)
		{
			AddUpdateComponent(updateComponent);
		}

		// attempt to automatigally set the main camera, if it hasn't already been set
		if (component->GetComponentId() == Camera::_GetComponentId())
		{
			if(_mainCamera == nullptr)
				_mainCamera = component->CastAs<Camera>();

			Camera* camera = component->CastAs<Camera>();
			if (std::find(_cameras.begin(), _cameras.end(), camera) == _cameras.end())
				_cameras.push_back(camera);
		}

		if (component->GetComponentId() == DirectionalLight::_GetComponentId() && _sunLight == nullptr)
		{
			_sunLight = component->CastAs<DirectionalLight>();
		}

		for (auto&& listener : _entityListeners)
		{
			listener->OnAddComponent(entity, component);
		}
		HEX_VALIDATE_SCENE_INVARIANTS();
	}

	void Scene::OnEntityRemoveComponent(Entity* entity, ComponentSignature previousSignature, BaseComponent* component)
	{
		std::unique_lock lock(_lock);
		(void)previousSignature;

		if (entity == nullptr || component == nullptr)
			return;

		const bool isEntityPendingDeletion = entity->IsPendingDeletion();

		const EntityId ownerId = entity->GetId();
		const ComponentId componentId = component->GetComponentId();
		if (IsValid(ownerId))
		{
			if (auto* pool = TryGetComponentPool(componentId); pool != nullptr)
			{
				EntityId movedOwner = InvalidEntityId;
				uint32_t movedDenseIndex = InvalidDenseIndex;
				pool->Remove(ownerId, nullptr, &movedOwner, &movedDenseIndex);

				if (movedOwner.IsValid() && IsValid(movedOwner))
				{
					EntitySlot& movedSlot = _entitySlots[movedOwner.index];
					EnsureSlotComponentCapacity(movedSlot, componentId);
					movedSlot.componentDenseIndices[componentId] = movedDenseIndex;
				}

				if (pool->components.empty())
				{
					_componentPools.erase(componentId);
				}
			}

			EntitySlot& slot = _entitySlots[ownerId.index];
			EnsureSlotComponentCapacity(slot, componentId);
			slot.componentDenseIndices[componentId] = InvalidDenseIndex;
		}

		if (auto* updateComponent = component->CastAs<UpdateComponent>(); updateComponent != nullptr)
		{
			RemoveUpdateComponent(updateComponent);
		}

		if (component->GetComponentId() == Camera::_GetComponentId())
		{
			_cameras.erase(std::remove(_cameras.begin(), _cameras.end(), component->CastAs<Camera>()), _cameras.end());

			if (_mainCamera == component->CastAs<Camera>())
				_mainCamera = nullptr;
		}

		if (component->GetComponentId() == DirectionalLight::_GetComponentId() && _sunLight == component->CastAs<DirectionalLight>())
		{
			_sunLight = nullptr;
		}

		if (auto* light = component->CastAs<Light>(); light != nullptr && light->GetDoesCastShadows())
		{
			g_pEnv->_sceneRenderer->RemoveShadowCaster(light);
		}
		MarkEntityViewDirty();

		if (!isEntityPendingDeletion)
		{
			for (auto&& listener : _entityListeners)
			{
				listener->OnRemoveComponent(entity, component);
			}
		}
		HEX_VALIDATE_SCENE_INVARIANTS();
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

			ForceRebuildPVS();
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

				const EntityId ownerId = updateComponent->GetOwnerId();
				Entity* owner = TryGetEntity(ownerId);
				if (owner == nullptr || owner->IsPendingDeletion())
				{
					DestroyEntity(ownerId);
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
				const EntityId ownerId = component->GetOwnerId();
				if (Transform* transform = GetComponent<Transform>(ownerId); transform != nullptr)
				{
					transform->UpdateInterpolatedPosition(r_interpolate._val.b);
				}
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

				const EntityId ownerId = updateComponent->GetOwnerId();
				Entity* owner = TryGetEntity(ownerId);
				if (owner == nullptr || owner->IsPendingDeletion())
				{
					DestroyEntity(ownerId);
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
			auto font = g_pEnv->GetUIManager().GetRenderer()->_style.font;
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
		bool IsMaterialTransparent(const Material* material)
		{
			if (!material)
				return false;

			if (material->_properties.hasTransparency == 1 || material->_properties.isWater == 1)
				return true;

			return material->GetBlendState() != BlendState::Opaque;
		}

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

			const bool isTransparency = (flags & MeshRenderFlags::MeshRenderTransparency) != 0;
			BlendState effectiveBlendState = material->GetBlendState();
			if (isTransparency)
			{
				if (effectiveBlendState == BlendState::Opaque || effectiveBlendState == BlendState::Transparency)
				{
					effectiveBlendState = BlendState::TransparencyPreserveAlpha;
				}
			}
			const DepthBufferState effectiveDepthState = isTransparency && material->GetDepthState() == DepthBufferState::DepthDefault
				? DepthBufferState::DepthRead
				: material->GetDepthState();

			material->SaveRenderState();
			graphicsDevice->SetBlendState(effectiveBlendState);
			graphicsDevice->SetDepthBufferState(effectiveDepthState);
			graphicsDevice->SetCullingMode(isShadowMap ? shadowCullMode : material->GetCullMode());

			mesh->UpdateConstantBuffer(nullptr, math::Matrix::Identity, material, instanceId, isTransparency);

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
			PROFILE();

			//std::unique_lock lock(_lock);
			const auto& pvsRenderables = pvs->GetRenderables();			
			snapshot.clear();
			snapshot.reserve(pvsRenderables.size());

			for (const auto& renderableBatch : pvsRenderables)
			{
				PROFILE();

				auto material = renderableBatch.first;
				if (!material)
					continue;

				auto& batch = snapshot.emplace_back(material, std::vector<RenderableSnapshot>()).second;
				batch.reserve(renderableBatch.second.size());

				for (const auto& meshEntityPair : renderableBatch.second)
				{
					PROFILE();

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

		if (isTransparency)
		{
			struct TransparentRenderItem
			{
				std::shared_ptr<Material> material;
				RenderableSnapshot* renderable = nullptr;
				float distSq = 0.0f;
			};

			std::vector<TransparentRenderItem> transparentItems;
			transparentItems.reserve(256);
			const math::Vector3 cameraPosition = GetMainCamera()->GetEntity()->GetPosition();

			for (auto& batch : snapshot)
			{
				auto material = batch.first;

				for (auto& renderable : batch.second)
				{
					totalCandidates++;

					auto mesh = renderable.mesh;
					auto instance = renderable.instance;

					if (!mesh || !instance)
					{
						skippedNullMeshOrInstance++;
						continue;
					}

					if (!IsMaterialTransparent(material.get()))
					{
						skippedTransparencyGate++;
						continue;
					}

					if ((layerMask & LAYERMASK(renderable.layer)) == 0)
					{
						skippedLayerMask++;
						continue;
					}

					if (mesh->GetLodLevel() != -1)
					{
						const float lodPartitions = r_lodPartition._val.f32;
						const float minDistance = lodPartitions * static_cast<float>(mesh->GetLodLevel());
						const float maxDistance = lodPartitions * static_cast<float>(mesh->GetLodLevel() + 1);
						const float distance = (renderable.instanceData.worldMatrix.Translation() - cameraPosition).Length();

						if (mesh->GetLodLevel() < 3)
						{
							if (distance < minDistance || distance > maxDistance)
							{
								skippedLod++;
								continue;
							}
						}
						else if (distance < minDistance)
						{
							skippedLod++;
							continue;
						}
					}

					const math::Vector3 objectPosition = renderable.instanceData.worldMatrix.Translation();
					TransparentRenderItem item;
					item.material = material;
					item.renderable = &renderable;
					item.distSq = (objectPosition - cameraPosition).LengthSquared();
					transparentItems.push_back(item);
				}
			}

			std::sort(
				transparentItems.begin(),
				transparentItems.end(),
				[](const TransparentRenderItem& left, const TransparentRenderItem& right)
				{
					return left.distSq > right.distSq;
				});

			for (auto& item : transparentItems)
			{
				auto* renderable = item.renderable;
				if (!renderable)
					continue;

				auto material = item.material;
				auto mesh = renderable->mesh;
				auto* instance = renderable->instance;

				if (!mesh || !instance || !material)
					continue;

				instance->Start();
				if (!PrepareMeshRender(mesh.get(), material.get(), renderFlags, _drawnEntities, renderable->shadowCullMode))
				{
					instance->Finish();
					skippedPrepareRender++;
					continue;
				}

				if (renderable->layer == Layer::Sky)
				{
					renderable->instanceData.worldMatrix = renderable->entity->GetWorldTMTranspose();
					renderable->instanceData.worldMatrixPrev = renderable->entity->GetWorldTMPrevTranspose();
					renderable->instanceData.worldMatrixInverseTranspose = renderable->entity->GetWorldTMInvert();
				}

				instance->Render(renderable->instanceData);
				instance->Finish();
				g_pEnv->_graphicsDevice->DrawIndexedInstanced(instance->GetMesh()->GetNumIndices(), 1);
				material->RestoreRenderState();

				++drawnInstancesTotal;
				++_drawCalls;
			}
		}
		else
		{
			for (auto it = snapshot.begin(); it != snapshot.end(); it++)
			{
				auto material = it->first;

				bool rendered = false;

				int drawnInstances = 0;
				MeshInstance* currentInstance = nullptr;
				MeshInstance* lastInstance = nullptr;

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

					if (!IsMaterialTransparent(material.get()))
					{
						if (material->DoesHaveAnyReflectivity())
							_didAnyDrawnItemReflect = true;
					}
					else
					{
						skippedTransparencyGate++;
						continue;
					}

					if (currentInstance != lastInstance && lastInstance != nullptr || renderable.hasAnimations)
					{
						if (isNormalRender)
							++_drawCalls;

						if (isShadowMap)
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
						const float lodPartitions = r_lodPartition._val.f32;

						float minDistance = lodPartitions * (float)(mesh->GetLodLevel());
						float maxDistance = lodPartitions * (float)(mesh->GetLodLevel() + 1);

						float distance = (renderable.instanceData.worldMatrix.Translation() - GetMainCamera()->GetEntity()->GetPosition()).Length();

						if (mesh->GetLodLevel() < 3)
						{
							if (distance < minDistance || distance > maxDistance)
							{
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
		RebuildEntityViewCache();
		return _entities;
	}

	bool Scene::GetEntities(const ComponentSignature signature, std::vector<Entity*>& entities)
	{
		std::unique_lock lock(_lock);
		if (signature == 0)
			return false;

		if ((signature & (signature - 1)) == 0)
		{
			ComponentId componentId = 0;
			ComponentSignature mask = signature;
			while ((mask >>= 1) != 0)
			{
				++componentId;
			}

			if (const auto* pool = TryGetComponentPool(componentId); pool != nullptr)
			{
				entities.reserve(entities.size() + pool->owners.size());
				for (const EntityId owner : pool->owners)
				{
					if (!IsValid(owner))
						continue;

					Entity* entity = _entitySlots[owner.index].entity;
					if (entity != nullptr)
						entities.push_back(entity);
				}
				return !entities.empty();
			}
		}

		for (const EntityId id : _liveEntities)
		{
			if (!IsValid(id))
				continue;

			Entity* entity = _entitySlots[id.index].entity;
			if (entity == nullptr)
				continue;

			if ((entity->GetComponentSignature() & signature) != 0)
			{
				entities.push_back(entity);
			}
		}
		
		return entities.size() > 0;
	}

	bool Scene::GetLiveEntityIds(std::vector<EntityId>& entityIds)
	{
		std::unique_lock lock(_lock);
		entityIds.insert(entityIds.end(), _liveEntities.begin(), _liveEntities.end());
		return !entityIds.empty();
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
		std::unique_lock lock(_lock);
		if (const auto* pool = TryGetComponentPool(id); pool != nullptr)
			return static_cast<uint32_t>(pool->components.size());
		return 0;
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
		std::unique_lock lock(_lock);

		min = math::Vector3(FLT_MAX);
		max = math::Vector3(FLT_MIN);

		const auto* pool = TryGetComponentPool(StaticMeshComponent::_GetComponentId());
		if (pool != nullptr)
		{
			for (const EntityId ownerId : pool->owners)
			{
				Entity* ent = TryGetEntity(ownerId);
				if (ent == nullptr || ent->IsPendingDeletion())
					continue;

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

		const auto* pool = TryGetComponentPool(StaticMeshComponent::_GetComponentId());
		if (pool == nullptr)
			return false;

		outComponents.reserve(pool->components.size());
		for (uint32_t denseIndex = 0; denseIndex < pool->components.size(); ++denseIndex)
		{
			auto* component = static_cast<StaticMeshComponent*>(pool->components[denseIndex]);
			if (component == nullptr || component->GetMesh() == nullptr)
				continue;

			Entity* entity = TryGetEntity(pool->owners[denseIndex]);
			if (entity == nullptr || entity->IsPendingDeletion())
				continue;

			if (entity->HasFlag(EntityFlags::DoNotRender))
				continue;

			const Layer layer = entity->GetLayer();

			if (layer == Layer::Sky || layer == Layer::Invisible)
				continue;

			if (!includeDynamic)
			{
				
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
		std::unique_lock lock(_lock);
		numFaces = 0;

		const auto* pool = TryGetComponentPool(StaticMeshComponent::_GetComponentId());
		if (pool != nullptr)
		{
			for (uint32_t denseIndex = 0; denseIndex < pool->components.size(); ++denseIndex)
			{
				auto* smc = static_cast<StaticMeshComponent*>(pool->components[denseIndex]);
				if (smc == nullptr)
					continue;

				auto mesh = smc->GetMesh();

				if (!mesh)
					continue;

				Entity* entity = TryGetEntity(pool->owners[denseIndex]);

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
		std::unique_lock lock(_lock);
		numFaces = 0;

		const auto* pool = TryGetComponentPool(StaticMeshComponent::_GetComponentId());
		if (pool != nullptr)
		{
			uint32_t indexOffset = 0;

			for (uint32_t denseIndex = 0; denseIndex < pool->components.size(); ++denseIndex)
			{
				auto* smc = static_cast<StaticMeshComponent*>(pool->components[denseIndex]);
				if (smc == nullptr)
					continue;

				auto mesh = smc->GetMesh();

				if (!mesh)
					continue;

				Entity* entity = TryGetEntity(pool->owners[denseIndex]);

				if (!entity)
					continue;

				if (entity->HasFlag(excludeFlags))
					continue;

				auto verts = mesh->GetVertices();
				auto inds = mesh->GetIndices();

				numFaces += mesh->GetNumFaces();

				const auto& worldTM = entity->GetWorldTM();

				for (auto& v : verts)
				{
					const math::Vector3 localPos = *(math::Vector3*)&v._position.x;
					vertices.push_back(math::Vector3::Transform(localPos, worldTM));
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
