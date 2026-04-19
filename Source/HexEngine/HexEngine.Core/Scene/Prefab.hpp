
#pragma once

#include "../FileSystem/IResource.hpp"

namespace HexEngine
{
	class Entity;
	class Scene;

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
		std::shared_ptr<Scene> _scene;
		std::vector<Entity*> _entities;
	};
}
