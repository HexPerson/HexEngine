
#pragma once

#include "../Required.hpp"
#include "Model.hpp"
#include "../FileSystem/ResourceSystem.hpp"
#include "../Plugin/IPlugin.hpp"

namespace HexEngine
{
	class IModelImporter : public IResourceLoader, public IPluginInterface
	{
	public:
		DECLARE_PLUGIN_INTERFACE(IModelImporter, 001);
	};
}
