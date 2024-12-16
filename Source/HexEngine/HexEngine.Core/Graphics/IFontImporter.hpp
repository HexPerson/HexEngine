
#pragma once

#include "IFontResource.hpp"
#include "../FileSystem/ResourceSystem.hpp"

#undef CreateFont

namespace HexEngine
{
	

	class IFontImporter : public IPluginInterface
	{
	public:
		DECLARE_PLUGIN_INTERFACE(IFontImporter, 001);

		//virtual IFont* CreateFont(const fs::path& path) = 0;

		//virtual void DestroyFont(IFont* font) = 0;
	};
}
