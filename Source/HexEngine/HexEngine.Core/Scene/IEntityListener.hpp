
#pragma once

#include "../Required.hpp"
#include "../Entity/Entity.hpp"

namespace HexEngine
{
	class IEntityListener
	{
	public:
		virtual void OnAddEntity(Entity* entity) = 0;

		virtual void OnRemoveEntity(Entity* entity) = 0;

		virtual void OnAddComponent(Entity* entity, BaseComponent* component) = 0;

		virtual void OnRemoveComponent(Entity* entity, BaseComponent* component) = 0;
	};
}
