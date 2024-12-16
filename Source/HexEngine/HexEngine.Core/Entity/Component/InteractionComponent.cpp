

#include "InteractionComponent.hpp"

namespace HexEngine
{
	InteractionComponent::InteractionComponent(Entity* entity) :
		BaseComponent(entity)
	{}

	InteractionComponent::InteractionComponent(Entity* entity, InteractionComponent* clone) :
		BaseComponent(entity),
		_callback(clone->_callback)
	{}

	void SetCallback(std::function<tInteractionCallback> callback)
	{

	}

	void InteractionComponent::Callback()
	{
		_callback(this, GetEntity());
	}
}