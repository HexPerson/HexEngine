#pragma once

#include <HexEngine.Core\HexEngine.hpp>

/// <summary>
/// Gameplay scripting plugin: provides HexEngine::IScriptEngine (Lua 5.5 via
/// sol2) for ScriptComponent. The engine object is created on the first
/// CreateInterface request and OWNED BY THE ENVIRONMENT from then on (it calls
/// Destroy() and deletes it through IScriptEngine's virtual destructor), so the
/// plugin never deletes it itself.
/// </summary>
class ScriptingPlugin : public HexEngine::IPlugin
{
public:
	virtual void Destroy() override;
	virtual void GetVersionData(VersionData* data) override;
	virtual HexEngine::IPluginInterface* CreateInterface(const std::string& interfaceName) override;
	virtual void GetDependencies(std::vector<std::string>& dependencies) const override {}

private:
	HexEngine::IScriptEngine* _engine = nullptr;
};

inline ScriptingPlugin* g_pScriptingPlugin = nullptr;
