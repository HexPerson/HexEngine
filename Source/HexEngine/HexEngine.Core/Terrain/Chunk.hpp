

#pragma once

#include "../Entity/Entity.hpp"

namespace HexEngine
{
	class HEX_API Chunk
	{
	public:
		Chunk(const dx::BoundingBox& box);		

		void RecalculateAABB();

		const std::vector<Entity*>& GetChunkChildren() const;
		const std::vector<StaticMeshComponent*>& GetChunkChildrenMeshes() const;

		void AddChunkEntity(Entity* entity);
		void RemoveChunkEntity(Entity* entity);

		void AddChunkComponent(Entity* entity, BaseComponent* component);
		void RemoveChunkComponent(Entity* entity, BaseComponent* component);

		const dx::BoundingBox& GetBoundingVolume() const;

		const dx::BoundingSphere& GetBoundingSphere() const;

	private:
		std::vector<Entity*> _children;
		std::vector<StaticMeshComponent*> _childrenMeshes;
		dx::BoundingBox _boundingVolume;
		dx::BoundingSphere _boundingSphere;
	};
}
