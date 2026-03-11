
#include "PluginSystem.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../FileSystem/FileSystem.hpp"
#include "../Environment/LogFile.hpp"

namespace HexEngine
{
	

	PluginSystem::PluginSystem()
	{

	}

	PluginSystem::~PluginSystem()
	{

	}

	/// <summary>
	/// Load all plugins in the Plugins folder
	/// </summary>
	/// <returns>The number of plugins that were loaded</returns>
	uint32_t PluginSystem::LoadAllPlugins()
	{
		auto pluginPath = g_pEnv->GetFileSystem().GetLocalAbsolutePath("Plugins");

		if (fs::exists(pluginPath) == false)
		{
			LOG_WARN("The plugin folder does not exist");
			return 0;
		}

		for (auto const& dir_entry : std::filesystem::directory_iterator{ pluginPath })
		{
			if (dir_entry.is_directory())
				continue;

			if (dir_entry.is_regular_file() == false)
				continue;

			const auto& path = dir_entry.path();

			if (path.extension() != ".dll")
				continue;

			if (HasLoadedPlugin(path.stem().string()))
				continue;

			if (!LoadPlugin(path))
			{
				LOG_WARN("Failed to load plugin!");
				continue;
			}			
		}

		return (uint32_t)_plugins.size();
	}

	bool PluginSystem::LoadPlugin(const fs::path& path)
	{
		void* pPluginHandle = LoadLibraryW(path.c_str());

		if (!pPluginHandle)
		{
			LOG_CRIT("Failed to load plugin '%S', error code: %d", path.filename().c_str(), GetLastError());
			return false;
		}

		void* pEntryFunc = GetProcAddress((HMODULE)pPluginHandle, "CreatePlugin");

		if (!pEntryFunc)
		{
			LOG_WARN("Could not find plugin entry point for plugin '%S'", path.filename().c_str());
			return false;
		}

		void* pExitFunc = GetProcAddress((HMODULE)pPluginHandle, "DestroyPlugin");

		if (!pExitFunc)
		{
			LOG_WARN("Could not find plugin exit point for plugin '%S'", path.filename().c_str());
			return false;
		}

		IPlugin::tEntryFunc entryFunc = (IPlugin::tEntryFunc)pEntryFunc;

		IPlugin* pluginInterface = entryFunc(g_pEnv);

		if (!pluginInterface)
		{
			LOG_WARN("The plugin '%S' returned a null IPlugin interface", path.filename().c_str());
			return false;
		}

		IPlugin::VersionData version;
		pluginInterface->GetVersionData(&version);

		if (version.enabled == false)
		{
			LOG_INFO("Plugin %s is disabled so skipping load", version.name.c_str());

			IPlugin::tExitFunc exitFunc = (IPlugin::tExitFunc)pExitFunc;
			exitFunc();
			return false;
		}

		LOG_INFO("Successfully loaded plugin: %s v%d.%d", version.name.c_str(), version.majorVersion, version.minorVersion);
		LOG_INFO("\tDescription: %s", version.description.c_str());

		InitInfo initInfo;
		initInfo.entryFunc = (IPlugin::tEntryFunc)pEntryFunc;
		initInfo.exitFunc = (IPlugin::tExitFunc)pExitFunc;
		initInfo.iface = pluginInterface;
		initInfo.moduleHandle = pPluginHandle;
		initInfo.versionData = version;

		// Check for dependencies
		std::vector<std::string> dependencies;
		pluginInterface->GetDependencies(dependencies);

		if (dependencies.size() > 0)
		{
			for (auto& dep : dependencies)
			{
				// if the plugin is already loaded, just ignore it
				if (HasLoadedPlugin(dep))
					continue;

				fs::path dependencyPath = path.parent_path() / dep;
				dependencyPath += ".dll";

				LoadPlugin(dependencyPath);
			}
		}

		/*if (pluginInterface->Create() == false)
		{
			LOG_WARN("Plugin '%s' returned false from IPlugin::Create(), it will be unloaded");

			initInfo.exitFunc();

			FreeLibrary((HMODULE)pPluginHandle);

			return false;
		}*/

		_plugins.push_back(initInfo);

		return true;
	}

	bool PluginSystem::HasLoadedPlugin(const std::string& name) const
	{
		for (auto& plugin : _plugins)
		{
			if (plugin.versionData.name == name)
				return true;
		}
		return false;
	}

	/// <summary>
	/// 
	/// </summary>
	void PluginSystem::UnloadAllPlugins()
	{
		for (auto& plugin : _plugins)
		{
			if(plugin.iface)
				plugin.iface->Destroy();

			plugin.exitFunc();

			FreeLibrary((HMODULE)plugin.moduleHandle);
		}

		_plugins.clear();
	}

	IPluginInterface* PluginSystem::CreateInterface(const std::string& interfaceName)
	{
		LOG_INFO("Locating an interface for '%s'", interfaceName.c_str());

		for (auto& plugin : _plugins)
		{
			if (plugin.iface)
			{
				if (auto exposedInterface = plugin.iface->CreateInterface(interfaceName); exposedInterface != nullptr)
				{
					LOG_INFO("Found an interface for '%s' at %p", interfaceName.c_str(), exposedInterface);

					return exposedInterface;
				}
			}
		}

		LOG_CRIT("A suitable plugin interface was not found for '%s'", interfaceName.c_str());

		return nullptr;
	}
}