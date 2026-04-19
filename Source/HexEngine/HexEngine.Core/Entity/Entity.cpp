
#include "Entity.hpp"
#include "Component\Transform.hpp"
#include "../HexEngine.hpp"
#include "Component\StaticMeshComponent.hpp"
#include "Component\FirstPersonCameraController.hpp"
#include "Component\RTSCameraController.hpp"
#include "Component\PointLight.hpp"

namespace HexEngine
{
	namespace
	{
		std::string GeneratePrefabNodeId()
		{
			static std::atomic<uint64_t> s_counter = 1;
			const uint64_t counter = s_counter.fetch_add(1, std::memory_order_relaxed);
			const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();

			std::ostringstream ss;
			ss << std::hex << now << "_" << counter;
			return ss.str();
		}
	}

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

	EntityFlags Entity::GetFlags() const
	{
		return _flags;
	}

	void Entity::DeleteMe(bool broadcast)
	{
		SetFlag(EntityFlags::IsPendingRemoval);

		if (broadcast)
		{
			// Notify all entities first
			EntityDestroyedMessage message(this);
			BroadcastMessage(&message);
		}

		// During destruction we can end up with stale parent pointers in complex
		// teardown orders. Detach children directly instead of calling SetParent,
		// which dereferences the previous parent to update its child list.
		while (!_children.empty())
		{
			auto* child = _children.back();
			_children.pop_back();

			child->SetFlag(EntityFlags::IsPendingRemoval);
			if (child->_parent == this)
			{
				child->_parent = nullptr;
			}

			child->_hasCachedWorldTM = false;
			child->_hasCachedWorldAABB = false;
			child->_hasCachedWorldBoundingSphere = false;
			child->_hasCachedWorldOBB = false;
			child->_hasCachedWorldTMTranspose = false;
			child->_hasCachedWorldTMInvert = false;
		}

		// if this entity doesn't have an UpdateComponent, give it one so it can be deleted
		if (HasA<UpdateComponent>() == false)
		{
			AddComponent<UpdateComponent>();
		}
	}

	bool Entity::IsValid() const
	{
		return !HasFlag(EntityFlags::IsPendingRemoval);
	}

	bool Entity::IsCreated() const
	{
		return HasFlag(EntityFlags::HasBeenCreated);
	}

