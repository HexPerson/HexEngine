#include "TrafficManagerComponent.hpp"
#include "Camera.hpp"
#include "TrafficLaneComponent.hpp"
#include "TrafficSpawnerComponent.hpp"
#include "TrafficVehicleComponent.hpp"
#include "../Entity.hpp"
#include "../../GUI/Elements/Checkbox.hpp"
#include "../../GUI/Elements/ComponentWidget.hpp"
#include "../../GUI/Elements/DragFloat.hpp"
#include "../../GUI/Elements/DragInt.hpp"
#include "../../Math/FloatMath.hpp"
#include "../../Scene/Scene.hpp"
#include "../../Scene/SceneManager.hpp"
#include <algorithm>

namespace HexEngine
{
	namespace
	{
		bool IsSpawnPointClear(Scene* scene, const math::Vector3& spawnPoint, float minDistance)
		{
			if (scene == nullptr || minDistance <= 0.0f)
				return true;

			std::vector<TrafficVehicleComponent*> vehicles;
			scene->GetComponents<TrafficVehicleComponent>(vehicles);
			const float minDistanceSq = minDistance * minDistance;

			for (auto* vehicle : vehicles)
			{
				if (vehicle == nullptr)
					continue;

				auto* entity = vehicle->GetEntity();
				if (entity == nullptr || entity->IsPendingDeletion())
					continue;

				if ((entity->GetWorldTM().Translation() - spawnPoint).LengthSquared() < minDistanceSq)
					return false;
			}

			return true;
		}
	}

	TrafficManagerComponent::TrafficManagerComponent(Entity* entity) :
		UpdateComponent(entity)
	{
	}

	TrafficManagerComponent::TrafficManagerComponent(Entity* entity, TrafficManagerComponent* copy) :
		UpdateComponent(entity, copy)
	{
		if (copy != nullptr)
		{
			_enabled = copy->_enabled;
			_globalMaxActiveVehicles = copy->_globalMaxActiveVehicles;
			_globalActivationDistance = copy->_globalActivationDistance;
			_drawDebug = copy->_drawDebug;
		}
	}

	void TrafficManagerComponent::Serialize(json& data, JsonFile* file)
	{
		file->Serialize(data, "_enabled", _enabled);
		file->Serialize(data, "_globalMaxActiveVehicles", _globalMaxActiveVehicles);
		file->Serialize(data, "_globalActivationDistance", _globalActivationDistance);
		file->Serialize(data, "_drawDebug", _drawDebug);
	}

