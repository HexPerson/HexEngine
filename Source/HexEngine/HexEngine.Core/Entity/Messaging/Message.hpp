
#pragma once

#include "../../Required.hpp"

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
	};

	class LeaveTriggerMessage : public Message
	{
	public:
		LeaveTriggerMessage() : Message(MessageId::LeaveTrigger) {}

		Entity* trigger = nullptr;
	};

	class RigidBodyCollision : public Message
	{
	public:
		RigidBodyCollision() : Message(MessageId::RigidBodyCollision) {}

		Entity* collidedWith = nullptr;
		math::Vector3 collisionPoint;
	};

	class NavigationTargetReachedMessage : public Message
	{
	public:
		NavigationTargetReachedMessage() : Message(MessageId::NavigationTargetReached) {}

		math::Vector3 targetPosition;
		math::Vector3 finalPosition;
	};
}
