

#include "Chunk.hpp"
#include "../Entity/Component/StaticMeshComponent.hpp"
#include "../Math/FloatMath.hpp"
#include "../Environment/IEnvironment.hpp"
#include "ChunkManager.hpp"
#include "../Environment/LogFile.hpp"
#include "../FileSystem/FileSystem.hpp"
#include "../Scene/SceneManager.hpp"
#include "../Environment/TimeManager.hpp"

namespace HexEngine
{
	Chunk::Chunk(const dx::BoundingBox& box, int32_t id) :
		_boundingVolume(box),
		_id(id)
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

		/*math::Vector3 corners[8];
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
		}*/

		_lock.lock();
		for (auto&& child : _childrenMeshes)
		{
			auto entity = child->GetEntity();

			if (entity->GetLayer() == Layer::Sky)
				continue;
			if (entity->GetName().rfind("HLOD_", 0) == 0)
				continue;

			const auto& bbox = entity->GetWorldAABB();

			dx::BoundingBox tempBox;
			dx::BoundingBox::CreateMerged(tempBox, bbox, aabb);

			/*if (bbox.Center.y + bbox.Extents.y > aabb.Center.y + aabb.Extents.y)
			{
				aabb.Extents.y = ((bbox.Center.y + bbox.Extents.y) - aabb.Center.y) / 2.0f;
			}
			else if (bbox.Center.y - bbox.Extents.y < aabb.Center.y - aabb.Extents.y)
			{
				aabb.Extents.y = ((bbox.Center.y + bbox.Extents.y) - aabb.Center.y) / 2.0f;
			}*/

			aabb.Center.y = tempBox.Center.y;
			aabb.Extents.y = tempBox.Extents.y;
		}
		_lock.unlock();

		_boundingVolume = aabb;

		//dx::BoundingBox::CreateFromPoints(_boundingVolume, points.size(), points.data(), sizeof(math::Vector3));

		dx::BoundingSphere::CreateFromBoundingBox(_boundingSphere, _boundingVolume);

