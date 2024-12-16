

#pragma once

#include "../Entity/Entity.hpp"

namespace HexEngine
{
	class Chunk
	{
	public:
		Chunk(const dx::BoundingBox& box);		

		void RecalculateAABB();

		const std::vector<Entity*>& GetChunkChildren() const;

		void AddChunkChild(Entity* entity);

		void RemoveChunkChild(Entity* entity);

		const dx::BoundingBox& GetBoundingVolume() const;

		const dx::BoundingSphere& GetBoundingSphere() const;

	private:
		std::vector<Entity*> _children;
		dx::BoundingBox _boundingVolume;
		dx::BoundingSphere _boundingSphere;
	};
}