	void TrafficManagerComponent::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		file->Deserialize(data, "_enabled", _enabled);
		file->Deserialize(data, "_globalMaxActiveVehicles", _globalMaxActiveVehicles);
		file->Deserialize(data, "_globalActivationDistance", _globalActivationDistance);
		file->Deserialize(data, "_drawDebug", _drawDebug);
	}

	bool TrafficManagerComponent::CreateWidget(ComponentWidget* widget)
	{
		auto* enabled = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Enabled", &_enabled);
		auto* drawDebug = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Draw Debug", &_drawDebug);
		auto* globalMaxVehicles = new DragInt(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Global Max Vehicles", &_globalMaxActiveVehicles, 1, 100000, 1);
		auto* globalActivationDistance = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Global Activation Dist", &_globalActivationDistance, 1.0f, 100000.0f, 1.0f, 1);

		enabled->SetPrefabOverrideBinding(GetComponentName(), "/_enabled");
		drawDebug->SetPrefabOverrideBinding(GetComponentName(), "/_drawDebug");
		globalMaxVehicles->SetPrefabOverrideBinding(GetComponentName(), "/_globalMaxActiveVehicles");
		globalActivationDistance->SetPrefabOverrideBinding(GetComponentName(), "/_globalActivationDistance");
		return true;
	}

	int32_t TrafficManagerComponent::DespawnSpawnerVehicles(TrafficSpawnerComponent* spawner)
	{
		int32_t removedCount = 0;
		auto& activeVehicles = spawner->ActiveVehicles();
		for (auto* entity : activeVehicles)
		{
			if (entity == nullptr || entity->IsPendingDeletion())
				continue;

			entity->DeleteMe();
			++removedCount;
		}
		activeVehicles.clear();
		return removedCount;
	}

	int32_t TrafficManagerComponent::SpawnVehicleFromSpawner(TrafficSpawnerComponent* spawner)
	{
		auto* scene = GetEntity()->GetScene();
		if (scene == nullptr)
			return 0;

		if (spawner->GetLaneEntityName().empty())
			return 0;

		auto* laneEntity = scene->GetEntityByName(spawner->GetLaneEntityName());
		if (laneEntity == nullptr || laneEntity->IsPendingDeletion())
			return 0;

		auto* lane = laneEntity->GetComponent<TrafficLaneComponent>();
		if (lane == nullptr)
			return 0;

		std::vector<math::Vector3> lanePoints;
		lane->GatherLanePoints(lanePoints);
		if (lanePoints.size() < 2)
			return 0;

		if (!IsSpawnPointClear(scene, lanePoints.front(), spawner->GetSpawnClearanceDistance()))
			return 0;

		const auto& prefabPaths = spawner->GetVehiclePrefabPaths();
		if (prefabPaths.empty())
			return 0;

		std::vector<std::string> validPrefabPaths;
		validPrefabPaths.reserve(prefabPaths.size());
		for (const auto& path : prefabPaths)
		{
			if (!path.empty())
				validPrefabPaths.push_back(path);
		}
		if (validPrefabPaths.empty())
			return 0;

		const int32_t randomIndex = GetRandomInt(0, static_cast<int32_t>(validPrefabPaths.size()) - 1);
		const fs::path prefabPath = validPrefabPaths[static_cast<size_t>(randomIndex)];

		auto spawnedEntities = g_pEnv->_sceneManager->LoadPrefab(g_pEnv->_sceneManager->GetCurrentScene(), prefabPath);
		if (spawnedEntities.empty())
			return 0;

		std::vector<Entity*> rootEntities;
		rootEntities.reserve(spawnedEntities.size());
		for (auto* spawned : spawnedEntities)
		{
			if (spawned == nullptr || spawned->IsPendingDeletion())
				continue;

			if (spawned->GetParent() == nullptr)
				rootEntities.push_back(spawned);
		}

		if (rootEntities.empty())
		{
			for (auto* spawned : spawnedEntities)
			{
				if (spawned != nullptr && !spawned->IsPendingDeletion())
				{
					rootEntities.push_back(spawned);
					break;
				}
			}
		}

		int32_t spawnedVehicleCount = 0;
		for (auto* root : rootEntities)
		{
			if (root == nullptr || root->IsPendingDeletion())
				continue;

			root->ForcePosition(lanePoints.front());

			auto* vehicle = root->GetComponent<TrafficVehicleComponent>();
			if (vehicle == nullptr)
				vehicle = root->AddComponent<TrafficVehicleComponent>();

			vehicle->SetLaneEntityName(spawner->GetLaneEntityName());
			vehicle->RestartPath();

			spawner->ActiveVehicles().push_back(root);
			++spawnedVehicleCount;
		}

		return spawnedVehicleCount;
	}

	void TrafficManagerComponent::UpdateSpawner(TrafficSpawnerComponent* spawner, Camera* camera, float frameTime, int32_t& activeGlobalCount)
	{
		if (spawner == nullptr)
			return;

		if (!spawner->IsEnabled())
			return;

		const math::Vector3 cameraPosition = camera->GetEntity()->GetPosition();
		const math::Vector3 spawnerPosition = spawner->GetEntity()->GetWorldTM().Translation();
		const float distanceToSpawner = (spawnerPosition - cameraPosition).Length();
		const float activationDistance = std::min(spawner->GetActivationDistance(), _globalActivationDistance);
		const bool active = distanceToSpawner <= std::max(activationDistance, 0.0f);

		if (!active)
		{
			if (spawner->ShouldDespawnWhenInactive())
				activeGlobalCount = std::max(0, activeGlobalCount - DespawnSpawnerVehicles(spawner));
			return;
		}

		if (static_cast<int32_t>(spawner->ActiveVehicles().size()) >= spawner->GetMaxActiveVehicles())
			return;

		if (activeGlobalCount >= _globalMaxActiveVehicles)
			return;

		float& timer = spawner->SpawnTimerRef();
		timer += frameTime;

		const float interval = std::max(spawner->GetSpawnIntervalSeconds(), 0.01f);
		if (timer < interval)
			return;

		timer = 0.0f;
		const int32_t spawnedVehicles = SpawnVehicleFromSpawner(spawner);
		if (spawnedVehicles > 0)
		{
			activeGlobalCount += spawnedVehicles;
		}
	}

	void TrafficManagerComponent::Update(float frameTime)
	{
		UpdateComponent::Update(frameTime);

		if (!_enabled)
			return;

		auto* scene = GetEntity()->GetScene();
		if (scene == nullptr)
			return;

		auto* camera = scene->GetMainCamera();
		if (camera == nullptr || camera->GetEntity() == nullptr)
			return;

		std::vector<TrafficSpawnerComponent*> spawners;
		scene->GetComponents<TrafficSpawnerComponent>(spawners);

		std::vector<TrafficVehicleComponent*> activeVehicles;
		scene->GetComponents<TrafficVehicleComponent>(activeVehicles);
		int32_t activeGlobalCount = static_cast<int32_t>(activeVehicles.size());
		for (auto* spawner : spawners)
		{
			if (spawner == nullptr || spawner->GetEntity() == nullptr || spawner->GetEntity()->IsPendingDeletion())
				continue;

			UpdateSpawner(spawner, camera, frameTime, activeGlobalCount);
		}
	}
}