		if (originalAABB.Extents.y != _boundingVolume.Extents.y)
		{
			LOG_DEBUG("Chunk volume %p changed from %.1f %.1f %.1f to %.1f %.1f %.1f (pos = %.1f %.1f %.1f)",
				this,
				originalAABB.Extents.x, originalAABB.Extents.y, originalAABB.Extents.z,
				_boundingVolume.Extents.x, _boundingVolume.Extents.y, _boundingVolume.Extents.z,
				_boundingVolume.Center.x, _boundingVolume.Center.y, _boundingVolume.Center.z);
		}
	}

	void Chunk::Lock()
	{
		_lock.lock();
	}

	void Chunk::Unlock()
	{
		_lock.unlock();
	}

	void Chunk::AddChunkEntity(Entity* entity)
	{
		std::unique_lock lock(_lock);

		entity->SetChunk(this);

		if (std::find(_children.begin(), _children.end(), entity) == _children.end())
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

		if(g_pEnv->_chunkManager->_continuousCalculationEnabled)
			RecalculateAABB();
	}

	void Chunk::RemoveChunkEntity(Entity* entity)
	{
		std::unique_lock lock(_lock);

		entity->SetChunk(nullptr);

		_children.erase(std::remove(_children.begin(), _children.end(), entity), _children.end());

		// we should also remove any mesh components if the parent entity got removed
		for (auto& mesh : entity->GetComponents<StaticMeshComponent>())
		{
			if (std::find(_childrenMeshes.begin(), _childrenMeshes.end(), mesh) != _childrenMeshes.end())
			{
				_childrenMeshes.erase(std::remove(_childrenMeshes.begin(), _childrenMeshes.end(), mesh));
			}			
		}

		if (g_pEnv->_chunkManager->_continuousCalculationEnabled)
			RecalculateAABB();
	}

	void Chunk::AddChunkComponent(Entity* entity, BaseComponent* component)
	{
		std::unique_lock lock(_lock);

		if (auto smc = component->CastAs<StaticMeshComponent>(); smc != nullptr)
		{
			_childrenMeshes.push_back(smc);
		}

		AddChunkEntity(entity);

		if (g_pEnv->_chunkManager->_continuousCalculationEnabled)
			RecalculateAABB();
	}

	void Chunk::RemoveChunkComponent(Entity* entity, BaseComponent* component)
	{
		std::unique_lock lock(_lock);

		if (auto smc = component->CastAs<StaticMeshComponent>(); smc != nullptr)
		{
			if (std::find(_childrenMeshes.begin(), _childrenMeshes.end(), smc) != _childrenMeshes.end())
			{
				_childrenMeshes.erase(std::remove(_childrenMeshes.begin(), _childrenMeshes.end(), smc));
			}
		}

		RemoveChunkEntity(entity);

		if (g_pEnv->_chunkManager->_continuousCalculationEnabled)
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

	void Chunk::CalculateChunkStats_UInt32(std::vector<math::Vector3>& vertices, std::vector<uint32_t>& indices, uint32_t& numFaces, EntityFlags excludeFlags)
	{
		std::unique_lock lock(_lock);

		numFaces = 0;

		uint32_t indexOffset = 0;

		for (auto& smc : _childrenMeshes)
		{
			auto mesh = smc->GetMesh();

			if (!mesh)
				continue;

			auto entity = smc->GetEntity();

			if (!entity)
				continue;

			if (entity->HasFlag(excludeFlags))
				continue;

			auto verts = mesh->GetVertices();
			auto inds = mesh->GetIndices();

			numFaces += mesh->GetNumFaces();

			auto entPos = smc->GetEntity()->GetPosition();

			for (auto& v : verts)
			{
				vertices.push_back(entPos + *(math::Vector3*)&v._position.x);
			}

			for (auto& i : inds)
			{
				indices.push_back((int)i + indexOffset);
			}

			indexOffset += verts.size();
		}
	}

	void Chunk::WriteToDisk()
	{
		if (_isCached)
			return;

		//if (g_pEnv->_timeManager->_currentTime - _cachedTime < 5.0f)
		//	return;

		if (_hasChanged)
		{
			SceneSaveFile file(g_pEnv->GetFileSystem().GetLocalAbsoluteDataPath(std::format("Chunks/Chunk_{}.hscene", _id)), std::ios::out | std::ios::trunc, g_pEnv->_sceneManager->GetCurrentScene(), SceneFileFlags::DontSaveVariables);

			file.Save(_children);
			file.Close();

			_hasChanged = false;
		}

		_lock.lock();
		{
			bool oldValue = g_pEnv->_chunkManager->IsContinuousChunkBoundCalculationEnabled();
			g_pEnv->_chunkManager->EnableContinuousChunkBoundCalculation(false);

			while(_children.size() > 0)
			{
				auto child = _children.front();

				g_pEnv->_sceneManager->GetCurrentScene()->DestroyEntity(child, false);
			}


			_children.clear();
			_childrenMeshes.clear();

			g_pEnv->_chunkManager->EnableContinuousChunkBoundCalculation(oldValue);
		}
		_lock.unlock();

		_isCached = true;
		_cachedTime = g_pEnv->_timeManager->_currentTime;
	}

	void Chunk::ReadFromDisk()
	{
		if (_isCached == false)
			return;

		//if (g_pEnv->_timeManager->_currentTime - _cachedTime < 5.0f)
		//	return;

		SceneSaveFile file(g_pEnv->GetFileSystem().GetLocalAbsoluteDataPath(std::format("Chunks/Chunk_{}.hscene", _id)), std::ios::in, g_pEnv->_sceneManager->GetCurrentScene(), SceneFileFlags::DontSaveVariables);

		file.Load();

		_isCached = false;
		//_cachedTime = g_pEnv->_timeManager->_currentTime;
	}
}
