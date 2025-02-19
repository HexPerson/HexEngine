
#include "Entity.hpp"
#include "Component\Transform.hpp"
#include "../HexEngine.hpp"
#include "Component\StaticMeshComponent.hpp"
#include "Component\FirstPersonCameraController.hpp"
#include "Component\RTSCameraController.hpp"
#include "Component\PointLight.hpp"

namespace HexEngine
{
	Entity::Entity(Scene* scene) :
		_scene(scene)
	{
		SetLayer(Layer::StaticGeometry);
		SetFlag(EntityFlags::PreviousTransformDirty);

		//_cachedTransform = AddComponent<Transform>();
	}

	Entity::~Entity()
	{
		Destroy();
	}

	void Entity::Create()
	{
		SetFlag(EntityFlags::HasBeenCreated);
	}

	Scene* Entity::GetScene() const
	{
		return _scene;
	}

	void Entity::SetName(const std::string& name)
	{
		_name = name;
	}

	const std::string& Entity::GetName() const
	{
		return _name;
	}

	bool Entity::HasFlag(EntityFlags flags) const
	{
		return (_flags & flags) != EntityFlags::None;
	}

	void Entity::SetFlag(EntityFlags flags)
	{
		_flags |= flags;
	}

	void Entity::ClearFlags(EntityFlags flags)
	{
		_flags &= ~flags;
	}

	void Entity::DeleteMe()
	{
		SetFlag(EntityFlags::WantsDeletion);

		for (auto&& child : _children)
		{
			child->SetFlag(EntityFlags::WantsDeletion);
			child->SetParent(nullptr);
		}

		// if this entity doesn't have an UpdateComponent, give it one so it can be deleted
		if (HasA<UpdateComponent>() == false)
		{
			AddComponent<UpdateComponent>();
		}
	}

	bool Entity::IsValid() const
	{
		return !HasFlag(EntityFlags::WantsDeletion) && !HasFlag(EntityFlags::IsPendingRemoval);
	}

	bool Entity::IsCreated() const
	{
		return HasFlag(EntityFlags::HasBeenCreated);
	}

	bool Entity::IsPendingDeletion() const
	{
		if (HasFlag(EntityFlags::WantsDeletion))
			return true;

		if (_parent)
			return _parent->IsPendingDeletion();

		return false;
	}

	Layer Entity::GetLayer() const
	{
		return _layer;
	}

	void Entity::SetLayer(Layer layer)
	{
		_layer = layer;
	}

	int32_t Entity::GetTag() const
	{
		return _tag;
	}

	void Entity::SetTag(int32_t tag)
	{
		_tag = tag;
	}

	Entity* Entity::GetParent()
	{
		return _parent;
	}

	void Entity::SetParent(Entity* parent)
	{
		// if it already has a parent, notify that parent that it is no longer the parent
		if (_parent != nullptr)
		{
			EntityParentChangedMessage message(this, _parent, EntityParentChangedMessage::Flags::NoLongerParent);
			_parent->OnMessage(&message, this);
		}

		_parent = parent;

		if (parent != nullptr)
		{
			_parent->_children.push_back(this);

			EntityParentChangedMessage message(this, _parent, EntityParentChangedMessage::Flags::BecameParent);
			_parent->OnMessage(&message, this);
		}

		_hasCachedWorldTM = false;
		_hasCachedWorldAABB = false;
		_hasCachedWorldBoundingSphere = false;
		_hasCachedWorldOBB = false;
		_hasCachedWorldTMTranspose = false;
	}

	const std::vector<Entity*>& Entity::GetChildren() const
	{
		return _children;
	}

