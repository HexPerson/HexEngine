
#pragma once

#include "../Required.hpp"
#include "IPlugin.hpp"

namespace HexEngine
{
	enum class PluginHookId
	{

	};

	struct PluginEvent
	{
		PluginHookId hookId;
	};

	class HEX_API PluginSystem
	{
	public:
		struct InitInfo
		{
			IPlugin* iface;
			void* moduleHandle;
			IPlugin::tEntryFunc entryFunc;
			IPlugin::tExitFunc exitFunc;
			IPlugin::VersionData versionData;
		};

		PluginSystem();

		~PluginSystem();

		uint32_t LoadAllPlugins();

		void UnloadAllPlugins();

		IPluginInterface* CreateInterface(const std::string& interfaceName);

		// Non-fatal variant of CreateInterface. Used when the caller has a
		// in-engine fallback for an interface that may or may not be provided
		// by a plugin (e.g. ISSAOProvider falling back to DiffuseGIAOProvider
		// when HBAOPlus isn't loaded). Returns nullptr silently if nothing
		// matches - no LOG_CRIT, no modal "Critical Error" dialog.
		IPluginInterface* TryCreateInterface(const std::string& interfaceName);

		bool HasLoadedPlugin(const std::string& name) const;

		const std::vector<InitInfo>& GetAllPlugins() const { return _plugins; }

	private:
		bool LoadPlugin(const fs::path& path);

	private:
		std::vector<InitInfo> _plugins;

	};
}
