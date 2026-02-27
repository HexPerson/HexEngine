
#include "PVS.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../Terrain/ChunkManager.hpp"
#include "../Entity/Component/StaticMeshComponent.hpp"
#include "../Entity/Component/SkeletalAnimationComponent.hpp"
#include "Scene.hpp"
#include "../Input/HVar.hpp"

namespace HexEngine
{
	HVar r_grassCullDist("r_grassCullDist", "The distance at which to cull grass", 1000.0f, 0.0f, 10000.0f);

	void PVS::ClearPVS()
	{
		std::unique_lock lock(_lock);

		auto oldSize = _pvs.size();
		_pvs.clear();
		//_pvs.reserve(oldSize);

		_forceRebuild = true;

		_totalEnts = 0;
		_totalSkeletalAnimators = 0;
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
				if (_optimisedParams.shape.frustum.sm.Contains(params.shape.frustum.sm) != dx::ContainmentType::CONTAINS)
					needsRebuild = true;
				break;

			case PVSParams::ShapeType::Frustum2:
				if (_optimisedParams.shape.frustum.lg.Contains(params.shape.frustum.sm) != dx::ContainmentType::CONTAINS)
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
			{
				//dx::BoundingSphere::CreateFromFrustum(_optimisedParams.shape.sphere, params.shape.frustum);
				//_optimisedParams.shape.sphere.Radius *= 1.05f;

				//_optimisedParams.shape.frustum.Transform(_optimisedParams.shape.frustum, math::Matrix::CreateScale(1.2f));

				//_optimisedParams.shape.frustum.Origin = params.shape.frustum.Origin;
				//_optimisedParams.shape.frustum.Orientation = params.shape.frustum.Orientation;

				//_optimisedParams.shape.frustum.Near *= 2.2f;//params.shape.frustum.Near;
				//_optimisedParams.shape.frustum.Far = params.shape.frustum.Far;

				//_optimisedParams.shape.frustum.RightSlope	*= 2.2f;
				//_optimisedParams.shape.frustum.LeftSlope	*= 2.2f;
				//_optimisedParams.shape.frustum.TopSlope		*= 2.2f;
				//_optimisedParams.shape.frustum.BottomSlope	*= 2.2f;
				//_optimisedParams.shape.frustum.Near *= 2.2f;
				////_optimisedParams.shape.frustum.Far *= 2.2f;

				//math::Vector3 o = _optimisedParams.shape.frustum.Origin;
				//math::Quaternion d = _optimisedParams.shape.frustum.Orientation;
				//o -= d.ToEuler() * 100.0f;

				//_optimisedParams.shape.frustum.Origin = o;
				
				break;
			}

			case PVSParams::ShapeType::Sphere:
				//_optimisedParams.shape.sphere.Transform(_optimisedParams.shape.sphere, math::Matrix::CreateScale(1.2f));
				_optimisedParams.shape.sphere.Radius *= 1.25f;
				break;
			}

