
#pragma once

#include <HexEngine.Core\HexEngine.hpp>
#include "MyInterface.hpp"

/// <summary>
/// The sample plugin class. Overrides IPlugin
/// </summary>
class SamplePlugin : public IPlugin
{
public:
	SamplePlugin();

	/// <summary>
	/// Called after the plugin is loaded
	/// </summary>
	/// <returns></returns>
	//virtual bool Create() override;

	/// <summary>
	/// Called just before the plugin is unloaded
	/// </summary>
	virtual void Destroy() override;

	/// <summary>
	/// Called by the engine to retrieve version info about this plugin
	/// </summary>
	/// <param name="data">A pointer to the VersionInfo instance representing this plugin</param>
	virtual void GetVersionData(VersionData* data) override;

	/// <summary>
	/// Factory function used by the engine to retrieve implemented interfaces
	/// </summary>
	/// <param name="interfaceName">The name of the interface being searched for</param>
	/// <returns>A pointer to an implemented interface if found, or null if not.</returns>
	virtual IPluginInterface* CreateInterface(const std::string& interfaceName) override;

	virtual void GetDependencies(std::vector<std::string>& dependencies) const override;

private:
	MyInterface* _interface = nullptr;
};

inline SamplePlugin* g_pSamplePlugin = nullptr;