	bool Entity::IsPendingDeletion() const
	{
		if (HasFlag(EntityFlags::IsPendingRemoval))
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

	Entity* Entity::GetParent() const
	{
		return _parent;
	}

	void Entity::SetChunk(Chunk* chunk)
	{
		_lastChunk = chunk;
	}

	Chunk* Entity::GetChunk() const
	{
		return _lastChunk;
	}

	void Entity::SetParent(Entity* parent)
	{
		if (parent == this)
		{
			LOG_WARN("Cannot parent entity '%s' to itself", GetName().c_str());
			return;
		}

		if (_parent == parent)
			return;

		// Preserve current world-space position across reparent operations by
		// converting it back into the new parent's local space after attaching.
		const math::Vector3 worldPositionBeforeReparent = GetWorldTM().Translation();

		// if it already has a parent, notify that parent that it is no longer the parent
		if (_parent != nullptr)
		{
			auto* previousParent = _parent;
			previousParent->_children.erase(std::remove(previousParent->_children.begin(), previousParent->_children.end(), this), previousParent->_children.end());

			EntityParentChangedMessage message(this, previousParent, EntityParentChangedMessage::Flags::NoLongerParent);
			previousParent->OnMessage(&message, this);
		}

		_parent = parent;

		if (parent != nullptr)
		{
			if (std::find(_parent->_children.begin(), _parent->_children.end(), this) == _parent->_children.end())
				_parent->_children.push_back(this);

			EntityParentChangedMessage message(this, _parent, EntityParentChangedMessage::Flags::BecameParent);
			_parent->OnMessage(&message, this);
		}

		_hasCachedWorldTM = false;
		_hasCachedWorldAABB = false;
		_hasCachedWorldBoundingSphere = false;
		_hasCachedWorldOBB = false;
		_hasCachedWorldTMTranspose = false;
		_hasCachedWorldTMInvert = false;

		if (_cachedTransform != nullptr)
		{
			math::Vector3 localPosition = worldPositionBeforeReparent;
			if (_parent != nullptr)
			{
				localPosition = math::Vector3::Transform(worldPositionBeforeReparent, _parent->GetWorldTMInvert());
			}

			_cachedTransform->SetPosition(localPosition);
		}
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

		// RemoveComponent mutates _components, so tear down from the back until empty.
		while (!_components.empty())
		{
			auto* component = _components.back().component;
			const auto componentId = _components.back().id;
			LOG_DEBUG("Destroying component with ID %d", componentId);
			RemoveComponent(component);
		}

		/*if (HasA<RigidBody>())
		{
			g_pEnv->_physicsSystem->UnlockWrite();
		}*/

		// Child destruction can broadcast messages that also mutate _children.
		while (!_children.empty())
		{
			auto* child = _children.back();
			_children.pop_back();
			_scene->DestroyEntity(child);
		}
	}

	void Entity::RemoveComponent(BaseComponent* component)
	{
		if (component == nullptr)
		{
			return;
		}

		auto it = std::find_if(_components.begin(), _components.end(), [component](const auto& comp) {
			return comp.component == component;
			});

		if (it == _components.end())
		{
			LOG_CRIT("An entity is trying to remove a component but does not have one registered!");
			return;
		}

		const ComponentId componentId = it->id;

		if (componentId == Transform::_GetComponentId())
		{
			_cachedTransform = nullptr;
		}

		ComponentSignature previousSignature = _componentsSignature;

		_componentsSignature = previousSignature & ~(1 << componentId);

		auto* componentToDestroy = it->component;
		_components.erase(it);

		_scene->OnEntityRemoveComponent(this, previousSignature, componentToDestroy);

		componentToDestroy->Destroy();
		delete componentToDestroy;
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
		//auto layer = GetLayer();

		//if (layer != Layer::Invisible || layer == Layer::Trigger)
		//{
		//	math::Color debugColour;
		//	switch (layer)
		//	{
		//	default:
		//		debugColour = math::Color(0, 1, 0, 0.4f); break;

		//	case Layer::Trigger:
		//		debugColour = math::Color(1.0f, 0.1f, 0.1f, 0.4f); break;
		//	}

		//	auto worldAABB = GetWorldAABB();
		//	worldAABB.Extents.x *= 1.1f;
		//	worldAABB.Extents.y *= 1.1f;
		//	worldAABB.Extents.z *= 1.1f;

		//	g_pEnv->_debugRenderer->DrawOBB(GetWorldOBB(), debugColour);
		//	g_pEnv->_debugRenderer->DrawAABB(worldAABB, math::Color(1.0f, 0, 0, 0.6f));

		//	//g_pEnv->_debugRenderer->DrawLine(GetPosition(), GetPosition() + _cachedTransform->GetForward() * 20.0f, math::Color(0, 0, 1, 1));
		//	//g_pEnv->_debugRenderer->DrawLine(GetPosition(), GetPosition() + _cachedTransform->GetRight() * 20.0f, math::Color(1, 0, 0, 1));
		//	//g_pEnv->_debugRenderer->DrawLine(GetPosition(), GetPosition() + _cachedTransform->GetUp() * 20.0f, math::Color(0, 1, 0, 1));
		//}

		_isEditorGizmoHovered = false;

		for (auto& comp : _components)
		{
			comp.component->OnDebugRender();

			if (HasFlag(EntityFlags::SelectedInEditor))
			{
				bool hovering = false;
				comp.component->OnRenderEditorGizmo(true, hovering);
				_isEditorGizmoHovered |= hovering;
			}
		}
	}

	void Entity::ForcePosition(const math::Vector3& position)
	{
		_cachedTransform->SetPosition(position);

		if (auto body = GetComponent<RigidBody>(); body != nullptr && body->GetIRigidBody() != nullptr && body->GetIRigidBody()->GetBodyType() == IRigidBody::BodyType::Static)
		{
			body->GetIRigidBody()->UpdatePosePosition(GetWorldTM().Translation());
		}

		ClearTransformCache();
	}

	void Entity::ForceRotation(const math::Quaternion& rotation)
	{
		_cachedTransform->SetRotation(rotation);

		if (auto body = GetComponent<RigidBody>(); body != nullptr && body->GetIRigidBody() != nullptr && body->GetIRigidBody()->GetBodyType() == IRigidBody::BodyType::Static)
		{
			math::Quaternion worldRotation = GetRotation();
			for (auto* parent = GetParent(); parent != nullptr; parent = parent->GetParent())
			{
				worldRotation = worldRotation * parent->GetRotation();
				worldRotation.Normalize();
			}

			body->GetIRigidBody()->UpdatePoseRotation(worldRotation);
		}

		ClearTransformCache();
	}

	void Entity::SetPosition(const math::Vector3& position)
	{
		_cachedTransform->SetPosition(position);
	}

	const math::Vector3& Entity::GetPosition() const
	{
		if (_cachedTransform == nullptr)
		{
			return _lastPosition;
		}
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

			_cachedWorldTMPrevTranspose = _cachedWorldTMPrev.Transpose();

			
			_cachedWorldTM = transform;// math::Matrix::CreateScale(transform->GetScale())* math::Matrix::CreateFromQuaternion(transform->GetRenderRotation())* math::Matrix::CreateWorld(position, math::Vector3::Forward, math::Vector3::Up);

			

			_hasCachedWorldTM = true;
		}

		return _cachedWorldTM;
	}