			_needsOptimisationRebuild = false;
			_hasBuildOptimisation = true;
		}

		if (!needsRebuild)
		{
			return;
		}

		ClearPVS();			

		_forceRebuild = false;

		std::vector<StaticMeshComponent*> components;

		if (g_pEnv->_chunkManager->HasActiveChunks(scene))
		{
			g_pEnv->_chunkManager->CalculatePVS(scene, this, params, components);
		}
		else
		{
			scene->GetComponents<StaticMeshComponent>(components);
		}

		/*std::sort(components.begin(), components.end(),

			[params](StaticMeshComponent* ent1, StaticMeshComponent* ent2) {

				float distance1 = (params.camera->GetEntity()->GetPosition() - ent1->GetEntity()->GetPosition()).Length();
				float distance2 = (params.camera->GetEntity()->GetPosition() - ent2->GetEntity()->GetPosition()).Length();

				return distance1 < distance2;
			});*/


		auto end = _pvs.end();
		//auto it = end;

		for (auto&& component : components)
		{
			auto entity = component->GetEntity();

			if (entity->GetLayer() == Layer::Invisible || entity->GetLayer() == Layer::Trigger || entity->HasFlag(EntityFlags::DoNotRender))
				continue;

			// don't bother rendering entities into the shadow map that can't receive shadows
			//
			if (params.isShadow)
			{
				if (entity->GetCastsShadows() == false)
					continue;
			}

			// always draw grass if its in the grass radius
#if 0
			if (entity->GetLayer() == Layer::Grass && params.isShadow == false)
			{
				//if ((params.camera->GetEntity()->GetPosition() - entity->GetPosition()).Length() <= r_grassCullDist._val.f32)
				{
					auto material = component->GetMaterial();

					if (!material)
						continue;

					auto mesh = component->GetMesh();

					if (!mesh)
						continue;

					auto meshInstance = mesh->GetInstance();

					if (!meshInstance)
						continue;

					auto& it = _pvs[material];

					if (it.size() == 0)
						it.reserve(1000);

					_totalEnts++;

					it.push_back({ mesh, entity, component });
					continue;
				}
				//else
				//	continue;
			}
#endif
			

			// early cull distance
			//if (params.camera)
			//{
			//	// because we earlier sorted the components by distance, if this check fails we can just exit the function because we know no more entities should be visible
			//	if ((params.camera->GetEntity()->GetPosition() - entity->GetPosition()).Length() - entity->GetWorldBoundingSphere().Radius >= params.camera->GetFarZ())
			//	{
			//		if (entity->IsInPVS() && params.isShadow == false)
			//		{
			//			PVSVisibilityChangedMessage pvsMsg;
			//			pvsMsg.visible = false;
			//			entity->OnMessage(&pvsMsg, nullptr);
			//		}
			//		continue;
			//	}{}
			//}

			if (component)
			{
				bool visible = IsEntityVisible(entity, params);

				if (entity->IsInPVS() != visible && params.isShadow == false)
				{
					PVSVisibilityChangedMessage pvsMsg;
					pvsMsg.visible = visible;
					entity->OnMessage(&pvsMsg, nullptr);
				}

				if (!visible)
					continue;

				auto mesh = component->GetMesh();

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

				auto material = component->GetMaterial();

				if (!material)
					continue;

				auto meshInstance = mesh->GetInstance();

				if (!meshInstance)
					continue;

				auto& it = _pvs[material];

				if (it.size() == 0)
					it.reserve(256);

				_totalEnts++;

				if (entity->HasA<SkeletalAnimationComponent>())
					_totalSkeletalAnimators++;

				_pvs[material].push_back({ mesh, entity, component });

			}
		}

		//if (params.isShadow == false)
		{
			for (auto& mevp : _pvs)
			{
				auto& mev = mevp.second;

				std::sort(mev.begin(), mev.end(),
					[](MeshEntityPair& left, MeshEntityPair& right)
					{
						return std::get<0>(left)->GetInstance()->GetInstanceId() < std::get<0>(right)->GetInstance()->GetInstanceId();
					}
				);
			}
		}
		//std::sort(_pvs.begin(), _pvs.end(),
	}

	bool PVS::IsEntityVisible(Entity* entity, const PVSParams& params)
	{
		if (entity->GetLayer() == Layer::Sky)
			return true;

		return IsShapeVisible(entity->GetWorldBoundingSphere(), params);
	}

	bool PVS::IsShapeVisible(const dx::BoundingBox& bbox, const PVSParams& params)
	{
		switch (params.shapeType)
		{
		case PVSParams::ShapeType::Frustum:
			return _optimisedParams.shape.frustum.sm.Intersects(bbox);

		case PVSParams::ShapeType::Frustum2:
			return _optimisedParams.shape.frustum.lg.Intersects(bbox);

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
		case PVSParams::ShapeType::Frustum2:
			return _optimisedParams.shape.frustum.sm.Intersects(bsphere);

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
					return std::get<1>(pair) == entity;
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