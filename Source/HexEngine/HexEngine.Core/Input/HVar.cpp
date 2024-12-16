

#include "HVar.hpp"
#include "CommandManager.hpp"
#include "../Environment/IEnvironment.hpp"

namespace HexEngine
{
	//extern HVar* g_hvars;
	//extern int32_t g_numVars;

	void HVar::Create()
	{
		/*CommandManager::LockVars();

		_next = GetVars();
		GetVars() = this;
		GetNumVars()++;

		CommandManager::UnlockVars();*/
	}

	HVar::Type HVar::GetType()
	{
		return _type;
	}
}