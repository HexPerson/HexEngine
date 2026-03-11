
#include "Prefab.hpp"
#include "../Environment/IEnvironment.hpp"

namespace HexEngine
{
	std::shared_ptr<Prefab> Prefab::Create(const fs::path& path)
	{
		return dynamic_pointer_cast<Prefab>(g_pEnv->GetResourceSystem().LoadResource(path));
	}

	void Prefab::Save()
	{

	}

	void Prefab::Destroy()
	{

	}

	ResourceType Prefab::GetResourceType() const
	{
		return ResourceType::Prefab;
	}

	const std::vector<Entity*>& Prefab::GetLoadedEntities() const
	{
		return _entities;
	}
}