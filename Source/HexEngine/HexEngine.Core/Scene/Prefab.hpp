
#pragma once

#include "../FileSystem/IResource.hpp"

namespace HexEngine
{
	class Entity;

	class HEX_API Prefab : public IResource
	{
		friend class PrefabLoader;

	public:
		static std::shared_ptr<Prefab> Create(const fs::path& path);

		virtual void			Save() override;
		virtual void			Destroy() override;
		virtual ResourceType	GetResourceType() const;

		const std::vector<Entity*>& GetLoadedEntities() const;

	private:
		std::vector<Entity*> _entities;
	};
}
