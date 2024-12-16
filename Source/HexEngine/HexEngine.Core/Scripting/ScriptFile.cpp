
#include "ScriptFile.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../FileSystem/ResourceSystem.hpp"

namespace HexEngine
{
	ScriptFile* ScriptFile::Create(const fs::path& path, ScriptComponent* component)
	{
		ScriptLoadOptions options;
		options.component = component;

		return (ScriptFile*)g_pEnv->_resourceSystem->LoadResource(path, &options);
	}
}