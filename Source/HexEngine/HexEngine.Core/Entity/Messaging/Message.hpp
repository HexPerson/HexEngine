
#pragma once

#include "../../Required.hpp"
#include "../EntityId.hpp"

namespace HexEngine
{
	class Entity;

	enum class MessageId
	{
		Invalid,
		TransformChanged,
		Updated,
		EnterTrigger,
		LeaveTrigger,
		RigidBodyCollision,
		EntityDestroyed,
		EntityParentChanged,
		NavigationTargetReached,
		PVSVisibilityChanged,
		EditorEntityDuplicated,
		EditorEntityCreated,
		EditorSceneViewportMouseDown,
		EditorSceneViewportMouseMove,
		EditorWorldRayCast,

		CustomMessage,	// custom game messages should start here
	};

	class Message
	{
	public:
		Message(MessageId id) :
			_id(id)
		{}

		// not actually needed, but we need it to be polymorphic
		virtual ~Message() {}

		template <typename T>
		T* CastAs()
		{
			return dynamic_cast<T*>(this);
		}

	public:
		MessageId _id;
	};

	class EntityDestroyedMessage : public Message
	{
	public:
		
		EntityDestroyedMessage(Entity* entity) :
			Message(MessageId::EntityDestroyed),
			_entity(entity)
		{}

		Entity* _entity;
		EntityId _entityId;
	};

	class EntityParentChangedMessage : public Message
	{
	public:
		enum class Flags
		{
			NoLongerParent,
			BecameParent,
		};
		EntityParentChangedMessage(Entity* entity, Entity* parent, Flags flags) :
			Message(MessageId::EntityParentChanged),
			_entity(entity),
			_parent(parent),
			_flags(flags)
		{}

		Entity* _entity;
		Entity* _parent;
		EntityId _entityId;
		EntityId _parentId;
		Flags _flags;
	};

	class TransformChangedMessage : public Message
	{
	public:
		enum ChangeFlags
		{
			PositionChanged = HEX_BITSET(0),
			RotationChanged = HEX_BITSET(1),
			ScaleChanged = HEX_BITSET(2),
		};

		TransformChangedMessage() : Message(MessageId::TransformChanged) {}		

	public:
		math::Vector3 _position;
		math::Quaternion _rotation;
		math::Vector3 _scale;
		ChangeFlags _flags;
	};
	DEFINE_ENUM_FLAG_OPERATORS(TransformChangedMessage::ChangeFlags);

	class UpdatedMessage : public Message
	{
	public:
		UpdatedMessage() : Message(MessageId::Updated) {}

	public:
		float _time = 0.0f;
	};

	class EnterTriggerMessage : public Message
	{
	public:
		EnterTriggerMessage() : Message(MessageId::EnterTrigger) {}

		Entity* trigger = nullptr;
		EntityId triggerId = InvalidEntityId;
	};

	class LeaveTriggerMessage : public Message
	{
	public:
		LeaveTriggerMessage() : Message(MessageId::LeaveTrigger) {}

		Entity* trigger = nullptr;
		EntityId triggerId = InvalidEntityId;
	};

	class RigidBodyCollision : public Message
	{
	public:
		RigidBodyCollision() : Message(MessageId::RigidBodyCollision) {}

		Entity* collidedWith = nullptr;
		EntityId collidedWithId = InvalidEntityId;
		math::Vector3 collisionPoint;
	};

	class NavigationTargetReachedMessage : public Message
	{
	public:
		NavigationTargetReachedMessage() : Message(MessageId::NavigationTargetReached) {}

		math::Vector3 targetPosition;
		math::Vector3 finalPosition;
	};

	class PVSVisibilityChangedMessage : public Message
	{
	public:
		PVSVisibilityChangedMessage() : Message(MessageId::PVSVisibilityChanged) {}

		bool visible = false;
	};

	class EditorEntityDuplicatedMessage : public Message
	{
	public:
		EditorEntityDuplicatedMessage(Entity* sourceEntity, Entity* duplicatedEntity) :
			Message(MessageId::EditorEntityDuplicated),
			source(sourceEntity),
			duplicate(duplicatedEntity)
		{
		}

		Entity* source = nullptr;
		Entity* duplicate = nullptr;
		EntityId sourceId = InvalidEntityId;
		EntityId duplicateId = InvalidEntityId;
		bool handled = false;
	};

	class EditorEntityCreatedMessage : public Message
	{
	public:
		EditorEntityCreatedMessage(Entity* createdEntity) :
			Message(MessageId::EditorEntityCreated),
			entity(createdEntity)
		{
		}

		Entity* entity = nullptr;
		EntityId entityId = InvalidEntityId;
		bool handled = false;
	};

	class EditorSceneViewportMouseDownMessage : public Message
	{
	public:
		EditorSceneViewportMouseDownMessage() :
			Message(MessageId::EditorSceneViewportMouseDown)
		{
		}

		int32_t button = 0;
		int32_t screenX = 0;
		int32_t screenY = 0;
		bool hasHit = false;
		math::Vector3 worldPosition = math::Vector3::Zero;
		Entity* hitEntity = nullptr;
		EntityId hitEntityId = InvalidEntityId;
		bool handled = false;
	};

	class EditorSceneViewportMouseMoveMessage : public Message
	{
	public:
		EditorSceneViewportMouseMoveMessage() :
			Message(MessageId::EditorSceneViewportMouseMove)
		{
		}

		int32_t screenX = 0;
		int32_t screenY = 0;
		bool hasHit = false;
		math::Vector3 worldPosition = math::Vector3::Zero;
		Entity* hitEntity = nullptr;
		EntityId hitEntityId = InvalidEntityId;
		bool handled = false;
	};

	class EditorWorldRayCastMessage : public Message
	{
	public:
		EditorWorldRayCastMessage() :
			Message(MessageId::EditorWorldRayCast)
		{
		}

		math::Vector3 rayOrigin = math::Vector3::Zero;
		math::Vector3 rayDirection = math::Vector3::Forward;
		float maxDistance = 0.0f;
		uint32_t mask = 0;
		std::vector<Entity*> ignoredEntities;
		bool hasHit = false;
		math::Vector3 hitPosition = math::Vector3::Zero;
		math::Vector3 hitNormal = math::Vector3::Up;
		Entity* hitEntity = nullptr;
		EntityId hitEntityId = InvalidEntityId;
		float hitDistance = 0.0f;
	};
}