	void Entity::Destroy()
	{
		// if this entity has a rigidbody component, we have to sycnhronize with the physics thread otherwise we will destroy components in use while the physics thread runs
		/*if (HasA<RigidBody>())
		{
			g_pEnv->_physicsSystem->LockWrite();
		}*/

		for (auto&& component : _components)
		{
			LOG_DEBUG("Destroying component with ID %d", component.id);

			RemoveComponent(component.component);
		}

		_components.clear();

		/*if (HasA<RigidBody>())
		{
			g_pEnv->_physicsSystem->UnlockWrite();
		}*/

		for (auto& child : _children)
		{
			_scene->DestroyEntity(child);
		}
		_children.clear();
	}

	void Entity::RemoveComponent(BaseComponent* component)
	{
		if (auto existingComponent = GetComponentByID(component->GetComponentId()); existingComponent == nullptr)
		{
			LOG_CRIT("An entity is trying to remove a component but does not have one registered!");
			return;
		}

		ComponentSignature previousSignature = _componentsSignature;

		_componentsSignature = previousSignature & ~(1 << component->GetComponentId());		

		_scene->OnEntityRemoveComponent(this, previousSignature, component);

		component->Destroy();
		delete component;
	}

	void Entity::RemoveComponentById(ComponentId id)
	{
		BaseComponent* existingComponent;

		if (existingComponent = GetComponentByID(id); existingComponent == nullptr)
		{
			LOG_CRIT("An entity is trying to remove a component but does not have one registered!");
			return;
		}

		ComponentSignature previousSignature = _componentsSignature;

		_componentsSignature = previousSignature & ~(1 << id);		

		for (auto it = _components.begin(); it != _components.end(); it++)
		{
			if (it->id == id)
			{
				_components.erase(it);
				break;
			}
		}

		_scene->OnEntityRemoveComponent(this, previousSignature, existingComponent);

		existingComponent->Destroy();
		delete existingComponent;
	}

	BaseComponent* Entity::AddComponent(BaseComponent* component)
	{
		/*if (auto existingComponent = GetComponentByID(component->GetComponentId()); existingComponent != nullptr)
		{
			LOG_WARN("An entity is trying to add a %s when it already has one registered!", component->GetComponentName().c_str());
			return existingComponent;
		}*/

		if (component->GetComponentId() == StaticMeshComponent::_GetComponentId())
		{
			_cachedMeshRenderer = (StaticMeshComponent*)component;
		}
		else if (component->GetComponentId() == Transform::_GetComponentId())
		{
			_cachedTransform = (Transform*)component;
		}

		ComponentSignature previousSignature = _componentsSignature;

		_components.push_back({ component->GetComponentId(), component });

		ComponentId newCompId = component->GetComponentId();

		_componentsSignature |= (1 << newCompId);

		// strip off all the other components signatures first otherwise we will get overlap
		/*for (auto& comp : _components)
		{
			if (comp.component != component)
				_componentsSignature &= ~(1 << comp.id);
		}*/

		// Does this component inherit from UpdateComponent? if so it should have its signature adjusted so it can be updated
		if (component->CastAs<UpdateComponent>() != nullptr)
		{
			_componentsSignature |= (1 << UpdateComponent::_GetComponentId());
		}

		_scene->OnEntityAddComponent(this, previousSignature, component);

		return component;
	}

	BaseComponent* Entity::GetComponentByID(const ComponentId& id)
	{
		for (auto&& component : _components)
		{
			if (component.id == id)
				return component.component;
		}

		return nullptr;
	}

	BaseComponent* Entity::GetComponentByClassName(const std::string& name)
	{
		for (auto&& component : _components)
		{
			if (component.component->GetComponentName() == name)
				return component.component;
		}

		return nullptr;
	}

	std::vector<BaseComponent*> Entity::GetComponentsBySignature(const ComponentSignature& signature)
	{
		std::vector<BaseComponent*> result;

		for (auto&& component : _components)
		{
			if ((signature & (1 << component.id)) != 0)
				result.push_back(component.component);
		}

		return result;
	}

	const ComponentSignature& Entity::GetComponentSignature() const
	{
		return _componentsSignature;
	}

