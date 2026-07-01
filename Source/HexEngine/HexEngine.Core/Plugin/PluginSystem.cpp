
#include "PluginSystem.hpp"
#include "ScopedModule.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../FileSystem/FileSystem.hpp"
#include "../Environment/LogFile.hpp"
#include "../Utility/Sha256.hpp"

#include <fstream>
#include <sstream>
#include <cstdlib>
#include <algorithm>

namespace HexEngine
{
	PluginSystem::PluginSystem()
	{

	}

	PluginSystem::~PluginSystem()
	{

	}

	void PluginSystem::SetLoadPolicy(PluginLoadPolicy policy)
	{
		_policy = policy;
		_policyExplicit = true;
	}

	void PluginSystem::ResolvePolicy()
	{
		if (_policyExplicit)
			return;

		// QA / testing override without a shipping rebuild:
		//   HEXENGINE_PLUGIN_POLICY=production   (or =developer)
		char buf[64] = {};
		size_t len = 0;
		if (getenv_s(&len, buf, sizeof(buf), "HEXENGINE_PLUGIN_POLICY") == 0 && len > 1)
		{
			std::string v(buf);
			std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return (char)std::tolower(c); });
			if (v.rfind("prod", 0) == 0)
			{
				_policy = PluginLoadPolicy::Production;
				LOG_INFO("Plugin load policy set to PRODUCTION via HEXENGINE_PLUGIN_POLICY.");
			}
			else if (v.rfind("dev", 0) == 0)
			{
				_policy = PluginLoadPolicy::Developer;
			}
		}
	}

	bool PluginSystem::LoadManifest(const fs::path& manifestPath)
	{
		_manifest = PluginManifest{};
		_manifestValid = false;

		std::ifstream file(manifestPath, std::ios::binary);
		if (!file.is_open())
			return false; // no manifest present

		std::stringstream ss;
		ss << file.rdbuf();

		std::string error;
		if (!ParsePluginManifest(ss.str(), _manifest, error))
		{
			LOG_CRIT("Plugin manifest '%S' is invalid: %s", manifestPath.filename().c_str(), error.c_str());
			return false;
		}

		LOG_INFO("Loaded plugin manifest with %zu allowlisted plugin(s).", _manifest.entries.size());
		_manifestValid = true;
		return true;
	}

	/// <summary>
	/// Load all plugins in the Plugins folder, subject to the load policy +
	/// manifest allowlist. Production mode fails closed (no valid manifest => no
	/// plugins loaded).
	/// </summary>
	/// <returns>The number of plugins that were loaded</returns>
	uint32_t PluginSystem::LoadAllPlugins()
	{
		ResolvePolicy();

		auto pluginPath = g_pEnv->GetFileSystem().GetLocalAbsolutePath("Plugins");

		if (fs::exists(pluginPath) == false)
		{
			LOG_WARN("The plugin folder does not exist");
			return 0;
		}

		const bool haveManifest = LoadManifest(pluginPath / "plugins.json");

		if (_policy == PluginLoadPolicy::Production && !haveManifest)
		{
			// Fail closed: without a valid allowlist we load nothing.
			LOG_CRIT("Production plugin policy is active but no valid manifest (Plugins/plugins.json) was found - refusing to load any plugins.");
			return 0;
		}

		if (!haveManifest)
			LOG_WARN("No plugin manifest (Plugins/plugins.json) present; loading all plugins (developer mode).");

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

			// Gating + logging happen inside LoadPlugin (also covers dependencies).
			LoadPlugin(path);
		}

		return (uint32_t)_plugins.size();
	}

	bool PluginSystem::LoadPlugin(const fs::path& path)
	{
		const std::string moduleFile = path.filename().string();

		// --- Manifest gate (BEFORE mapping the DLL) --------------------------
		// In production an unlisted/unverified module is never even LoadLibrary'd.
		// Only compute the hash when it can affect the decision (production, or a
		// manifest entry that pins one) so developer loads stay fast.
		const PluginManifestEntry* entry = FindPluginManifestEntry(_manifest, moduleFile);
		const bool needHash = (_policy == PluginLoadPolicy::Production) || (entry != nullptr && !entry->sha256.empty());

		std::string actualHash;
		const std::string* actualHashPtr = nullptr;
		if (needHash)
		{
			std::string err;
			if (Sha256File(path, actualHash, err))
				actualHashPtr = &actualHash;
			else
				LOG_WARN("Could not hash plugin '%s' for verification: %s", moduleFile.c_str(), err.c_str());
		}

		const PluginLoadDecision decision = EvaluatePluginLoad(_manifest, _policy, moduleFile, actualHashPtr);
		if (decision != PluginLoadDecision::Allow)
		{
			LOG_WARN("Refusing to load plugin '%s': %s (policy=%s)",
				moduleFile.c_str(), ToString(decision),
				_policy == PluginLoadPolicy::Production ? "production" : "developer");
			return false;
		}

		if (_policy == PluginLoadPolicy::Developer && entry == nullptr)
			LOG_WARN("Plugin '%s' is not listed in the plugin manifest (allowed in developer mode).", moduleFile.c_str());

		// --- Load (RAII-owned so every early return frees the module) --------
		ScopedModule module(LoadLibraryW(path.c_str()));
		if (!module)
		{
			LOG_CRIT("Failed to load plugin '%S', error code: %lu", path.filename().c_str(), GetLastError());
			return false;
		}

		void* pEntryFunc = GetProcAddress(module.get(), "CreatePlugin");
		if (!pEntryFunc)
		{
			LOG_WARN("Could not find plugin entry point for plugin '%S'", path.filename().c_str());
			return false; // ScopedModule frees the module
		}

		void* pExitFunc = GetProcAddress(module.get(), "DestroyPlugin");
		if (!pExitFunc)
		{
			LOG_WARN("Could not find plugin exit point for plugin '%S'", path.filename().c_str());
			return false; // ScopedModule frees the module
		}

		IPlugin::tEntryFunc entryFunc = (IPlugin::tEntryFunc)pEntryFunc;
		IPlugin* pluginInterface = entryFunc(g_pEnv);
		if (!pluginInterface)
		{
			LOG_WARN("The plugin '%S' returned a null IPlugin interface", path.filename().c_str());
			return false; // ScopedModule frees the module
		}

		IPlugin::VersionData version;
		pluginInterface->GetVersionData(&version);

		if (version.enabled == false)
		{
			LOG_INFO("Plugin %s is disabled so skipping load", version.name.c_str());
			((IPlugin::tExitFunc)pExitFunc)();
			return false; // ScopedModule frees the module
		}

		LOG_INFO("Successfully loaded plugin: %s v%d.%d", version.name.c_str(), version.majorVersion, version.minorVersion);
		LOG_INFO("\tDescription: %s", version.description.c_str());

		InitInfo initInfo;
		initInfo.entryFunc = entryFunc;
		initInfo.exitFunc = (IPlugin::tExitFunc)pExitFunc;
		initInfo.iface = pluginInterface;
		initInfo.moduleHandle = nullptr;
		initInfo.versionData = version;

		// Dependencies (recursively gated through LoadPlugin).
		std::vector<std::string> dependencies;
		pluginInterface->GetDependencies(dependencies);
		for (auto& dep : dependencies)
		{
			if (HasLoadedPlugin(dep))
				continue;

			fs::path dependencyPath = path.parent_path() / dep;
			dependencyPath += ".dll";
			LoadPlugin(dependencyPath);
		}

		// Commit: hand module ownership to the plugin record (no longer freed here).
		initInfo.moduleHandle = module.release();
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

	IPluginInterface* PluginSystem::TryCreateInterface(const std::string& interfaceName)
	{
		for (auto& plugin : _plugins)
		{
			if (plugin.iface)
			{
				if (auto exposedInterface = plugin.iface->CreateInterface(interfaceName); exposedInterface != nullptr)
				{
					LOG_INFO("Found an optional interface for '%s' at %p", interfaceName.c_str(), exposedInterface);
					return exposedInterface;
				}
			}
		}
		// No LOG_CRIT - caller already has a fallback ready.
		return nullptr;
	}
}