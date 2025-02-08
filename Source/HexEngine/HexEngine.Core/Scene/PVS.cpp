
#include "PVS.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../Terrain/ChunkManager.hpp"
#include "../Entity/Component/StaticMeshComponent.hpp"
#include "Scene.hpp"

namespace HexEngine
{
	void PVS::ClearPVS()
	{
		std::unique_lock lock(_lock);

		auto oldSize = _pvs.size();
		_pvs.clear();
		//_pvs.reserve(oldSize);

		_forceRebuild = true;
	}

	const PVS::MeshInstanceMap& PVS::GetRenderables() const
	{
		return _pvs;
	}

	/*bool SortThird(const PVS::MeshEntityPair& left, const PVS::MeshEntityPair& right)
	{

	}

	bool SortSecond(const PVS::MeshEntityVector& left, const PVS::MeshEntityVector& right)
	{
		return SortThird(left. right);
	}

	bool SortFirst(const PVS::MaterialEntityVectorPair& left, const PVS::MaterialEntityVectorPair& right)
	{
		return SortSecond(left.second, right.second);
	}*/

	void PVS::ForceRebuild()
	{
		_forceRebuild = true;
	}

	bool PVS::NeedsRebuild() const
	{
		return _forceRebuild;
	}

	void PVS::CalculateVisibility(Scene* scene, const PVSParams& params)
	{
		std::unique_lock lock(_lock);

		//scene->Lock();

		bool needsRebuild = _forceRebuild;

		if (_hasBuildOptimisation)
		{
			switch (params.shapeType)
			{
			case PVSParams::ShapeType::Frustum:
				if (_optimisedParams.shape.sphere.Contains(params.shape.frustum) != dx::ContainmentType::CONTAINS)
					needsRebuild = true;
				break;

			case PVSParams::ShapeType::Sphere:
				if (_optimisedParams.shape.sphere.Contains(params.shape.sphere) != dx::ContainmentType::CONTAINS)
					needsRebuild = true;
				break;
			}
		}

		if (needsRebuild)
			_needsOptimisationRebuild = true;

		if (_needsOptimisationRebuild)
		{
			memcpy(&_optimisedParams, &params, sizeof(PVSParams));

			switch (params.shapeType)
			{
			case PVSParams::ShapeType::Frustum:
				dx::BoundingSphere::CreateFromFrustum(_optimisedParams.shape.sphere, params.shape.frustum);
				_optimisedParams.shape.sphere.Radius *= 1.2f;

				/*_optimisedParams.shape.frustum.Transform(_optimisedParams.shape.frustum, math::Matrix::CreateScale(1.2f));
				_optimisedParams.shape.frustum.Near = params.shape.frustum.Near;
				_optimisedParams.shape.frustum.Far = params.shape.frustum.Far;*/
				break;

			case PVSParams::ShapeType::Sphere:
				//_optimisedParams.shape.sphere.Transform(_optimisedParams.shape.sphere, math::Matrix::CreateScale(1.2f));
				_optimisedParams.shape.sphere.Radius *= 1.2f;
				break;
			}

			_needsOptimisationRebuild = false;
			_hasBuildOptimisation = true;
		}

		if (!needsRebuild)
		{
			//scene->Unlock();
			return;
		}

		ClearPVS();		

		_forceRebuild = false;

		Scene::EntityComponentVector componentIterator;

		if (g_pEnv->_chunkManager->HasActiveChunks(scene))
		{
			g_pEnv->_chunkManager->CalculatePVS(scene, this, params, componentIterator);
		}
		else
		{
			scene->GetComponents(1 << StaticMeshComponent::_GetComponentId(), componentIterator);
		}

		/*std::sort(componentIterator.begin(), componentIterator.end(), 
			
			[](Entity* ent1, Entity* ent2) {

				auto r1 = ent1->GetCachedMeshRenderer();
				auto r2 = ent2->GetCachedMeshRenderer();



				
			}*/


		auto end = _pvs.end();
		//auto it = end;

		for (auto&& component : componentIterator)
		{
			auto entity = component->GetEntity();

			if (entity->GetLayer() == Layer::Invisible || entity->GetLayer() == Layer::Trigger)
				continue;

			// don't bother rendering entities into the shadow map that can't receive shadows
			//
			if (entity->GetCastsShadows() == false)
				continue;

			auto meshComponent = entity->GetCachedMeshRenderer();

			if (meshComponent)
			{
				bool visible = false;
				
				//if (meshComponent->GetDepthState() == DepthBufferState::DepthNone)
				//	visible = true;
				//else
					visible = IsEntityVisible(entity, params);

				if (!visible)
					continue;

				auto mesh = meshComponent->GetMesh();

				if (!mesh)
					continue;

				if (auto lod = mesh->GetLodLevel(); lod != -1)
				{
					//if (lod < r_shadowMinimumLodThreshold._val.i32)
					//	continue;

					if (params.forceMaxLod)
					{
						if (lod < mesh->GetMaxLodLevel())
							continue;
					}

					const float lodPartitions = params.lodPartition;

					float minDistance = lodPartitions * (float)(mesh->GetLodLevel());
					float maxDistance = lodPartitions * (float)(mesh->GetLodLevel() + 1);

					float distance = (entity->GetPosition() - scene->GetMainCamera()->GetEntity()->GetPosition()).Length();

					if (mesh->GetLodLevel() < 3)
					{
						// always allow level 3
						if (distance < minDistance || distance > maxDistance)
						{
							continue;
						}
					}
					else
					{
						if (distance < minDistance)
						{
							continue;
						}
					}
				}

				auto material = meshComponent->GetMaterial();

				if (!material)
					continue;

				auto meshInstance = mesh->GetInstance();

				if (!meshInstance)
					continue;

				/*it = std::find_if(_pvs.begin(), _pvs.end(), [material, mesh](const MaterialEntityVectorPair& p) {
					if (p.first == nullptr)
						return false;
					return p.first->Equals(*material);
					});*/

				//if (it == _pvs.end())
				{
					//MaterialEntityVectorPair newEntry = { material, {} };

					auto& it = _pvs[material];

					if (it.size() == 0)
						it.reserve(256);

					_pvs[material].push_back({ mesh,entity });
					//_pvs.push_back(newEntry);

					//it = _pvs.end() - 1;

					//it->second.reserve(256);
				}
				{
					/*bool found = false;
					for (auto&& pair : it->second)
					{
						if (pair.second == entity && pair.first == mesh)
						{
							found = true;
							break;
						}
					}
					if (!found)*/
					{
						//it->second.push_back({ mesh,entity });

						
					}
				}
			}
		}

		for (auto& mevp : _pvs)
		{
			auto& mev = mevp.second;

			std::sort(mev.begin(), mev.end(),
				[](MeshEntityPair& left, MeshEntityPair& right)
				{
					return left.first->GetInstance()->GetInstanceId() < right.first->GetInstance()->GetInstanceId();
				}
			);
		}
		//std::sort(_pvs.begin(), _pvs.end(),
	}

