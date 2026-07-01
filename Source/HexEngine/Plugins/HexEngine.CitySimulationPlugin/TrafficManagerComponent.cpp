#include "TrafficManagerComponent.hpp"
#include "TrafficLaneComponent.hpp"
#include "TrafficSpawnerComponent.hpp"
#include "TrafficVehicleComponent.hpp"
#include <HexEngine.Core/FileSystem/PrefabLoader.hpp>

namespace
{
	// Use the spatial grid (built once per frame in TrafficManager::Update)
	// instead of scene->GetComponents<TrafficVehicleComponent>(), which
	// allocates a fresh vector and scans the full ECS each spawn attempt.
	bool IsSpawnPointClear(const math::Vector3& spawnPoint, float minDistance)
	{
		if (minDistance <= 0.0f)
			return true;

		thread_local std::vector<TrafficVehicleComponent*> nearby;
		TrafficVehicleComponent::QueryNearbyVehicles(spawnPoint, minDistance + 2.0f, nearby);

		const float minDistanceSq = minDistance * minDistance;
		for (auto* vehicle : nearby)
		{
			if (vehicle == nullptr) continue;
			auto* entity = vehicle->GetEntity();
			if (entity == nullptr || entity->IsPendingDeletion()) continue;
			if ((entity->GetWorldTM().Translation() - spawnPoint).LengthSquared() < minDistanceSq)
				return false;
		}
		return true;
	}
}

TrafficManagerComponent::TrafficManagerComponent(HexEngine::Entity* entity) :
	UpdateComponent(entity)
{
}

TrafficManagerComponent::TrafficManagerComponent(HexEngine::Entity* entity, TrafficManagerComponent* copy) :
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

void TrafficManagerComponent::Serialize(json& data, HexEngine::JsonFile* file)
{
	file->Serialize(data, "_enabled", _enabled);
	file->Serialize(data, "_globalMaxActiveVehicles", _globalMaxActiveVehicles);
	file->Serialize(data, "_globalActivationDistance", _globalActivationDistance);
	file->Serialize(data, "_drawDebug", _drawDebug);
}

void TrafficManagerComponent::Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask)
{
	file->Deserialize(data, "_enabled", _enabled);
	file->Deserialize(data, "_globalMaxActiveVehicles", _globalMaxActiveVehicles);
	file->Deserialize(data, "_globalActivationDistance", _globalActivationDistance);
	file->Deserialize(data, "_drawDebug", _drawDebug);
}

