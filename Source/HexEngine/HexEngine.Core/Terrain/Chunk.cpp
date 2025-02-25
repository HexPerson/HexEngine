

#include "Chunk.hpp"
#include "../Entity/Component/StaticMeshComponent.hpp"
#include "../Math/FloatMath.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../Environment/LogFile.hpp"

namespace HexEngine
{
	Chunk::Chunk(const dx::BoundingBox& box) :
		_boundingVolume(box)
	{
	}

	const std::vector<Entity*>& Chunk::GetChunkChildren() const
	{
		return _children;
	}

	const std::vector<StaticMeshComponent*>& Chunk::GetChunkChildrenMeshes() const
	{
		return _childrenMeshes;
	}

	void Chunk::RecalculateAABB()
	{
		dx::BoundingBox aabb = _boundingVolume;
		dx::BoundingBox originalAABB = aabb;

		std::vector<math::Vector3> points;

		math::Vector3 corners[8];
		aabb.GetCorners(corners);

		for (auto& corner : corners)
		{
			points.push_back(corner);
		}

		for (auto&& children : _children)
		{
			math::Vector3 corners[8];

			children->GetWorldAABB().GetCorners(corners);

			for (auto& corner : corners)
			{
				points.push_back(corner);
			}
		}

		

		dx::BoundingBox::CreateFromPoints(_boundingVolume, points.size(), points.data(), sizeof(math::Vector3));

		dx::BoundingSphere::CreateFromBoundingBox(_boundingSphere, _boundingVolume);

		if (originalAABB.Extents.y != _boundingVolume.Extents.y)
		{
			LOG_DEBUG("Chunk volume changed from %.1f %.1f %.1f to %.1f %.1f %.1f (pos = %.1f %.1f %.1f)",
				originalAABB.Extents.x, originalAABB.Extents.y, originalAABB.Extents.z,
				_boundingVolume.Extents.x, _boundingVolume.Extents.y, _boundingVolume.Extents.z,
				_boundingVolume.Center.x, _boundingVolume.Center.y, _boundingVolume.Center.z);
		}
	}

	void Chunk::AddChunkEntity(Entity* entity)
	{
		if (std::binary_search(_children.begin(), _children.end(), entity) == false)
		{
			_children.push_back(entity);
		}

		/*if (std::binary_search(_children.begin(), _children.end(), entity) == false)
		{
			_children.push_back(entity);
		}*/

		// sort the list in order of mesh instance
		/*std::sort(_children.begin(), _children.end(),

			[](Entity* left, Entity* right) {

				auto meshLeft = left->GetCachedMeshRenderer();
				auto meshRight = left->GetCachedMeshRenderer();

				if (meshLeft && meshRight)
				{
					auto meshL = meshLeft->GetMesh();
					auto meshR = meshRight->GetMesh();

					if (meshL && meshR)
					{
						auto instanceL = meshL->GetInstance();
						auto instanceR = meshR->GetInstance();

						return instanceL->GetInstanceId() < instanceR->GetInstanceId();
					}
				}
				return false;
			});*/

		RecalculateAABB();
	}

	void Chunk::RemoveChunkEntity(Entity* entity)
	{
		_children.erase(std::remove(_children.begin(), _children.end(), entity), _children.end());

		// we should also remove any mesh components if the parent entity got removed
		for (auto& mesh : entity->GetComponents<StaticMeshComponent>())
		{
			_childrenMeshes.erase(std::remove(_childrenMeshes.begin(), _childrenMeshes.end(), mesh));
		}

		RecalculateAABB();
	}

	void Chunk::AddChunkComponent(Entity* entity, BaseComponent* component)
	{
		if (auto smc = component->CastAs<StaticMeshComponent>(); smc != nullptr)
		{
			_childrenMeshes.push_back(smc);
		}

		RecalculateAABB();
	}

	void Chunk::RemoveChunkComponent(Entity* entity, BaseComponent* component)
	{
		if (auto smc = component->CastAs<StaticMeshComponent>(); smc != nullptr)
		{
			_childrenMeshes.erase(std::remove(_childrenMeshes.begin(), _childrenMeshes.end(), smc));
		}

		RecalculateAABB();
	}

	const dx::BoundingBox& Chunk::GetBoundingVolume() const
	{
		return _boundingVolume;
	}

	const dx::BoundingSphere& Chunk::GetBoundingSphere() const
	{
		return _boundingSphere;
	}
}