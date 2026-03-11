
#include "IFontResource.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../FileSystem/ResourceSystem.hpp"

namespace HexEngine
{
	std::shared_ptr<IFontResource> IFontResource::Create(const fs::path& path, FontImportOptions* options)
	{
		return dynamic_pointer_cast<IFontResource>(g_pEnv->GetResourceSystem().LoadResource(path, options));
	}
}