bool TrafficManagerComponent::CreateWidget(HexEngine::ComponentWidget* widget)
{
	auto* enabled = new HexEngine::Checkbox(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Enabled", &_enabled);
	auto* drawDebug = new HexEngine::Checkbox(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Draw Debug", &_drawDebug);
	auto* globalMaxVehicles = new HexEngine::DragInt(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Global Max Vehicles", &_globalMaxActiveVehicles, 1, 100000, 1);
	auto* globalActivationDistance = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Global Activation Dist", &_globalActivationDistance, 1.0f, 100000.0f, 1.0f, 1);

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
	if (lanePoints.empty())
		return 0;

	if (!IsSpawnPointClear(lanePoints.front(), spawner->GetSpawnClearanceDistance()))
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

	const int32_t randomIndex = HexEngine::GetRandomInt(0, static_cast<int32_t>(validPrefabPaths.size()) - 1);
	const fs::path prefabPath = validPrefabPaths[static_cast<size_t>(randomIndex)];

	auto spawnedEntities = HexEngine::g_pEnv->_prefabLoader->LoadPrefab(HexEngine::g_pEnv->_sceneManager->GetCurrentScene(), prefabPath);
	if (spawnedEntities.empty())
		return 0;

	// Spawned traffic is TRANSIENT - the manager re-creates it every run. Mark
	// the whole prefab instance (root + children) DoNotSave so it never gets
	// baked into a saved scene. Without this, saving while the sim is running
	// persisted the live vehicles; on reload they deserialized as orphan
	// "ghost" entities not owned by any spawner, with stale/unresolved lane
	// references - they sat wherever the save put them (under the map) and
	// never moved because their lane never resolved. Flagging them out of the
	// save removes the entire failure mode; the spawner makes fresh, working
	// vehicles when the scene next runs.
	for (auto* spawned : spawnedEntities)
	{
		if (spawned != nullptr)
			spawned->SetFlag(HexEngine::EntityFlags::DoNotSave);
	}

	std::vector<HexEngine::Entity*> rootEntities;
	rootEntities.reserve(spawnedEntities.size());
	for (auto* spawned : spawnedEntities)
	{
		if (spawned == nullptr || spawned->IsPendingDeletion())
			continue;

		if (spawned->GetParent() == nullptr)
			rootEntities.push_back(spawned);

		if (auto mesh = spawned->GetComponent<HexEngine::StaticMeshComponent>(); mesh != nullptr)
		{
			mesh->SetExcludeFromGI(true);
		}

		for (auto& child : spawned->GetChildren())
		{
			if (auto mesh = child->GetComponent<HexEngine::StaticMeshComponent>(); mesh != nullptr)
			{
				mesh->SetExcludeFromGI(true);
			}
		}
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

		// Traffic graph is lane-driven now; do not run authored waypoint A->B route mode for spawned traffic.
		vehicle->SetUseWaypointRoute(false);
		vehicle->SetLaneEntityName(spawner->GetLaneEntityName());
		vehicle->RestartPath();

		spawner->ActiveVehicles().push_back(root);
		++spawnedVehicleCount;
	}

	return spawnedVehicleCount;
}

void TrafficManagerComponent::UpdateSpawner(TrafficSpawnerComponent* spawner, HexEngine::Camera* camera, float frameTime, int32_t& activeGlobalCount)
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
		// Spawner out of range - just stop SPAWNING new vehicles. We used
		// to also bulk-despawn every vehicle this spawner had created
		// (gated on ShouldDespawnWhenInactive), which made the world
		// feel jarringly empty whenever the player drove away and then
		// turned back. Existing vehicles now keep driving wherever they
		// were going; only new spawns are suppressed.
		return;
	}

	float& timer = spawner->SpawnTimerRef();
	timer += frameTime;

	const float interval = std::max(spawner->GetSpawnIntervalSeconds(), 0.01f);
	if (timer < interval)
		return;

	// Catch-up loop: drain the accumulated time at `interval`-sized steps
	// so a lag spike doesn't lose spawns (old `timer = 0` discarded any
	// leftover). Hard cap on spawns per frame avoids burst storms after
	// huge spikes; the spawner cap and global cap also act as throttles.
	constexpr int32_t kMaxCatchUpSpawnsPerFrame = 3;
	int32_t spawnedThisFrame = 0;
	while (timer >= interval && spawnedThisFrame < kMaxCatchUpSpawnsPerFrame)
	{
		if (static_cast<int32_t>(spawner->ActiveVehicles().size()) >= spawner->GetMaxActiveVehicles())
			break;
		if (activeGlobalCount >= _globalMaxActiveVehicles)
			break;

		timer -= interval;
		const int32_t spawnedVehicles = SpawnVehicleFromSpawner(spawner);
		if (spawnedVehicles > 0)
		{
			activeGlobalCount += spawnedVehicles;
			++spawnedThisFrame;
		}
		else
		{
			// Spawn failed (likely no clearance at front of lane). Don't
			// loop and keep retrying every frame at full burst - reset
			// the timer to wait one more interval before trying again.
			timer = 0.0f;
			break;
		}
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

	// Rebuild the spatial grid ONCE per frame, before any vehicle ticks
	// and before spawn-clearance checks. Drops per-vehicle avoidance
	// from O(N) full-list scan to O(local-density) cell query, AND
	// avoids scene->GetComponents<TrafficVehicleComponent>() per spawn
	// attempt. Cheap (a single pass over the existing s_allVehicles).
	TrafficVehicleComponent::RebuildSpatialGrid();

	std::vector<TrafficSpawnerComponent*> spawners;
	scene->GetComponents<TrafficSpawnerComponent>(spawners);

	// Global active count via the cheap static registry, not a full
	// ECS scan. s_allVehicles is maintained by TrafficVehicleComponent's
	// ctor/dtor so it's always current.
	int32_t activeGlobalCount = static_cast<int32_t>(TrafficVehicleComponent::GetAliveVehicleCount());
	for (auto* spawner : spawners)
	{
		if (spawner == nullptr || spawner->GetEntity() == nullptr || spawner->GetEntity()->IsPendingDeletion())
			continue;

		UpdateSpawner(spawner, camera, frameTime, activeGlobalCount);
	}
}
