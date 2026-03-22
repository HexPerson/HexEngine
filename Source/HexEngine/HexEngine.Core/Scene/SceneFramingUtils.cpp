#include "SceneFramingUtils.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace HexEngine::SceneFramingUtils
{
	bool ComputeSceneBounds(
		Scene* scene,
		math::Vector3& outBoundsMin,
		math::Vector3& outBoundsMax,
		bool ignoreDoNotSaveEntities)
	{
		if (scene == nullptr)
			return false;

		math::Vector3 boundsMin(std::numeric_limits<float>::max());
		math::Vector3 boundsMax(std::numeric_limits<float>::lowest());
		bool hasBounds = false;

		for (const auto& bySignature : scene->GetEntities())
		{
			for (auto* entity : bySignature.second)
			{
				if (entity == nullptr || entity->IsPendingDeletion())
					continue;

				if (ignoreDoNotSaveEntities && entity->HasFlag(EntityFlags::DoNotSave))
					continue;

				bool contributed = false;
				if (auto* meshComponent = entity->GetComponent<StaticMeshComponent>(); meshComponent != nullptr && meshComponent->GetMesh() != nullptr)
				{
					const auto& worldAabb = entity->GetWorldAABB();
					const math::Vector3 center(worldAabb.Center);
					const math::Vector3 extents(worldAabb.Extents);
					const math::Vector3 aabbMin = center - extents;
					const math::Vector3 aabbMax = center + extents;

					boundsMin.x = std::min(boundsMin.x, aabbMin.x);
					boundsMin.y = std::min(boundsMin.y, aabbMin.y);
					boundsMin.z = std::min(boundsMin.z, aabbMin.z);

					boundsMax.x = std::max(boundsMax.x, aabbMax.x);
					boundsMax.y = std::max(boundsMax.y, aabbMax.y);
					boundsMax.z = std::max(boundsMax.z, aabbMax.z);

					hasBounds = true;
					contributed = true;
				}

				if (!contributed)
				{
					const auto pos = entity->GetPosition();
					const math::Vector3 minPos = pos - math::Vector3(0.25f);
					const math::Vector3 maxPos = pos + math::Vector3(0.25f);

					boundsMin.x = std::min(boundsMin.x, minPos.x);
					boundsMin.y = std::min(boundsMin.y, minPos.y);
					boundsMin.z = std::min(boundsMin.z, minPos.z);

					boundsMax.x = std::max(boundsMax.x, maxPos.x);
					boundsMax.y = std::max(boundsMax.y, maxPos.y);
					boundsMax.z = std::max(boundsMax.z, maxPos.z);

					hasBounds = true;
				}
			}
		}

		if (!hasBounds)
			return false;

		outBoundsMin = boundsMin;
		outBoundsMax = boundsMax;
		return true;
	}

	bool FrameCameraToSceneBounds(
		Scene* scene,
		Camera* camera,
		bool ignoreDoNotSaveEntities)
	{
		if (scene == nullptr || camera == nullptr || camera->GetEntity() == nullptr)
			return false;

		math::Vector3 boundsMin;
		math::Vector3 boundsMax;
		if (!ComputeSceneBounds(scene, boundsMin, boundsMax, ignoreDoNotSaveEntities))
			return false;

		const math::Vector3 center = (boundsMin + boundsMax) * 0.5f;
		const math::Vector3 extents = (boundsMax - boundsMin) * 0.5f;
		const float radius = std::max(0.5f, extents.Length());

		const float aspectRatio = std::max(0.01f, camera->GetAspectRatio());
		const float verticalFov = ToRadian(std::clamp(camera->GetFov(), 20.0f, 120.0f));
		const float horizontalFov = 2.0f * std::atan(std::tan(verticalFov * 0.5f) * aspectRatio);
		const float fittingHalfFov = std::max(0.1f, std::min(verticalFov, horizontalFov) * 0.5f);

		float distance = (radius / std::tan(fittingHalfFov)) * 1.15f;
		distance = std::max(distance, camera->GetNearZ() + radius + 0.5f);
		distance = std::min(distance, camera->GetFarZ() * 0.75f);

		math::Vector3 viewDirection(-1.0f, 0.35f, -1.0f);
		viewDirection.Normalize();

		const math::Vector3 cameraPosition = center - (viewDirection * distance);
		camera->GetEntity()->SetPosition(cameraPosition);

		math::Vector3 lookDirection = center - cameraPosition;
		if (lookDirection.Length() < 0.01f)
			lookDirection = math::Vector3::Forward;
		else
			lookDirection.Normalize();

		// Camera forward in this engine is -Z, so yaw/pitch are derived from negative Z-forward.
		const float yaw = ToDegree(std::atan2(-lookDirection.x, -lookDirection.z));
		const float pitch = ToDegree(std::asin(std::clamp(lookDirection.y, -1.0f, 1.0f)));

		camera->SetYaw(yaw);
		camera->SetPitch(pitch);
		camera->SetRoll(0.0f);
		camera->Update(0.0f);
		return true;
	}
}
