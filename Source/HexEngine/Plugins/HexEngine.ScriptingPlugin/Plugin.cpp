
#include "Plugin.hpp"
#include "LuaScriptEngine.hpp"

CREATE_PLUGIN(g_pScriptingPlugin, ScriptingPlugin);

void ScriptingPlugin::Destroy()
{
	// The environment owns (and has already deleted) the engine.
	_engine = nullptr;
}

void ScriptingPlugin::GetVersionData(VersionData* data)
{
	data->author = "HexPerson";
	data->description = "Gameplay scripting: Lua 5.5 (sol2) runtime for ScriptComponent, with hot reload";
	data->majorVersion = 1;
	data->minorVersion = 0;
	data->name = "ScriptingPlugin";
}

HexEngine::IPluginInterface* ScriptingPlugin::CreateInterface(const std::string& interfaceName)
{
	if (interfaceName == HexEngine::IScriptEngine::InterfaceName)
	{
		if (_engine == nullptr)
			_engine = new HexScripting::LuaScriptEngine;
		return _engine;
	}

	return nullptr;
}
