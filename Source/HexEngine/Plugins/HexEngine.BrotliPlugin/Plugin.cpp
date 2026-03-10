
#include "Plugin.hpp"

CREATE_PLUGIN(g_pBrotliPlugin, BrotliPlugin);

//HEX_IMPORT HVar* HexEngine::g_hvars;
//HEX_IMPORT HCommand* HexEngine::g_commands;
//HEX_IMPORT int32_t HexEngine::g_numVars;
//HEX_IMPORT int32_t HexEngine::g_numCommands;

BrotliPlugin::BrotliPlugin()
{
	_brotli = new Brotli;
}

//bool BrotliPlugin::Create()
//{
//	return true;
//}

void BrotliPlugin::Destroy()
{
	SAFE_DELETE(_brotli);
}

void BrotliPlugin::GetVersionData(VersionData* data)
{
	data->author = "HexPerson";
	data->description = "Provides Google's Brotli compression";
	data->majorVersion = 1;
	data->minorVersion = 0;
	data->name = "HexEngine.BrotliPlugin";
}

HexEngine::IPluginInterface* BrotliPlugin::CreateInterface(const std::string& interfaceName)
{
	if (interfaceName == HexEngine::ICompressionProvider::InterfaceName)
		return _brotli;

	return nullptr;
}