	bool PVS::IsEntityVisible(Entity* entity, const PVSParams& params)
	{
		return IsShapeVisible(entity->GetWorldAABB(), params);
	}

	bool PVS::IsShapeVisible(const dx::BoundingBox& bbox, const PVSParams& params)
	{
		switch (params.shapeType)
		{
		case PVSParams::ShapeType::Frustum:
			return _optimisedParams.shape.sphere.Intersects(bbox);

		case PVSParams::ShapeType::Sphere:
			return _optimisedParams.shape.sphere.Intersects(bbox);

		default:
			return false;
		}
	}

	bool PVS::IsShapeVisible(const dx::BoundingSphere& bsphere, const PVSParams& params)
	{
		switch (params.shapeType)
		{
		case PVSParams::ShapeType::Frustum:
			return _optimisedParams.shape.sphere.Intersects(bsphere);

		case PVSParams::ShapeType::Sphere:
			return _optimisedParams.shape.sphere.Intersects(bsphere);

		default:
			return false;
		}
	}

	void PVS::AddEntity(Entity* entity)
	{
		_forceRebuild = true;
	}

	void PVS::RemoveEntity(Entity* entity)
	{
		for (auto it = _pvs.begin(); it != _pvs.end(); it++)
		{
			it->second.erase(std::remove_if(it->second.begin(), it->second.end(),
				[entity](const MeshEntityPair& pair) {
					return pair.second == entity;
				}), it->second.end());

			/*for (auto& mep : it->second)
			{
				if (mep.second == entity)
				{

				}
			}*/
		}
	}

	const PVSParams& PVS::GetOptimisedParams() const
	{
		return _optimisedParams;
	}
}