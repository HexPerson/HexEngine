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

		math::Vector3 meshBoundsMin(std::numeric_limits<float>::max());
		math::Vector3 meshBoundsMax(std::numeric_limits<float>::lowest());
		bool hasMeshBounds = false;

		math::Vector3 fallbackBoundsMin(std::numeric_limits<float>::max());
		math::Vector3 fallbackBoundsMax(std::numeric_limits<float>::lowest());
		bool hasFallbackBounds = false;

		for (const auto& bySignature : scene->GetEntities())
		{
			for (auto* entity : bySignature.second)
			{
				if (entity == nullptr || entity->IsPendingDeletion())
					continue;

				if (ignoreDoNotSaveEntities && entity->HasFlag(EntityFlags::DoNotSave))
					continue;

				if (auto* meshComponent = entity->GetComponent<StaticMeshComponent>(); meshComponent != nullptr && meshComponent->GetMesh() != nullptr)
				{
					if (entity->HasFlag(EntityFlags::DoNotRender))
						continue;

					const auto& worldAabb = entity->GetWorldAABB();
					const math::Vector3 center(worldAabb.Center);
					const math::Vector3 extents(worldAabb.Extents);
					const math::Vector3 aabbMin = center - extents;
					const math::Vector3 aabbMax = center + extents;

					meshBoundsMin.x = std::min(meshBoundsMin.x, aabbMin.x);
					meshBoundsMin.y = std::min(meshBoundsMin.y, aabbMin.y);
					meshBoundsMin.z = std::min(meshBoundsMin.z, aabbMin.z);

					meshBoundsMax.x = std::max(meshBoundsMax.x, aabbMax.x);
					meshBoundsMax.y = std::max(meshBoundsMax.y, aabbMax.y);
					meshBoundsMax.z = std::max(meshBoundsMax.z, aabbMax.z);

					hasMeshBounds = true;
					continue;
				}

				const auto worldPosition = entity->GetWorldTM().Translation();
				const math::Vector3 minPos = worldPosition - math::Vector3(0.25f);
				const math::Vector3 maxPos = worldPosition + math::Vector3(0.25f);

				fallbackBoundsMin.x = std::min(fallbackBoundsMin.x, minPos.x);
				fallbackBoundsMin.y = std::min(fallbackBoundsMin.y, minPos.y);
				fallbackBoundsMin.z = std::min(fallbackBoundsMin.z, minPos.z);

				fallbackBoundsMax.x = std::max(fallbackBoundsMax.x, maxPos.x);
				fallbackBoundsMax.y = std::max(fallbackBoundsMax.y, maxPos.y);
				fallbackBoundsMax.z = std::max(fallbackBoundsMax.z, maxPos.z);

				hasFallbackBounds = true;
			}
		}

		if (hasMeshBounds)
		{
			outBoundsMin = meshBoundsMin;
			outBoundsMax = meshBoundsMax;
			return true;
		}

		if (hasFallbackBounds)
		{
			outBoundsMin = fallbackBoundsMin;
			outBoundsMax = fallbackBoundsMax;
			return true;
		}

		return false;
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

		math::Vector3 viewDirection(-1.0f, -0.65f, -1.0f);
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
