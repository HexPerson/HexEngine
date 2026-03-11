
#include "ScriptFile.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../FileSystem/ResourceSystem.hpp"

namespace HexEngine
{
	std::shared_ptr<ScriptFile> ScriptFile::Create(const fs::path& path, ScriptComponent* component)
	{
		ScriptLoadOptions options;
		options.component = component;

		return dynamic_pointer_cast<ScriptFile>(g_pEnv->GetResourceSystem().LoadResource(path, &options));
	}
}