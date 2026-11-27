#include "TrafficManagerComponent.hpp"
#include <cfloat>
#include "TrafficLaneComponent.hpp"
#include "TrafficSpawnerComponent.hpp"
#include "TrafficVehicleComponent.hpp"
#include "CityAssetPins.hpp"
#include <HexEngine.Core/FileSystem/PrefabLoader.hpp>
#include <HexEngine.Core/Entity/Component/DayNightCycleComponent.hpp>
#include <HexEngine.Core/Entity/Component/DrivableComponent.hpp>
#include <HexEngine.Core/Environment/TransitPlayerState.hpp>
#include <unordered_map>
#include <HexEngine.Core/Entity/Component/SpotLight.hpp>
#include <HexEngine.Core/Entity/Component/ParticleSystemComponent.hpp>
#include <HexEngine.Core/Scene/Scene.hpp>
#include <HexEngine.Core/Environment/TimeManager.hpp>
#include <HexEngine.Core/Entity/Component/Transform.hpp>
#include <HexEngine.Core/Input/HVar.hpp>
#include <algorithm>
#include <cmath>

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

	// Keep the prefab resident between spawns (see CityAssetPins).
	CityAssetPins::Prefab(prefabPath);

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
		{
			spawned->SetFlag(HexEngine::EntityFlags::DoNotSave);
			// Transient movers are not GI geometry (spawning one used to rebuild every
			// GI clipmap's triangle list); flagged in the spawn frame, before the
			// scene resolves the add (Scene::ProcessPendingGiMeshes).
			for (auto* smc : spawned->GetComponents<HexEngine::StaticMeshComponent>())
				if (smc != nullptr)
					smc->SetExcludeFromVoxelGI(true);
		}
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
			mesh->SetExcludeFromVoxelGI(true);
		}

		for (auto& child : spawned->GetChildren())
		{
			if (auto mesh = child->GetComponent<HexEngine::StaticMeshComponent>(); mesh != nullptr)
			{
				mesh->SetExcludeFromVoxelGI(true);
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
	// Spawn only between the min distance (out of the player's sight, so cars
	// don't pop into existence in front of them) and the activation distance.
	const bool active = distanceToSpawner <= std::max(activationDistance, 0.0f) &&
		distanceToSpawner >= spawner->GetMinActivationDistance();

	// Day/night density: blend the spawner's caps by the scene clock. Spawned
	// cars roam the network indefinitely, so when the cap drops (evening) trim
	// the surplus - one car at a time, the farthest from the camera, and only
	// once it is out of sight.
	const int32_t dayCap = spawner->GetMaxActiveVehicles();
	const int32_t nightCap = spawner->GetNightMaxActiveVehicles();
	const int32_t cap = static_cast<int32_t>(std::lround(dayCap + (nightCap - dayCap) * _nightFactor));
	auto& spawned = spawner->ActiveVehicles();
	if (static_cast<int32_t>(spawned.size()) > cap)
	{
		const float hideDistance = std::max(120.0f, spawner->GetMinActivationDistance());
		size_t farthest = spawned.size();
		float farthestDist = hideDistance;
		for (size_t i = 0; i < spawned.size(); ++i)
		{
			auto* v = spawned[i];
			if (v == nullptr || v->IsPendingDeletion())
				continue;
			const float d = (v->GetWorldTM().Translation() - cameraPosition).Length();
			if (d > farthestDist)
			{
				farthestDist = d;
				farthest = i;
			}
		}
		if (farthest < spawned.size())
		{
			spawned[farthest]->DeleteMe();
			spawned.erase(spawned.begin() + farthest);
			--activeGlobalCount;
		}
	}

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
		if (static_cast<int32_t>(spawner->ActiveVehicles().size()) >= cap)
			break;
		if (activeGlobalCount >= _globalMaxActiveVehicles)
			break;

		timer -= interval;
		const int32_t spawnedVehicles = SpawnVehicleFromSpawner(spawner);
		if (spawnedVehicles > 0)
		{
			activeGlobalCount += spawnedVehicles;
			++spawnedThisFrame;
			// One spawn per spawner per frame: the clearance test reads the
			// spatial grid, which only sees cars from the start of the frame,
			// so a catch-up burst stacked several cars on the same lane node
			// and they wedged each other there forever.
			timer = std::min(timer, interval);
			break;
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

	auto* scene = GetEntity()->GetScene();
	if (scene == nullptr)
		return;

	// Traffic is a play-time simulation. In edit mode it spawned and drove cars
	// (constant shadow/GI churn) and anything left over from a play session
	// kept driving in the editor after stop: clear the spawned cars instead.
	const bool running = HexEngine::g_pEnv != nullptr && HexEngine::g_pEnv->IsGameRunning();
	if (!running)
	{
		if (_wasGameRunning)
		{
			std::vector<TrafficSpawnerComponent*> spawners;
			scene->GetComponents<TrafficSpawnerComponent>(spawners);
			for (auto* spawner : spawners)
			{
				if (spawner != nullptr)
				{
					DespawnSpawnerVehicles(spawner);
					spawner->SpawnTimerRef() = 0.0f;
				}
			}
			CityAssetPins::Clear();   // traffic is play-only; don't pin assets in the editor
		}
		_wasGameRunning = false;
		return;
	}
	_wasGameRunning = true;

	if (!_enabled)
		return;

	auto* camera = scene->GetMainCamera();
	if (camera == nullptr || camera->GetEntity() == nullptr)
		return;

	// Night factor from the scene clock (dusk 17.5-18.5 h, dawn 5.5-6.5 h -
	// the same ramps as the ambient soundscape).
	{
		std::vector<HexEngine::DayNightCycleComponent*> clocks;
		scene->GetComponents<HexEngine::DayNightCycleComponent>(clocks);
		const float h = (!clocks.empty() && clocks[0] != nullptr) ? clocks[0]->GetCurrentTimeHours() : 12.0f;
		if (h >= 6.5f && h < 17.5f) _nightFactor = 0.0f;
		else if (h >= 17.5f && h < 18.5f) _nightFactor = h - 17.5f;
		else if (h >= 5.5f && h < 6.5f) _nightFactor = 1.0f - (h - 5.5f);
		else _nightFactor = 1.0f;
	}

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

	UpdateNearMisses(scene);
	UpdateHeadlights(scene, camera);
}

HexEngine::HVar cs_trafficHeadlights("cs_trafficHeadlights", "Night headlights for the nearest traffic vehicles (two lamps each; 0 = off)", 12, 0, 32);

const TrafficManagerComponent::LampPair& TrafficManagerComponent::LampsFor(HexEngine::Entity* vehicle)
{
	auto it = _lampCache.find(vehicle->GetName());
	if (it != _lampCache.end())
		return it->second;
	if (_lampCache.size() > 512)
		_lampCache.clear();

	// The body's extent in the vehicle's own space: every mesh part's world box
	// corners taken back through the root's inverse (handles nested parts, part
	// scales and the mirrored bus alike). Vehicles drive along local +Z.
	const math::Matrix toLocal = vehicle->GetWorldTM().Invert();
	math::Vector3 mn(FLT_MAX, FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	bool any = false;
	std::vector<HexEngine::Entity*> stack{ vehicle };
	while (!stack.empty())
	{
		HexEngine::Entity* e = stack.back();
		stack.pop_back();
		if (e == nullptr)
			continue;
		if (e->GetComponent<HexEngine::StaticMeshComponent>() != nullptr)
		{
			const dx::BoundingBox& wb = e->GetWorldAABB();
			math::Vector3 corners[8];
			wb.GetCorners(corners);
			for (const auto& c : corners)
			{
				const math::Vector3 l = math::Vector3::Transform(c, toLocal);
				mn = math::Vector3::Min(mn, l);
				mx = math::Vector3::Max(mx, l);
			}
			any = true;
		}
		for (auto* c : e->GetChildren())
			stack.push_back(c);
	}

	LampPair lamps;
	if (!any || !(mx.z - mn.z > 0.5f))
	{
		lamps.left = math::Vector3(-0.6f, 0.7f, 2.2f);   // typical car if it can't be measured
		lamps.right = math::Vector3(0.6f, 0.7f, 2.2f);
	}
	else
	{
		const float height = mx.y - mn.y;
		const float y = mn.y + std::clamp(height * 0.33f, 0.5f, 0.85f);
		const float cx = (mn.x + mx.x) * 0.5f;
		const float halfW = (mx.x - mn.x) * 0.5f;
		const float z = mx.z - 0.05f;   // just ahead of the nose's front face
		lamps.left = math::Vector3(cx - halfW * 0.7f, y, z);
		lamps.right = math::Vector3(cx + halfW * 0.7f, y, z);
	}
	return _lampCache.emplace(vehicle->GetName(), lamps).first->second;
}

void TrafficManagerComponent::UpdateHeadlights(HexEngine::Scene* scene, HexEngine::Camera* camera)
{
	const int32_t wantCars = cs_trafficHeadlights._val.i32;
	const int32_t want = wantCars * 2;   // a lamp per headlight
	// Resolve the pool by name every frame; drop any that no longer exist (play
	// stop / scene reload destroyed them) so they are recreated below.
	std::vector<HexEngine::Entity*> pool;
	pool.reserve(_headlights.size());
	for (auto it = _headlights.begin(); it != _headlights.end();)
	{
		HexEngine::Entity* e = scene->GetEntityByName(*it);
		if (e == nullptr || e->IsPendingDeletion())
		{
			it = _headlights.erase(it);
			continue;
		}
		pool.push_back(e);
		++it;
	}
	const auto lampOff = [](HexEngine::Entity* e)
	{
		if (auto* l = e->GetComponent<HexEngine::SpotLight>())
			l->SetDiffuseColour(math::Color(1.0f, 0.93f, 0.8f, 0.0f));
	};
	if (want <= 0 || _nightFactor <= 0.01f)
	{
		for (auto* e : pool)
			lampOff(e);
		return;
	}
	while ((int32_t)pool.size() < want)
	{
		auto* e = scene->CreateEntity("TrafficHeadlight_" + std::to_string(pool.size()));
		if (e == nullptr)
			break;
		e->SetFlag(HexEngine::EntityFlags::DoNotSave);
		auto* l = e->AddComponent<HexEngine::SpotLight>();
		if (l != nullptr)
		{
			l->SetDoesCastShadows(false);
			l->SetInjectIntoGI(false);
			l->SetIsVolumetric(false);
			l->SetRadius(24.0f);
			l->SetInnerConeAngle(24.0f);
			l->SetOuterConeAngle(48.0f);
			l->SetLightStength(70.0f);   // per lamp; two per vehicle
		}
		_headlights.push_back(e->GetName());
		pool.push_back(e);
	}

	// Nearest moving vehicles to the camera.
	const math::Vector3 camPos = camera->GetEntity()->GetWorldTM().Translation();
	std::vector<TrafficVehicleComponent*> cars;
	TrafficVehicleComponent::QueryNearbyVehicles(camPos, 90.0f, cars);
	cars.erase(std::remove_if(cars.begin(), cars.end(), [](TrafficVehicleComponent* v)
		{ return v == nullptr || v->IsParked() || v->GetEntity() == nullptr || v->GetEntity()->IsPendingDeletion(); }), cars.end());
	std::sort(cars.begin(), cars.end(), [&](TrafficVehicleComponent* a, TrafficVehicleComponent* b)
		{
			return (a->GetEntity()->GetWorldTM().Translation() - camPos).LengthSquared() <
				(b->GetEntity()->GetWorldTM().Translation() - camPos).LengthSquared();
		});

	for (size_t i = 0; i < pool.size(); ++i)
	{
		auto* e = pool[i];
		auto* l = e->GetComponent<HexEngine::SpotLight>();
		const size_t car = i / 2;
		if (l == nullptr)
			continue;
		if (car >= cars.size())
		{
			lampOff(e);
			continue;
		}
		HexEngine::Entity* vehicle = cars[car]->GetEntity();
		const math::Matrix tm = vehicle->GetWorldTM();
		const LampPair& lamps = LampsFor(vehicle);
		// Cars drive along local +Z; a spot shines along its world -Z, so turn it
		// round and dip it ~7 degrees at the road.
		math::Vector3 fwd = math::Vector3::TransformNormal(math::Vector3::Backward, tm);
		fwd.y = 0.0f;
		if (fwd.LengthSquared() < 1e-6f)
			continue;
		fwd.Normalize();
		const float yaw = std::atan2(fwd.x, fwd.z) + 3.14159265f;
		const math::Vector3 pos = math::Vector3::Transform((i % 2) == 0 ? lamps.left : lamps.right, tm);
		if (auto* tf = e->GetComponent<HexEngine::Transform>())
		{
			tf->SetPosition(pos);
			tf->SetRotation(math::Quaternion::CreateFromYawPitchRoll(yaw, -0.12f, 0.0f));
		}
		l->SetDiffuseColour(math::Color(1.0f, 0.93f, 0.8f, std::clamp(_nightFactor, 0.0f, 1.0f)));
	}
}

// Near-miss detection (Drop 05): track each car close to the player's ridden
// vehicle; when it pulls away again after a pass that came within
// kNearGap of its body without touching, at a real closing speed, count one.
namespace
{
	// Drop 07: a headlight on whatever the player rides, at night. One pooled
	// SpotLight found by name (DoNotSave), like the traffic headlights - and
	// tuned the same (no shadows, GI or volumetrics).
	void UpdatePlayerHeadlight(HexEngine::Scene* scene, bool riding, const math::Vector3& pos, const math::Vector3& fwd, float night, float noseAhead)
	{
		static const char* kName = "PlayerHeadlight";
		HexEngine::Entity* e = scene->GetEntityByName(kName);
		if (e != nullptr && e->IsPendingDeletion())
			e = nullptr;
		const bool on = riding && night > 0.01f && fwd.LengthSquared() > 1e-6f;
		if (e == nullptr)
		{
			if (!on)
				return;
			e = scene->CreateEntity(kName);
			if (e == nullptr)
				return;
			e->SetFlag(HexEngine::EntityFlags::DoNotSave);
			if (auto* l = e->AddComponent<HexEngine::SpotLight>())
			{
				l->SetDoesCastShadows(false);
				l->SetInjectIntoGI(false);
				l->SetIsVolumetric(false);
				// Tuned in play 2026-11-27: 90 lm / 44 deg blew out a white pool right in
				// front of the rider (the lamp is a metre from the camera, the car lamps
				// never are).
				l->SetRadius(28.0f);
				l->SetInnerConeAngle(14.0f);
				l->SetOuterConeAngle(32.0f);
				l->SetLightStength(28.0f);
			}
		}
		auto* l = e->GetComponent<HexEngine::SpotLight>();
		if (l == nullptr)
			return;
		if (!on)
		{
			l->SetDiffuseColour(math::Color(1.0f, 0.96f, 0.88f, 0.0f));
			return;
		}
		// A spot shines along its local -Z: turn it to the travel direction and
		// dip it ~8 degrees at the road (as the car lamps).
		const float yaw = std::atan2(fwd.x, fwd.z) + 3.14159265f;
		if (auto* tf = e->GetComponent<HexEngine::Transform>())
		{
			// At the vehicle's nose (half the wheelbase + a bit), not inside a van cab.
			tf->SetPosition(pos + fwd * noseAhead + math::Vector3(0.0f, 0.95f, 0.0f));
			tf->SetRotation(math::Quaternion::CreateFromYawPitchRoll(yaw, -0.09f, 0.0f));
		}
		l->SetDiffuseColour(math::Color(1.0f, 0.96f, 0.88f, std::clamp(night, 0.0f, 1.0f)));
	}
}

namespace
{
	// Drop 08: spray kicked up behind the ridden vehicle on a wet road. One
	// pooled ParticleSystemComponent (DoNotSave) parked at the rear wheel,
	// fed particles by Trigger() in proportion to speed x wetness (the effect's
	// own rate is 0).
	void UpdatePlayerSpray(HexEngine::Scene* scene, bool riding, const math::Vector3& pos, const math::Vector3& fwd,
		float speed, float frameTime)
	{
		static const char* kName = "PlayerWheelSpray";
		static float s_carry = 0.0f;
		const float wet = scene->GetWeatherSurfaceParams().wetness;
		const float amount = riding ? std::clamp((wet - 0.25f) / 0.5f, 0.0f, 1.0f) * std::clamp((std::fabs(speed) - 3.0f) / 9.0f, 0.0f, 1.0f) : 0.0f;
		HexEngine::Entity* e = scene->GetEntityByName(kName);
		if (e != nullptr && e->IsPendingDeletion())
			e = nullptr;
		if (e == nullptr)
		{
			if (amount <= 0.0f)
				return;
			e = scene->CreateEntity(kName);
			if (e == nullptr)
				return;
			e->SetFlag(HexEngine::EntityFlags::DoNotSave);
			if (auto* ps = e->AddComponent<HexEngine::ParticleSystemComponent>())
			{
				ps->SetEffectPath("GameData.Particles/WheelSpray.hparticles");
				ps->SetReceiveLighting(true);
			}
		}
		auto* ps = e->GetComponent<HexEngine::ParticleSystemComponent>();
		if (ps == nullptr || amount <= 0.0f || frameTime <= 0.0f)
		{
			s_carry = 0.0f;
			return;
		}
		if (auto* tf = e->GetComponent<HexEngine::Transform>())
			tf->SetPosition(pos - fwd * 0.75f + math::Vector3(0.0f, 0.12f, 0.0f));
		// Up to ~140 puffs a second at full speed in a downpour.
		s_carry += 140.0f * amount * std::min(frameTime, 0.1f);
		const uint32_t n = (uint32_t)s_carry;
		s_carry -= (float)n;
		if (n > 0)
			ps->Trigger(n);
	}
}

void TrafficManagerComponent::UpdateNearMisses(HexEngine::Scene* scene)
{
	auto& st = HexEngine::GetTrafficPlayerState();
	HexEngine::DrivableComponent* ride = nullptr;
	{
		std::vector<HexEngine::DrivableComponent*> drivables;
		scene->GetComponents<HexEngine::DrivableComponent>(drivables);
		for (auto* d : drivables)
			if (d != nullptr && d->IsPlayerControlled() && d->GetEntity() != nullptr) { ride = d; break; }
	}
	if (ride == nullptr)
	{
		_nearTracks.clear();
		st.nearbyVehicles = 0;
		TrafficVehicleComponent::SetPlayerObstacle(false, math::Vector3::Zero, math::Vector3::Zero);
		UpdatePlayerHeadlight(scene, false, math::Vector3::Zero, math::Vector3::Zero, _nightFactor, 0.0f);
		UpdatePlayerSpray(scene, false, math::Vector3::Zero, math::Vector3::Zero, 0.0f, 0.0f);
		return;
	}

	constexpr float kTrackRadius = 12.0f;   // start watching a car inside this (centre distance)
	constexpr float kNearGap = 1.1f;        // body gap that counts as "near" (m)
	constexpr float kMinClosing = 4.0f;     // closing speed below this is just traffic (m/s)
	constexpr float kHalfWidth = 0.95f, kHalfLength = 2.3f;   // typical car body (local X / Z)

	const math::Matrix bikeTM = ride->GetEntity()->GetWorldTM();
	const math::Vector3 bikePos = bikeTM.Translation();
	math::Vector3 bikeFwd = math::Vector3::TransformNormal(math::Vector3::Forward, bikeTM) * ride->GetTuning().forwardSign;
	bikeFwd.y = 0.0f;
	if (bikeFwd.LengthSquared() > 1e-6f) bikeFwd.Normalize();
	const math::Vector3 bikeVel = bikeFwd * ride->GetForwardSpeed();
	const bool crashed = ride->CrashRemountSecondsLeft() > 0.0f;
	// Cars brake for the ridden vehicle in their lane (TrafficVehicleComponent).
	TrafficVehicleComponent::SetPlayerObstacle(true, bikePos, bikeVel);
	UpdatePlayerHeadlight(scene, true, bikePos, bikeFwd, _nightFactor, ride->GetTuning().wheelbase * 0.5f + 0.5f);
	UpdatePlayerSpray(scene, !crashed, bikePos, bikeFwd, ride->GetForwardSpeed(),
		HexEngine::g_pEnv->_timeManager != nullptr ? HexEngine::g_pEnv->_timeManager->GetGameFrameTime() : 0.0f);

	std::vector<TrafficVehicleComponent*> nearby;
	TrafficVehicleComponent::QueryNearbyVehicles(bikePos, kTrackRadius, nearby);
	st.nearbyVehicles = (uint32_t)nearby.size();

	std::unordered_map<TrafficVehicleComponent*, NearTrack> next;
	for (auto* v : nearby)
	{
		if (v == nullptr || v->GetEntity() == nullptr || v->GetEntity()->IsPendingDeletion())
			continue;
		const math::Matrix tm = v->GetEntity()->GetWorldTM();
		math::Vector3 ax = math::Vector3::TransformNormal(math::Vector3::Right, tm); ax.y = 0.0f; ax.Normalize();
		math::Vector3 az = math::Vector3::TransformNormal(math::Vector3::Backward, tm); az.y = 0.0f; az.Normalize();
		const math::Vector3 d = bikePos - tm.Translation();
		const float lx = std::max(0.0f, std::fabs(d.Dot(ax)) - kHalfWidth);
		const float lz = std::max(0.0f, std::fabs(d.Dot(az)) - kHalfLength);
		const float gap = std::sqrt(lx * lx + lz * lz);
		if (std::fabs(d.y) > 3.0f)
			continue;   // a bridge / underpass, not a pass

		// Car velocity along whichever local axis is its heading: its speed with the
		// sign of the motion (cars drive along their length axis).
		const math::Vector3 carVel = az * v->GetCurrentSpeed();
		const float closing = std::min((bikeVel - carVel).Length(), (bikeVel + carVel).Length());

		NearTrack tr;
		auto it = _nearTracks.find(v);
		if (it != _nearTracks.end())
			tr = it->second;
		if (gap < tr.minGap)
		{
			tr.minGap = gap;
			tr.closingAtMin = closing;
		}
		if (crashed)
			tr.spoiled = true;

		// Passed: was near, now clearly pulling away.
		if (!tr.spoiled && !tr.counted && tr.minGap < kNearGap && gap > tr.minGap + 1.5f)
		{
			if (tr.minGap > 0.02f && tr.closingAtMin >= kMinClosing)
			{
				++st.nearMisses;
				st.lastGap = tr.minGap;
				st.lastRelativeSpeed = tr.closingAtMin;
				ride->AddCameraShake(0.25f);
				LOG_INFO("traffic: NEAR MISS gap %.2f m at %.1f m/s", tr.minGap, tr.closingAtMin);
			}
			tr.counted = true;
		}
		next[v] = tr;
	}
	_nearTracks.swap(next);
}
