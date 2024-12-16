
#pragma once

#include "../Plugin/IPlugin.hpp"

namespace HexEngine
{
	class IScriptEngine : public IPluginInterface
	{
	public:
		DECLARE_PLUGIN_INTERFACE(IScriptEngine, 001);
	};
}
