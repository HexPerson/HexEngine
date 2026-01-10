

#include "HVar.hpp"
#include "CommandManager.hpp"
#include "../Environment/IEnvironment.hpp"

namespace HexEngine
{
	HEX_API HVar* g_hvars = nullptr;
	HEX_API int32_t g_numVars = 0;

	void HVar::Create()
	{
		CommandManager::LockVars();

		_next = g_hvars;
		g_hvars = this;
		g_numVars++;

		CommandManager::UnlockVars();
	}

	HVar::Type HVar::GetType()
	{
		return _type;
	}
}