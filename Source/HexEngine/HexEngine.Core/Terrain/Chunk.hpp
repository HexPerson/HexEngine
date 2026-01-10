

#pragma once

#include "../Entity/Entity.hpp"

namespace HexEngine
{
	class HEX_API Chunk
	{
	public:
		Chunk(const dx::BoundingBox& box, int32_t id);		

		void										RecalculateAABB();

		const std::vector<Entity*>&					GetChunkChildren() const;
		const std::vector<StaticMeshComponent*>&	GetChunkChildrenMeshes() const;

		void										AddChunkEntity(Entity* entity);
		void										RemoveChunkEntity(Entity* entity);

		void										AddChunkComponent(Entity* entity, BaseComponent* component);
		void										RemoveChunkComponent(Entity* entity, BaseComponent* component);

		const dx::BoundingBox&						GetBoundingVolume() const;
		const dx::BoundingSphere&					GetBoundingSphere() const;

		void										CalculateChunkStats_UInt32(std::vector<math::Vector3>& vertices, std::vector<uint32_t>& indices, uint32_t& numFaces, EntityFlags excludeFlags = EntityFlags::None);

		void										Lock();
		void										Unlock();

		void										WriteToDisk();
		void										ReadFromDisk();
		bool										IsCached() const { return _isCached; }

	private:
		int32_t _id;
		std::vector<Entity*> _children;
		std::vector<StaticMeshComponent*> _childrenMeshes;
		dx::BoundingBox _boundingVolume;
		dx::BoundingSphere _boundingSphere;
		mutable std::recursive_mutex _lock;
		bool _isCached = false;
		float _cachedTime = 0.0f;
		bool _hasChanged = true;
	};
}