	void Entity::DebugRender()
	{
		auto layer = GetLayer();

		if (layer != Layer::Invisible || layer == Layer::Trigger)
		{
			math::Color debugColour;
			switch (layer)
			{
			default:
				debugColour = math::Color(0, 1, 0, 0.4f); break;

			case Layer::Trigger:
				debugColour = math::Color(1.0f, 0.1f, 0.1f, 0.4f); break;
			}

			auto worldAABB = GetWorldAABB();
			worldAABB.Extents.x *= 1.1f;
			worldAABB.Extents.y *= 1.1f;
			worldAABB.Extents.z *= 1.1f;

			g_pEnv->_debugRenderer->DrawOBB(GetWorldOBB(), debugColour);
			g_pEnv->_debugRenderer->DrawAABB(worldAABB, math::Color(1.0f, 0, 0, 0.6f));

			g_pEnv->_debugRenderer->DrawLine(GetPosition(), GetPosition() + _cachedTransform->GetForward() * 20.0f, math::Color(0, 0, 1, 1));
			g_pEnv->_debugRenderer->DrawLine(GetPosition(), GetPosition() + _cachedTransform->GetRight() * 20.0f, math::Color(1, 0, 0, 1));
			g_pEnv->_debugRenderer->DrawLine(GetPosition(), GetPosition() + _cachedTransform->GetUp() * 20.0f, math::Color(0, 1, 0, 1));
		}

		for (auto& comp : _components)
		{
			comp.component->DebugRender();
		}
	}

	void Entity::ForcePosition(const math::Vector3& position)
	{
		ClearTransformCache();

		_cachedTransform->SetPosition(position);

		if (auto body = GetComponent<RigidBody>(); body != nullptr && body->GetIRigidBody() != nullptr && body->GetIRigidBody()->GetBodyType() == IRigidBody::BodyType::Static)
		{
			body->GetIRigidBody()->UpdatePosePosition(GetWorldTM().Translation());
		}

		//SetPosition(position);
	}

	void Entity::ForceRotation(const math::Quaternion& rotation)
	{
		ClearTransformCache();

		_cachedTransform->SetRotation(rotation);

		if (auto body = GetComponent<RigidBody>(); body != nullptr && body->GetIRigidBody() != nullptr && body->GetIRigidBody()->GetBodyType() == IRigidBody::BodyType::Static)
		{
			body->GetIRigidBody()->UpdatePoseRotation(GetRotation());
		}

		//SetPosition(position);
	}

	void Entity::SetPosition(const math::Vector3& position)
	{
		_cachedTransform->SetPosition(position);
	}

	const math::Vector3& Entity::GetPosition() const
	{
		return _cachedTransform->GetPosition();
	}

	/*const math::Vector3 Entity::GetRenderPosition() const
	{
		return _cachedTransform->GetRenderPosition();
	}*/

	void Entity::SetRotation(const math::Quaternion& rotation)
	{
		_cachedTransform->SetRotation(rotation);
	}

	const math::Quaternion& Entity::GetRotation() const
	{
		return _cachedTransform->GetRotation();
	}

	/*const math::Quaternion Entity::GetRenderRotation() const
	{
		return _cachedTransform->GetRenderRotation();
	}*/

