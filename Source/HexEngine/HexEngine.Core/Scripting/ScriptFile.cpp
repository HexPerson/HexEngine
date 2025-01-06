
#include "ScriptFile.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../FileSystem/ResourceSystem.hpp"

namespace HexEngine
{
	std::shared_ptr<ScriptFile> ScriptFile::Create(const fs::path& path, ScriptComponent* component)
	{
		ScriptLoadOptions options;
		options.component = component;

		return reinterpret_pointer_cast<ScriptFile>(g_pEnv->_resourceSystem->LoadResource(path, &options));
	}
}