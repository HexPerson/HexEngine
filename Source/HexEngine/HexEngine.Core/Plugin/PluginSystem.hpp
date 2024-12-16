
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

	class PluginSystem
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

		bool HasLoadedPlugin(const std::string& name) const;

	private:
		bool LoadPlugin(const fs::path& path);

	private:
		std::vector<InitInfo> _plugins;

	};
}