	const math::Matrix& Entity::GetWorldTM()
	{
		if (!_hasCachedWorldTM)
		{
			auto transform = GetLocalTM();

			//math::Vector3 position = transform->GetRenderPosition();

			auto parent = GetParent();

			while (parent)
			{
				transform *= parent->GetLocalTM();
				parent = parent->GetParent();
			}
				//position += GetParent()->GetComponent<Transform>()->GetRenderPosition();

			//position += _renderOffset;

			//_cachedWorldTM = math::Matrix::CreateScale(transform->GetScale()) * math::Matrix::CreateWorld(transform->GetPosition(), transform->GetForward() /*math::Vector3::Transform(math::Vector3::Forward, transform->GetRotation())*/, math::Vector3::Up);

			//_cachedWorldTM = math::Matrix::CreateScale(transform->GetScale()) * math::Matrix::CreateWorld(transform->GetPosition(), math::Vector3::Transform(math::Vector3::Forward, transform->GetRotation()), math::Vector3::Up);

			if (HasFlag(EntityFlags::PreviousTransformDirty))
			{
				_cachedWorldTMPrev = transform;
				ClearFlags(EntityFlags::PreviousTransformDirty);
			}
			else
				_cachedWorldTMPrev = _cachedWorldTM;

			
			_cachedWorldTM = transform;// math::Matrix::CreateScale(transform->GetScale())* math::Matrix::CreateFromQuaternion(transform->GetRenderRotation())* math::Matrix::CreateWorld(position, math::Vector3::Forward, math::Vector3::Up);

			

			_hasCachedWorldTM = true;
		}

		return _cachedWorldTM;
	}

	const math::Matrix& Entity::GetWorldTMPrev()
	{
		return _cachedWorldTMPrev;
	}

	const math::Matrix& Entity::GetWorldTMTranspose()
	{
		if (!_hasCachedWorldTMTranspose)
		{
			_cachedWorldTMTranspose = GetWorldTM().Transpose();

			_hasCachedWorldTMTranspose = true;
		}

		return _cachedWorldTMTranspose;
	}

	const math::Matrix& Entity::GetLocalTM()
	{
		if (!_hasCachedLocalTM)
		{
			auto transform = _cachedTransform;

			//_cachedLocalTM = math::Matrix::CreateScale(transform->GetScale()) * math::Matrix::CreateWorld(math::Vector3(), math::Vector3::Transform(math::Vector3::Forward, transform->GetRotation()), math::Vector3::Up);

			_cachedLocalTM = math::Matrix::CreateScale(transform->GetScale()) * math::Matrix::CreateFromQuaternion(transform->GetRotation(TransformState::Interpolated)) * math::Matrix::CreateTranslation(transform->GetPosition(TransformState::Interpolated));

			_hasCachedLocalTM = true;
		}

		return _cachedLocalTM;
	}

	void Entity::SetScale(const math::Vector3& scale)
	{
		_cachedTransform->SetScale(scale);
	}

	const math::Vector3& Entity::GetScale() const
	{
		return _cachedTransform->GetScale();
	}

	const dx::BoundingBox Entity::GetAABB()
	{
		auto aabb = _aabb;

		math::Vector3 absoluteScale = GetScale();
		Entity* parent = GetParent();

		while (parent)
		{
			absoluteScale *= parent->GetScale();
			parent = parent->GetParent();
		}

		aabb.Transform(aabb, math::Matrix::CreateScale(absoluteScale));
		aabb.Transform(aabb, math::Matrix::CreateFromQuaternion(GetRotation()));

		return aabb;
	}

	const dx::BoundingOrientedBox Entity::GetOBB()
	{
		auto obb = _obb;

		math::Vector3 absoluteScale = GetScale();
		Entity* parent = GetParent();

		while (parent)
		{
			absoluteScale *= parent->GetScale();
			parent = parent->GetParent();
		}

		obb.Transform(obb, math::Matrix::CreateScale(absoluteScale));
		obb.Transform(obb, math::Matrix::CreateFromQuaternion(GetRotation()));

		return obb;
	}

	void Entity::SetAABB(const dx::BoundingBox& bbox)
	{
		//dx::BoundingBox scaledBB;

		/*math::Vector3 absoluteScale = GetScale();
		Entity* parent = GetParent();

		while (parent)
		{
			absoluteScale *= parent->GetScale();
			parent = parent->GetParent();
		}*/

		_aabb = bbox;
		//_aabb.Transform(scaledBB, math::Matrix::CreateScale(absoluteScale));

		// did it recieve an occlision volume yet? if not set one
		if (_occlusionVolume.Extents.x == 0.0f && _occlusionVolume.Extents.y == 0.0f && _occlusionVolume.Extents.z == 0.0f)
			SetOcclusionVolume(bbox);

		_hasCachedWorldAABB = false;
		_hasCachedWorldBoundingSphere = false;

		dx::BoundingSphere::CreateFromBoundingBox(_boundingSphere, bbox);
	}

