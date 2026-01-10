
#pragma once

#include "../Required.hpp"
#include "../Environment/IEnvironment.hpp"
#include "IPhysicsSystem.hpp"
#include "../Entity/Entity.hpp"

namespace HexEngine
{
	namespace PhysUtils
	{
		bool HEX_API RayCast(const math::Vector3& from, const math::Vector3& to, LayerMask mask, RayHit* hitInfo, const std::vector<Entity*>& entsToIgnore = {});

		bool HEX_API RayCast(const math::Ray& ray, float maxDistance, LayerMask mask, RayHit* hitInfo, const std::vector<Entity*>& entsToIgnore = {});

		//bool CameraPickEntity(const math::Vector3& ray, RayHit& hit, uint32_t layerMask = (uint32_t)Layer::AllLayers);

		//bool PickEntity(const math::Vector3& from, const math::Vector3& ray, RayHit& hit, uint32_t layerMask, float maxDistance = 0.0f);

		//Entity* RayCast(const math::Ray& ray, float traceLength, math::Vector3& worldEndPos, uint32_t signature, Entity* ignore = nullptr);
	}
}
