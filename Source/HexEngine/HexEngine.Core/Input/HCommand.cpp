
#include "HCommand.hpp"
#include "CommandManager.hpp"
#include "../Environment/IEnvironment.hpp"

namespace HexEngine
{
	HEX_API HCommand* g_commands = nullptr;
	HEX_API uint32_t g_numCommands = 0;

	HCommand::HCommand(const char* name, CommandFunc func)
	{
		_name = name;
		_func = func;

		CommandManager::LockCommands();
		
		_next = g_commands;
		g_commands = this;
		g_numCommands++;
		
		CommandManager::UnlockCommands();
	}
}