	void Entity::SetOBB(const dx::BoundingOrientedBox& obb)
	{
		dx::BoundingOrientedBox scaledBB;

		/*math::Vector3 absoluteScale = GetScale();
		Entity* parent = GetParent();

		while (parent)
		{
			absoluteScale *= parent->GetScale();
			parent = parent->GetParent();
		}	*/	

		_obb = obb;
		//_obb.Transform(_obb, math::Matrix::CreateScale(absoluteScale));

		_hasCachedWorldOBB = false;
	}

	void Entity::SetOcclusionVolume(const dx::BoundingBox& volume)
	{
		_occlusionVolume = volume;

		_hasCachedWorldOcclusionVolume = false;
	}

	const dx::BoundingBox& Entity::GetOcclusionVolume() const
	{
		return _occlusionVolume;
	}

	const dx::BoundingBox Entity::GetWorldOcclusionVolume()
	{
		if (_hasCachedWorldOcclusionVolume == false)
		{
			_cachedWorldOcclusionVolume = GetOcclusionVolume();

			auto position = _cachedTransform->GetPosition();

			_cachedWorldOcclusionVolume.Center.x += position.x;
			_cachedWorldOcclusionVolume.Center.y += position.y;
			_cachedWorldOcclusionVolume.Center.z += position.z;

			_hasCachedWorldOcclusionVolume = true;
		}

		return _cachedWorldOcclusionVolume;
	}

	const dx::BoundingSphere& Entity::GetBoundingSphere() const
	{
		return _boundingSphere;
	}

	const dx::BoundingSphere Entity::GetWorldBoundingSphere()
	{
		if (_hasCachedWorldBoundingSphere == false)
		{
			_cachedWorldBoundingSphere = GetBoundingSphere();

			auto position = _cachedTransform->GetPosition();

			_cachedWorldBoundingSphere.Center.x += position.x;
			_cachedWorldBoundingSphere.Center.y += position.y;
			_cachedWorldBoundingSphere.Center.z += position.z;

			_hasCachedWorldBoundingSphere = true;
		}

		return _cachedWorldBoundingSphere;
	}

	const dx::BoundingBox& Entity::GetWorldAABB()
	{
		if (!_hasCachedWorldAABB)
		{
			_cachedWorldAABB = GetAABB();

			const auto& translation = GetWorldTM().Translation();
			_cachedWorldAABB.Center.x += translation.x;
			_cachedWorldAABB.Center.y += translation.y;
			_cachedWorldAABB.Center.z += translation.z;

			_hasCachedWorldAABB = true;
		}

		return _cachedWorldAABB;
	}

	const dx::BoundingOrientedBox& Entity::GetWorldOBB()
	{
		if (!_hasCachedWorldOBB)
		{
			_cachedWorldOBB = GetOBB();
			//_cachedWorldOBB.Transform(_cachedWorldOBB, GetWorldTM());

			const auto& translation = GetWorldTM().Translation();
			_cachedWorldOBB.Center.x += translation.x;
			_cachedWorldOBB.Center.y += translation.y;
			_cachedWorldOBB.Center.z += translation.z;

			_hasCachedWorldOBB = true;
		}

		return _cachedWorldOBB;
	}

	void Entity::ClearTransformCache()
	{
		_hasCachedLocalTM = false;
		_hasCachedWorldTM = false;
		_hasCachedWorldAABB = false;
		_hasCachedWorldOBB = false;
		_hasCachedWorldTMTranspose = false;
		_hasCachedWorldBoundingSphere = false;
	}