	const math::Matrix& Entity::GetWorldTMPrev() const
	{
		return _cachedWorldTMPrev;
	}

	const math::Matrix& Entity::GetWorldTMPrevTranspose() const
	{
		return _cachedWorldTMPrevTranspose;
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

	const math::Matrix& Entity::GetWorldTMInvert()
	{
		if (!_hasCachedWorldTMInvert)
		{
			_cachedWorldTMInvert = GetWorldTM().Invert();

			_hasCachedWorldTMInvert = true;
		}

		return _cachedWorldTMInvert;
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

	void Entity::RecalculateBoundingVolumes(const dx::BoundingBox& aabb)
	{
		// Keep entity bounds in local mesh space.
		// World-space scale/rotation/translation is applied in GetWorldOBB/GetWorldAABB.
		_aabb = aabb;

		dx::BoundingOrientedBox::CreateFromBoundingBox(_obb, _aabb);

		dx::BoundingSphere::CreateFromBoundingBox(_boundingSphere, _aabb);

		_hasCachedWorldAABB = false;
		_hasCachedWorldOBB = false;
		_hasCachedWorldBoundingSphere = false;
	}

	math::Vector3 Entity::GetAbsoluteScale() const
	{
		math::Vector3 scale = GetScale();

		Entity* parent = GetParent();

		while (parent)
		{
			scale *= parent->GetScale();
			parent = parent->GetParent();
		}

		return scale;
	}

	const dx::BoundingBox& Entity::GetAABB()
	{
		return _aabb;
	}

	const dx::BoundingOrientedBox& Entity::GetOBB()
	{
		return _obb;
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

		dx::BoundingSphere::CreateFromBoundingBox(_boundingSphere, bbox);

		_hasCachedWorldAABB = false;
		_hasCachedWorldBoundingSphere = false;
		_hasCachedWorldBoundingSphere = false;		
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

			auto position = GetWorldTM().Translation();

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
			// Derive the world-space sphere from the world AABB so parent transforms
			// (rotation/scale) and non-centered mesh origins are fully accounted for.
			dx::BoundingSphere::CreateFromBoundingBox(_cachedWorldBoundingSphere, GetWorldAABB());

			_hasCachedWorldBoundingSphere = true;
		}

		return _cachedWorldBoundingSphere;
	}

	const dx::BoundingBox& Entity::GetWorldAABB()
	{
		if (!_hasCachedWorldAABB)
		{
			auto worldOBB = GetWorldOBB();

			math::Vector3 corners[8];
			worldOBB.GetCorners(corners);

			dx::BoundingBox::CreateFromPoints(_cachedWorldAABB, 8, corners, sizeof(math::Vector3));
			//_cachedWorldAABB = GetAABB();

			/*const auto& translation = GetWorldTM().Translation();

			_cachedWorldAABB.Center.x += translation.x;
			_cachedWorldAABB.Center.y += translation.y;
			_cachedWorldAABB.Center.z += translation.z;*/

			_hasCachedWorldAABB = true;
		}

		return _cachedWorldAABB;
	}

	const dx::BoundingOrientedBox& Entity::GetWorldOBB()
	{
		if (!_hasCachedWorldOBB)
		{
			_cachedWorldOBB = GetOBB();
			_cachedWorldOBB.Transform(_cachedWorldOBB, GetWorldTM());

			/*const auto& translation = GetWorldTM().Translation();
			_cachedWorldOBB.Center.x += translation.x;
			_cachedWorldOBB.Center.y += translation.y;
			_cachedWorldOBB.Center.z += translation.z;*/

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
		_hasCachedWorldTMInvert = false;
		++_transformVersion;

		if (auto mainCamera = _scene->GetMainCamera())
		{
			if (auto pvs = mainCamera->GetPVS())
			{
				pvs->UpdateEntityInstanceCache(this);
			}
		}

		if (auto sun = _scene->GetSunLight())
		{
			for (auto i = 0; i < sun->GetMaxSupportedShadowCascades(); ++i)
			{
				if (auto pvs = sun->GetPVS(i))
				{
					pvs->UpdateEntityInstanceCache(this);
				}
			}
		}
	}

	void Entity::OnMessage(Message* message, MessageListener* sender)
	{
		auto invalidateChildTransformCaches = [](Entity* root, auto&& invalidateRef) -> void
		{
			if (root == nullptr)
				return;

			for (auto* child : root->GetChildren())
			{
				if (child == nullptr || child->IsPendingDeletion())
					continue;

				++child->_transformVersion;
				child->_hasCachedLocalTM = false;
				child->_hasCachedWorldTM = false;
				child->_hasCachedWorldAABB = false;
				child->_hasCachedWorldOBB = false;
				child->_hasCachedWorldTMTranspose = false;
				child->_hasCachedWorldTMInvert = false;
				child->_hasCachedWorldBoundingSphere = false;

				invalidateRef(child, invalidateRef);
			}
		};

		switch (message->_id)
		{
		case MessageId::TransformChanged:
		{
			auto transformMessage = message->CastAs<TransformChangedMessage>();

			if ((transformMessage->_flags & (TransformChangedMessage::ChangeFlags::ScaleChanged | TransformChangedMessage::ChangeFlags::RotationChanged)) != 0)
			{
				++_transformVersion;
				_hasCachedLocalTM = false;
				_hasCachedWorldTM = false;
				_hasCachedWorldAABB = false;
				_hasCachedWorldOBB = false;
				_hasCachedWorldTMTranspose = false;
				_hasCachedWorldTMInvert = false;
				_hasCachedWorldBoundingSphere = false;

				if (auto meshComponent = GetComponent<StaticMeshComponent>(); meshComponent != nullptr && meshComponent->GetMesh())
				{
					RecalculateBoundingVolumes(meshComponent->GetMesh()->GetAABB());
				}
			}

			if ((transformMessage->_flags & TransformChangedMessage::ChangeFlags::PositionChanged) == TransformChangedMessage::ChangeFlags::PositionChanged)
			{
				++_transformVersion;
				_hasCachedWorldTM = false;
				_hasCachedLocalTM = false;
				_hasCachedWorldAABB = false;
				_hasCachedWorldTMTranspose = false;
				_hasCachedWorldTMInvert = false;
				_hasCachedWorldBoundingSphere = false;
				_hasCachedWorldOBB = false;

				_lastPosition = transformMessage->_position;
			}

			// Parent-space motion invalidates all descendant world transforms/bounds.
			invalidateChildTransformCaches(this, invalidateChildTransformCaches);

			auto updatePvsForDescendants = [](PVS* pvs, Entity* root, auto&& updateRef) -> void
			{
				if (pvs == nullptr || root == nullptr)
					return;

				for (auto* child : root->GetChildren())
				{
					if (child == nullptr || child->IsPendingDeletion())
						continue;

					pvs->UpdateEntityInstanceCache(child);
					updateRef(pvs, child, updateRef);
				}
			};

			if (auto mainCamera = _scene->GetMainCamera())
			{
				if (auto pvs = mainCamera->GetPVS())
				{
					pvs->UpdateEntityInstanceCache(this);
					updatePvsForDescendants(pvs, this, updatePvsForDescendants);
				}
			}

			if (auto sun = _scene->GetSunLight())
			{
				for (auto i = 0; i < sun->GetMaxSupportedShadowCascades(); ++i)
				{
					if (auto pvs = sun->GetPVS(i))
					{
						pvs->UpdateEntityInstanceCache(this);
					}
				}
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

		case MessageId::PVSVisibilityChanged:
		{
			_isInPVS = message->CastAs<PVSVisibilityChangedMessage>()->visible;
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

	void Entity::OnGUI()
	{
		for (auto&& component : _components)
		{
			component.component->OnGUI();
		}
	}

	void Entity::BroadcastMessage(Message* message)
	{
		for (auto& entSet : g_pEnv->_sceneManager->GetCurrentScene()->GetEntities())
		{
			for (auto& ent : entSet.second)
			{
				if (ent == this)
					continue;

				ent->OnMessage(message, this);
			}
		}

		_scene->BroadcastMessage(message);
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
		entData["flags"] = GetFlags();
		entData["prefabNodeId"] = EnsurePrefabNodeId();

		if (!_prefabSourcePath.empty())
		{
			auto& prefabData = entData["prefab"];
			prefabData["sourcePath"] = _prefabSourcePath.string();
			prefabData["rootEntityName"] = _prefabRootEntityName;
			prefabData["isRootInstance"] = _isPrefabInstanceRoot;

			if (!_prefabPropertyOverrides.empty())
			{
				std::vector<std::string> overrides(_prefabPropertyOverrides.begin(), _prefabPropertyOverrides.end());
				std::sort(overrides.begin(), overrides.end());
				prefabData["overrides"] = overrides;
			}

			if (!_prefabOverridePatches.empty())
			{
				auto patches = _prefabOverridePatches;
				std::sort(patches.begin(), patches.end(),
					[](const PrefabOverridePatch& a, const PrefabOverridePatch& b)
					{
						if (a.componentName != b.componentName)
							return a.componentName < b.componentName;
						if (a.path != b.path)
							return a.path < b.path;
						return a.op < b.op;
					});

				auto& patchArray = prefabData["overridePatches"];
				patchArray = json::array();
				for (const auto& patch : patches)
				{
					if (patch.componentName.empty() || patch.path.empty() || patch.op.empty())
						continue;

					json patchData = json::object();
					patchData["component"] = patch.componentName;
					patchData["path"] = patch.path;
					patchData["op"] = patch.op;
					if (patch.op != "remove")
						patchData["value"] = patch.value;
					patchArray.push_back(std::move(patchData));
				}
			}
		}

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

			LOG_DEBUG("Saved component '%s::%s'", GetName().c_str(), component.component->GetComponentName())
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
		if (data.find("layer") != data.end())
		{
			Layer layer = data["layer"].get<Layer>();
			SetLayer(layer);
		}

		if (data.find("flags") != data.end())
		{
			EntityFlags flags = data["flags"].get<EntityFlags>();
			flags &= ~EntityFlags::SelectedInEditor; // remove the selected in editor flag
			SetFlag(flags);
		}

		if (data.find("prefabNodeId") != data.end() && data["prefabNodeId"].is_string())
		{
			_prefabNodeId = data["prefabNodeId"].get<std::string>();
		}
		if (_prefabNodeId.empty())
		{
			_prefabNodeId = GeneratePrefabNodeId();
		}

		if (data.find("prefab") != data.end())
		{
			const auto& prefabData = data["prefab"];
			const auto sourcePath = prefabData.value("sourcePath", std::string());
			_prefabSourcePath = sourcePath.empty() ? fs::path() : fs::path(sourcePath);
			_prefabRootEntityName = prefabData.value("rootEntityName", std::string());
			_isPrefabInstanceRoot = prefabData.value("isRootInstance", false);
			_prefabPropertyOverrides.clear();
			_prefabOverridePatches.clear();

			const auto overridesIt = prefabData.find("overrides");
			if (overridesIt != prefabData.end() && overridesIt->is_array())
			{
				for (const auto& item : *overridesIt)
				{
					if (item.is_string())
					{
						const auto overridePath = item.get<std::string>();
						if (!overridePath.empty())
							_prefabPropertyOverrides.insert(overridePath);
					}
				}
			}

			const auto patchesIt = prefabData.find("overridePatches");
			if (patchesIt != prefabData.end() && patchesIt->is_array())
			{
				for (const auto& item : *patchesIt)
				{
					if (!item.is_object())
						continue;

					PrefabOverridePatch patch;
					patch.componentName = item.value("component", std::string());
					patch.path = item.value("path", std::string());
					patch.op = item.value("op", std::string());
					if (patch.componentName.empty() || patch.path.empty() || patch.op.empty())
						continue;

					const auto valueIt = item.find("value");
					patch.value = valueIt != item.end() ? *valueIt : json();
					UpsertPrefabOverridePatch(patch);
				}
			}
		}
		else
		{
			ClearPrefabSource();
		}

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

	void Entity::ToggleVisibility()
	{
		if (HasFlag(EntityFlags::DoNotRender))
		{
			ClearFlags(EntityFlags::DoNotRender);			
		}
		else
		{
			SetFlag(EntityFlags::DoNotRender);
		}

		for (auto& child : _children)
		{
			child->ToggleVisibility();
		}
	}

	void Entity::SetPrefabSource(const fs::path& prefabPath, const std::string& prefabRootEntityName, bool isRootInstance)
	{
		_prefabSourcePath = prefabPath;
		_prefabRootEntityName = prefabRootEntityName;
		_isPrefabInstanceRoot = isRootInstance;
		_prefabPropertyOverrides.clear();
		_prefabOverridePatches.clear();
	}

	void Entity::ClearPrefabSource()
	{
		_prefabSourcePath.clear();
		_prefabRootEntityName.clear();
		_isPrefabInstanceRoot = false;
		_prefabPropertyOverrides.clear();
		_prefabOverridePatches.clear();
	}

	bool Entity::IsPrefabInstance() const
	{
		return !_prefabSourcePath.empty();
	}

	bool Entity::IsPrefabInstanceRoot() const
	{
		return IsPrefabInstance() && _isPrefabInstanceRoot;
	}

	const fs::path& Entity::GetPrefabSourcePath() const
	{
		return _prefabSourcePath;
	}

	const std::string& Entity::GetPrefabRootEntityName() const
	{
		return _prefabRootEntityName;
	}

	void Entity::SetPrefabInstanceRoot(bool isRootInstance)
	{
		_isPrefabInstanceRoot = isRootInstance;
	}

	void Entity::SetPrefabNodeId(const std::string& prefabNodeId)
	{
		_prefabNodeId = prefabNodeId;
	}

	const std::string& Entity::GetPrefabNodeId() const
	{
		return _prefabNodeId;
	}

	const std::string& Entity::EnsurePrefabNodeId()
	{
		if (_prefabNodeId.empty())
		{
			_prefabNodeId = GeneratePrefabNodeId();
		}

		return _prefabNodeId;
	}

	void Entity::MarkPrefabPropertyOverride(const std::string& propertyPath)
	{
		if (propertyPath.empty())
			return;

		_prefabPropertyOverrides.insert(propertyPath);
	}

	void Entity::ClearPrefabPropertyOverride(const std::string& propertyPath)
	{
		if (propertyPath.empty())
			return;

		_prefabPropertyOverrides.erase(propertyPath);
	}

	bool Entity::HasPrefabPropertyOverride(const std::string& propertyPath) const
	{
		return !propertyPath.empty() && _prefabPropertyOverrides.find(propertyPath) != _prefabPropertyOverrides.end();
	}

	void Entity::ClearPrefabPropertyOverrides()
	{
		_prefabPropertyOverrides.clear();
	}

	void Entity::SetPrefabPropertyOverrides(const std::unordered_set<std::string>& overrides)
	{
		_prefabPropertyOverrides = overrides;
	}

	const std::unordered_set<std::string>& Entity::GetPrefabPropertyOverrides() const
	{
		return _prefabPropertyOverrides;
	}

	void Entity::UpsertPrefabOverridePatch(const PrefabOverridePatch& patch)
	{
		if (patch.componentName.empty() || patch.path.empty() || patch.op.empty())
			return;

		for (auto& existing : _prefabOverridePatches)
		{
			if (existing.componentName == patch.componentName && existing.path == patch.path)
			{
				existing.op = patch.op;
				existing.value = patch.value;
				return;
			}
		}

		_prefabOverridePatches.push_back(patch);
	}

	void Entity::ClearPrefabOverridePatch(const std::string& componentName, const std::string& path)
	{
		if (componentName.empty() || path.empty())
			return;

		_prefabOverridePatches.erase(
			std::remove_if(_prefabOverridePatches.begin(), _prefabOverridePatches.end(),
				[&](const PrefabOverridePatch& patch)
				{
					return patch.componentName == componentName && patch.path == path;
				}),
			_prefabOverridePatches.end());
	}

	void Entity::ClearPrefabOverridePatches()
	{
		_prefabOverridePatches.clear();
	}

	void Entity::SetPrefabOverridePatches(const std::vector<PrefabOverridePatch>& patches)
	{
		_prefabOverridePatches = patches;
	}

	const std::vector<Entity::PrefabOverridePatch>& Entity::GetPrefabOverridePatches() const
	{
		return _prefabOverridePatches;
	}
}
