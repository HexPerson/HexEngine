
#pragma once

#include "BaseComponent.hpp"

namespace HexEngine
{
	class InteractionComponent;

	using tInteractionCallback = void(InteractionComponent*, Entity*);

	class HEX_API InteractionComponent : public BaseComponent
	{
	public:
		CREATE_COMPONENT_ID(InteractionComponent);
		DEFINE_COMPONENT_CTOR(InteractionComponent);

		void SetCallback(std::function<tInteractionCallback> callback);

		void Callback();

	private:
		std::function<tInteractionCallback> _callback;
	};
}