	void Entity::OnMessage(Message* message, MessageListener* sender)
	{
		switch (message->_id)
		{
		case MessageId::TransformChanged:
		{
			auto transformMessage = message->CastAs<TransformChangedMessage>();

			if ((transformMessage->_flags & (TransformChangedMessage::ChangeFlags::ScaleChanged | TransformChangedMessage::ChangeFlags::RotationChanged)) != 0)
			{
				_hasCachedLocalTM = false;
				_hasCachedWorldTM = false;
				_hasCachedWorldAABB = false;
				_hasCachedWorldOBB = false;
				_hasCachedWorldTMTranspose = false;
				_hasCachedWorldBoundingSphere = false;
			}

			if ((transformMessage->_flags & TransformChangedMessage::ChangeFlags::PositionChanged) == TransformChangedMessage::ChangeFlags::PositionChanged)
			{
				_hasCachedWorldTM = false;
				_hasCachedLocalTM = false;
				_hasCachedWorldAABB = false;
				_hasCachedWorldTMTranspose = false;
				_hasCachedWorldBoundingSphere = false;
				_hasCachedWorldOBB = false;
			}

			break;
		}

		case MessageId::EntityDestroyed:
		{
			auto destroyMessage = message->CastAs<EntityDestroyedMessage>();

			auto it = std::find(_children.begin(), _children.end(), destroyMessage->_entity);

			if (it != _children.end())
			{
				LOG_DEBUG("Removing child '%s' from parent '%s'", destroyMessage->_entity->GetName().c_str(), GetName().c_str());

				_children.erase(it);
			}
			break;

		}
		}

		for (auto&& component : _components)
		{
			if ((void*)component.component != sender)
				component.component->OnMessage(message, sender);
		}

		// also send to all children
		for (auto&& child : _children)
		{
			if(sender != this)
			child->OnMessage(message, sender);
		}
	}

	void Entity::SetCastsShadows(bool castsShadows)
	{
		_canCastShadows = castsShadows;
	}

	bool Entity::GetCastsShadows()
	{
		return _canCastShadows;
	}

	void Entity::Serialize(json& data, JsonFile* file)
	{
		json& entData = data[GetName()];
		
		entData["layer"] = GetLayer();

		//int numComponents = (int)_components.size();
		//file->Write(&numComponents, sizeof(int));

		json& compsData = entData["components"];

		// now save the components
		//
		for (auto& component : _components)
		{
			json compData = json::object();

			compData["name"] = component.component->GetComponentName();
			//json& compData = compsData[component.component->GetComponentName()];

			component.component->Serialize(compData, file);

			compsData.push_back(compData);

			LOG_DEBUG("Saved component '%s::%s'", GetName().c_str(), component.component->GetComponentName().c_str())
		}
	}

	Entity* Entity::LoadFromFile(json& data, const std::string& name, Scene* scene, JsonFile* file)
	{
		LOG_DEBUG("Loading entity '%s' from file", name.c_str());

		Entity* entity = scene->CreateEntity(name);

		if (entity == nullptr)
			return nullptr;

		//entity->Deserialize(data, file);

		return entity;
	}

	void Entity::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		Layer layer = data["layer"].get<Layer>();

		SetLayer(layer);

		for(auto& comp : data["components"].items())
		{
			std::string componentName = comp.value()["name"];

			LOG_DEBUG("Loading component id %s", componentName.c_str());

			BaseComponent* component = GetComponentByClassName(componentName);			

			// we only should add one of each component
			if (component == nullptr)
			{
				for (auto& cls : g_pEnv->_classRegistry->GetAllClasses())
				{
					if (cls.second.name == componentName)
					{
						component = cls.second.newInstanceFn(this);
						AddComponent(component);
						break;
					}
				}
			}

			if (!component)
			{
				LOG_CRIT("Unhandled component load: %s", componentName.c_str());
				return;
			}

			if (mask != 0 && ((1 << component->GetComponentId()) & mask) != mask)
				continue;

			component->Deserialize(comp.value(), file, mask);
		}
	}
}