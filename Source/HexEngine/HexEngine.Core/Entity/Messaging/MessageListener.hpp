

#pragma once

#include "../../Required.hpp"
#include "Message.hpp"

namespace HexEngine
{
	class MessageListener
	{
	public:
		virtual void OnMessage(Message* message, MessageListener* sender) {};
	};
}