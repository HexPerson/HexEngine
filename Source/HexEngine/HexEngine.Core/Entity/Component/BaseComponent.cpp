

#include "BaseComponent.hpp"

#include "../../Environment/IEnvironment.hpp"
#include "../../Environment/LogFile.hpp"

namespace HexEngine
{
	BaseComponent::BaseComponent(Entity* entity) :
		_entity(entity)
	{}

	Entity* BaseComponent::GetEntity() const
	{
		return _entity;
	}
}