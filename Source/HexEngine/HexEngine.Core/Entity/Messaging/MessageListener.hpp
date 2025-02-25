

#pragma once

#include "../../Required.hpp"
#include "Message.hpp"

namespace HexEngine
{
	class HEX_API MessageListener
	{
	public:
		virtual void OnMessage(Message* message, MessageListener* sender) {};
	};
}