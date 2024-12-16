
#pragma once

#include <HexEngine.Core\HexEngine.hpp>
#include "Brotli.hpp"

class BrotliPlugin : public IPlugin
{
public:
	BrotliPlugin();

	//virtual bool Create() override;

	virtual void Destroy() override;

	virtual void GetVersionData(VersionData* data) override;

	virtual IPluginInterface* CreateInterface(const std::string& interfaceName) override;

	virtual void GetDependencies(std::vector<std::string>& dependencies) const override {}

private:
	Brotli* _brotli = nullptr;
};

inline BrotliPlugin* g_pBrotliPlugin = nullptr;