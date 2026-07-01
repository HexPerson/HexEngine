#include "TrafficVehicleComponent.hpp"
#include "TrafficLaneComponent.hpp"
#include <HexEngine.Core/Audio/AudioManager.hpp>
#include <HexEngine.Core/GUI/Elements/ArrayElement.hpp>
#include <HexEngine.Core/GUI/Elements/AssetSearch.hpp>
#include <unordered_map>

std::vector<TrafficVehicleComponent*> TrafficVehicleComponent::s_allVehicles;

namespace
{
	// Spatial grid for vehicle-vs-vehicle proximity queries. 32m cells:
	// large enough that any look-ahead radius (typically 10-15m) only
	// needs to query the home cell plus at most one ring of neighbours
	// (9 cells), but small enough that each cell holds only a handful
	// of vehicles in typical city density. Built once per frame by
	// TrafficVehicleComponent::RebuildSpatialGrid(), queried per
	// vehicle. Drops avoidance from O(N^2) to roughly O(N * vehicles-
	// per-cell), ~10x faster at typical traffic loads.
	constexpr float kSpatialCellSize = 32.0f;

	inline uint64_t MakeCellKey(int32_t x, int32_t z)
	{
		return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) | static_cast<uint32_t>(z);
	}

	inline void WorldToCell(const math::Vector3& p, int32_t& outX, int32_t& outZ)
	{
		outX = static_cast<int32_t>(std::floor(p.x / kSpatialCellSize));
		outZ = static_cast<int32_t>(std::floor(p.z / kSpatialCellSize));
	}

	std::unordered_map<uint64_t, std::vector<TrafficVehicleComponent*>>& GetSpatialGrid()
	{
		static std::unordered_map<uint64_t, std::vector<TrafficVehicleComponent*>> grid;
		return grid;
	}

	struct WaypointGraphNode
	{
		math::Vector3 position = math::Vector3::Zero;
		std::vector<std::string> neighbours;
	};

	void DrawPointMarker(const math::Vector3& position, float extent, const math::Color& colour)
	{
		dx::BoundingBox marker;
		marker.Center = position;
		marker.Extents = math::Vector3(extent);
		HexEngine::g_pEnv->_debugRenderer->DrawAABB(marker, colour);
	}

	void AddWaypointEdge(std::unordered_map<std::string, WaypointGraphNode>& graph, const std::string& from, const std::string& to)
	{
		if (from.empty() || to.empty() || from == to)
			return;

		auto fromIt = graph.find(from);
		auto toIt = graph.find(to);
		if (fromIt == graph.end() || toIt == graph.end())
			return;

		auto& neighbours = fromIt->second.neighbours;
		if (std::find(neighbours.begin(), neighbours.end(), to) == neighbours.end())
			neighbours.push_back(to);
	}

	bool TryResolveNextLane(HexEngine::Scene* scene, TrafficLaneComponent* currentLane, std::string& outNextLaneName, TrafficLaneComponent*& outNextLane)
	{
		outNextLaneName.clear();
		outNextLane = nullptr;

		if (scene == nullptr || currentLane == nullptr)
			return false;

		const auto& configuredNext = currentLane->GetNextLaneEntityNames();
		if (configuredNext.empty())
			return false;

		// Keep round-robin behaviour, but skip invalid/deleted targets instead of immediately failing.
		for (size_t attempt = 0; attempt < configuredNext.size(); ++attempt)
		{
			const std::string candidateName = currentLane->GetNextLaneEntityName();
			if (candidateName.empty())
				continue;

			auto* nextLaneEntity = scene->GetEntityByName(candidateName);
			if (nextLaneEntity == nullptr || nextLaneEntity->IsPendingDeletion())
				continue;

			auto* nextLane = nextLaneEntity->GetComponent<TrafficLaneComponent>();
			if (nextLane == nullptr)
				continue;

			outNextLaneName = candidateName;
			outNextLane = nextLane;
			return true;
		}

		return false;
	}
}

TrafficVehicleComponent::TrafficVehicleComponent(HexEngine::Entity* entity) :
	UpdateComponent(entity)
{
	s_allVehicles.push_back(this);
	// Per-vehicle audio personality, so a city of cars doesn't honk in
	// unison and engines have slight pitch variation.
	_honkPersonality = HexEngine::GetRandomFloat(0.7f, 1.4f);
	_enginePitchOffset = HexEngine::GetRandomFloat(-0.07f, 0.07f);
}

TrafficVehicleComponent::TrafficVehicleComponent(HexEngine::Entity* entity, TrafficVehicleComponent* copy) :
	UpdateComponent(entity, copy)
{
	if (copy != nullptr)
	{
		_laneEntityName = copy->_laneEntityName;
		_targetIndex = copy->_targetIndex;
		_speed = copy->_speed;
		_acceleration = copy->_acceleration;
		_rotationLerp = copy->_rotationLerp;
		_arrivalDistance = copy->_arrivalDistance;
		_currentSpeed = 0.0f;
		_useLaneSpeedLimit = copy->_useLaneSpeedLimit;
		_invertDirection = copy->_invertDirection;
		_despawnAtRouteEnd = copy->_despawnAtRouteEnd;
		_useWaypointRoute = copy->_useWaypointRoute;
		_startWaypointEntityName = copy->_startWaypointEntityName;
		_destinationWaypointEntityName = copy->_destinationWaypointEntityName;
		_drawDebug = copy->_drawDebug;
		_avoidanceEnabled = copy->_avoidanceEnabled;
		_avoidanceLookAheadDistance = copy->_avoidanceLookAheadDistance;
		_avoidanceFollowDistance = copy->_avoidanceFollowDistance;
		_brakingStrength = copy->_brakingStrength;
		// Audio config carries over to clones, but the runtime sound
		// handle does NOT - each instance creates its own playback.
		_engineSoundPath = copy->_engineSoundPath;
		_honkSoundPaths = copy->_honkSoundPaths;
		_audioCullDistance = copy->_audioCullDistance;
		_engineSoundRadius = copy->_engineSoundRadius;
		_honkSoundRadius = copy->_honkSoundRadius;
		_engineIdlePitch = copy->_engineIdlePitch;
		_engineMaxPitch = copy->_engineMaxPitch;
		_engineIdleVolume = copy->_engineIdleVolume;
		_engineMaxVolume = copy->_engineMaxVolume;
		_engineNumGears = copy->_engineNumGears;
		_engineShiftDamping = copy->_engineShiftDamping;
		_honkBlockedThreshold = copy->_honkBlockedThreshold;
		_honkCooldownMin = copy->_honkCooldownMin;
		_honkCooldownMax = copy->_honkCooldownMax;
	}

	s_allVehicles.push_back(this);
	// Fresh personality even on clones - traffic should still vary.
	_honkPersonality = HexEngine::GetRandomFloat(0.7f, 1.4f);
	_enginePitchOffset = HexEngine::GetRandomFloat(-0.07f, 0.07f);
}

TrafficVehicleComponent::~TrafficVehicleComponent()
{
	// Must stop and release the looping engine sound before the
	// component dies - the AudioManager keeps a weak ref but the
	// underlying SoundEffectInstance will be reused next allocation.
	StopEngineSound();
	s_allVehicles.erase(std::remove(s_allVehicles.begin(), s_allVehicles.end(), this), s_allVehicles.end());

	// Also purge ourselves from the spatial grid. It's only rebuilt once per
	// TrafficManager update, so between our deletion and the next rebuild it would
	// otherwise keep a dangling pointer that QueryNearbyVehicles hands to another
	// vehicle's ComputeAvoidanceSpeed (use-after-free). Scan all cells: we may have
	// moved since the grid was last built, so our pointer can be in any cell.
	auto& grid = GetSpatialGrid();
	for (auto& kv : grid)
		kv.second.erase(std::remove(kv.second.begin(), kv.second.end(), this), kv.second.end());
}

void TrafficVehicleComponent::RebuildSpatialGrid()
{
	auto& grid = GetSpatialGrid();
	// Clear values but keep map capacity - cells get reused across frames
	// since vehicles cluster in similar regions.
	for (auto& kv : grid)
		kv.second.clear();

	for (auto* v : s_allVehicles)
	{
		if (v == nullptr) continue;
		auto* e = v->GetEntity();
		if (e == nullptr || e->IsPendingDeletion()) continue;
		const math::Vector3 pos = e->GetWorldTM().Translation();
		int32_t cx, cz;
		WorldToCell(pos, cx, cz);
		grid[MakeCellKey(cx, cz)].push_back(v);
	}
}

void TrafficVehicleComponent::QueryNearbyVehicles(const math::Vector3& center, float radius, std::vector<TrafficVehicleComponent*>& out)
{
	out.clear();
	auto& grid = GetSpatialGrid();
	int32_t cx, cz;
	WorldToCell(center, cx, cz);
	const int32_t cellRadius = std::max(1, static_cast<int32_t>(std::ceil(radius / kSpatialCellSize)));
	for (int32_t dz = -cellRadius; dz <= cellRadius; ++dz)
	{
		for (int32_t dx = -cellRadius; dx <= cellRadius; ++dx)
		{
			auto it = grid.find(MakeCellKey(cx + dx, cz + dz));
			if (it != grid.end())
				out.insert(out.end(), it->second.begin(), it->second.end());
		}
	}
}

size_t TrafficVehicleComponent::GetAliveVehicleCount()
{
	// s_allVehicles holds raw pointers - some may be queued for deletion.
	// Cheap enough to filter here vs. scanning Scene::GetComponents.
	size_t alive = 0;
	for (auto* v : s_allVehicles)
	{
		if (v == nullptr) continue;
		auto* e = v->GetEntity();
		if (e == nullptr || e->IsPendingDeletion()) continue;
		++alive;
	}
	return alive;
}

void TrafficVehicleComponent::StartEngineSoundIfNeeded(const math::Vector3& worldPos)
{
	if (_engineSoundPlaying) return;
	if (_engineSoundPath.empty()) return;

	if (!_engineSound)
	{
		// SoundEffect::Create goes through the ResourceSystem cache, so
		// loading the same path from many vehicles returns the SAME
		// shared SoundEffect (one SoundEffectInstance). If vehicle B
		// then calls Loop on it, it stomps vehicle A's playback. Clone
		// per vehicle to get an independent SoundEffectInstance backed
		// by the shared loaded audio data.
		auto master = HexEngine::SoundEffect::Create(_engineSoundPath);
		if (!master)
			return;
		_engineSound = master->CreatePlaybackClone();
		if (!_engineSound)
			return;
		_engineSound->SetRadius(_engineSoundRadius);
	}

	if (HexEngine::g_pEnv && HexEngine::g_pEnv->_audioManager)
	{
		_engineSound->SetVolume(_engineIdleVolume);
		// Pitch is in DirectX semi-octave units [-1, 1]; SoundEffect::SetPitch
		// already clamps, but pre-clamp here for clarity.
		const float startPitch = std::clamp(_engineIdlePitch + _enginePitchOffset, -1.0f, 1.0f);
		_engineSound->SetPitch(startPitch);
		// Seed the smoothed-pitch state so the first frame of the gear
		// envelope doesn't lerp up from a stale value (e.g. 0).
		_engineSmoothedPitch = startPitch;
		HexEngine::g_pEnv->_audioManager->Loop(_engineSound, worldPos);
		_engineSoundPlaying = true;
	}
}

void TrafficVehicleComponent::StopEngineSound()
{
	if (!_engineSoundPlaying || !_engineSound) return;
	if (HexEngine::g_pEnv && HexEngine::g_pEnv->_audioManager)
		HexEngine::g_pEnv->_audioManager->Stop(_engineSound);
	_engineSoundPlaying = false;
}

void TrafficVehicleComponent::TryHonk(const math::Vector3& worldPos)
{
	if (_honkSoundPaths.empty()) return;
	if (HexEngine::g_pEnv == nullptr || HexEngine::g_pEnv->_audioManager == nullptr) return;

	// Pick a random variant so multiple honks don't sound identical.
	// Filter out empty entries first; if none remain, nothing to play.
	int32_t validCount = 0;
	for (const auto& p : _honkSoundPaths) if (!p.empty()) ++validCount;
	if (validCount == 0) return;

	const int32_t pickIdx = HexEngine::GetRandomInt(0, validCount - 1);
	int32_t skipped = 0;
	std::string chosenPath;
	for (const auto& p : _honkSoundPaths)
	{
		if (p.empty()) continue;
		if (skipped == pickIdx) { chosenPath = p; break; }
		++skipped;
	}
	if (chosenPath.empty()) return;

	// SoundEffect::Create is cached at the ResourceSystem - calling it
	// from many vehicles for the same path returns the SAME shared
	// SoundEffect with ONE underlying SoundEffectInstance. Playing on
	// it would stomp any already-in-flight honk on the same clip
	// (vehicle A's honk cut off mid-way when vehicle B honks). Clone
	// the master to get a fresh instance per honk - same loaded audio
	// data, independent playback state.
	auto master = HexEngine::SoundEffect::Create(chosenPath);
	if (!master) return;
	auto honk = master->CreatePlaybackClone();
	if (!honk) return;
	honk->SetRadius(_honkSoundRadius);
	honk->SetVolume(1.0f);
	// Slight per-honk pitch variation for natural-sounding traffic.
	// In DirectX semi-octave units [-1, 1]; ±0.05 = ~3.5% frequency shift.
	honk->SetPitch(HexEngine::GetRandomFloat(-0.05f, 0.05f));
	HexEngine::g_pEnv->_audioManager->Play(honk, worldPos);
	// AudioManager keeps only a WEAK ref - the clone shared_ptr would
	// die at end of scope and free the SoundEffectInstance before the
	// honk finishes playing. Keep the clone alive in _activeHonks;
	// UpdateAudio prunes entries that are no longer playing.
	_activeHonks.push_back(std::move(honk));
}

void TrafficVehicleComponent::UpdateAudio(const math::Vector3& worldPos, float frameTime)
{
	// Audible-range cull. Saves audio voices (and Apply3D work) on
	// distant traffic the player can't hear anyway.
	float distSqToCamera = std::numeric_limits<float>::max();
	if (HexEngine::g_pEnv && HexEngine::g_pEnv->_sceneManager)
	{
		if (auto scene = HexEngine::g_pEnv->_sceneManager->GetCurrentScene())
		{
			if (auto* cam = scene->GetMainCamera(); cam != nullptr && cam->GetEntity() != nullptr)
			{
				const math::Vector3 camPos = cam->GetEntity()->GetPosition();
				distSqToCamera = (worldPos - camPos).LengthSquared();
			}
		}
	}
	const float cullDist = std::max(_audioCullDistance, 1.0f);
	// Hysteresis: stop slightly beyond start distance to avoid flicker
	// for vehicles parked exactly at the cull edge.
	const float startSq = cullDist * cullDist;
	const float stopSq  = (cullDist * 1.1f) * (cullDist * 1.1f);

	if (_engineSoundPlaying)
	{
		if (distSqToCamera > stopSq)
		{
			StopEngineSound();
		}
	}
	else
	{
		if (distSqToCamera <= startSq)
			StartEngineSoundIfNeeded(worldPos);
	}

	// Engine sound modulation while playing.
	if (_engineSoundPlaying && _engineSound)
	{
		_engineSound->SetPosition(worldPos);

		// Pitch via gear simulation. Divide the speed range into N gear
		// bands; within each band pitch sweeps idle->max linearly. At a
		// gear boundary pitch drops back to idle and starts climbing
		// again - same behaviour as a real gearbox.
		//
		// This fixes the "pitch only changes at start of acceleration"
		// problem: with a single linear ramp the pitch saturates at max
		// the moment the vehicle hits cruise speed, then sits there. With
		// gears, even at constant cruise speed the vehicle's pitch lives
		// somewhere inside the current gear's band - far more dynamic
		// and engine-like.
		const float speedAlpha    = std::clamp(_currentSpeed / _audioReferenceMaxSpeed, 0.0f, 1.0f);
		const int32_t numGears    = std::max(1, _engineNumGears);
		const float gearPos       = speedAlpha * float(numGears);    // continuous gear index, e.g. 2.37
		const float gearLocal     = std::clamp(gearPos - std::floor(gearPos), 0.0f, 1.0f);
		const float targetPitch   = std::lerp(_engineIdlePitch, _engineMaxPitch, gearLocal) + _enginePitchOffset;

		// Smooth the abrupt drop at gear boundaries so the "shift"
		// doesn't crackle. Exponential damping - same frame-rate
		// independent pattern used for vehicle rotation.
		const float shiftBlend    = 1.0f - std::exp(-std::max(_engineShiftDamping, 0.01f) * frameTime);
		_engineSmoothedPitch      = std::lerp(_engineSmoothedPitch, targetPitch, shiftBlend);

		const float vol = std::lerp(_engineIdleVolume, _engineMaxVolume, speedAlpha);
		_engineSound->SetPitch(std::clamp(_engineSmoothedPitch, -1.0f, 1.0f));
		_engineSound->SetVolume(std::clamp(vol, 0.0f, 1.0f));
	}

	// Prune any honk clones that have finished playing. Without this
	// they pile up forever (each TryHonk pushes a new strong ref).
	_activeHonks.erase(
		std::remove_if(_activeHonks.begin(), _activeHonks.end(),
			[](const std::shared_ptr<HexEngine::SoundEffect>& h)
			{
				return !h || !h->IsPlaying();
			}),
		_activeHonks.end());

	// Honk logic. Vehicle counts as "blocked" when its avoidance system
	// has forced its speed close to zero - i.e. it WANTS to go but can't.
	// Once blocked > threshold, fire a honk and reset to a random cooldown.
	// Cooldown ticks down whether blocked or not so we don't accidentally
	// honk again the instant we hit threshold next.
	if (_honkCooldown > 0.0f) _honkCooldown = std::max(0.0f, _honkCooldown - frameTime);

	const bool isMoving = _currentSpeed > 0.5f;
	const bool wantsToMove = _speed > 0.1f;
	if (wantsToMove && !isMoving)
	{
		_blockedTime += frameTime;
		const float effectiveThreshold = std::max(0.5f, _honkBlockedThreshold / std::max(_honkPersonality, 0.1f));
		if (_blockedTime >= effectiveThreshold && _honkCooldown <= 0.0f)
		{
			// Only honk if we're within audible range - keeps far-away
			// vehicles from spawning sound voices the player can't hear.
			if (distSqToCamera <= stopSq)
				TryHonk(worldPos);
			_blockedTime = 0.0f;
			_honkCooldown = HexEngine::GetRandomFloat(
				std::max(0.5f, _honkCooldownMin),
				std::max(_honkCooldownMin + 0.5f, _honkCooldownMax));
		}
	}
	else
	{
		// Reset blocked timer the moment we get moving again so a brief
		// jam doesn't immediately re-trigger when traffic clears.
		_blockedTime = 0.0f;
	}
}

void TrafficVehicleComponent::SetLaneEntityName(const std::string& laneEntityName)
{
	_laneEntityName = laneEntityName;
	RestartPath();
}

void TrafficVehicleComponent::SetWaypointRouteEndpoints(const std::string& startWaypointEntityName, const std::string& destinationWaypointEntityName)
{
	_startWaypointEntityName = startWaypointEntityName;
	_destinationWaypointEntityName = destinationWaypointEntityName;
}

bool TrafficVehicleComponent::ConsumeRouteEndReachedEvent()
{
	const bool reached = _routeEndReachedEvent;
	_routeEndReachedEvent = false;
	return reached;
}

void TrafficVehicleComponent::RestartPath()
{
	_targetIndex = 0;
	_plannedRoutePoints.clear();
	_routeEndReachedEvent = false;

	if (_useWaypointRoute)
	{
		std::vector<math::Vector3> route;
		if (BuildWaypointRoute(route) && route.size() >= 2)
		{
			_plannedRoutePoints = std::move(route);
		}
	}

	if (_invertDirection)
	{
		std::vector<math::Vector3> points;
		if (GatherPlannedRoute(points) && !points.empty())
		{
			_targetIndex = points.size() - 1;
		}
	}

	_currentSpeed = 0.0f;
}

bool TrafficVehicleComponent::GatherPlannedRoute(std::vector<math::Vector3>& outPoints)
{
	outPoints.clear();

	if (_useWaypointRoute && _plannedRoutePoints.size() >= 2)
	{
		outPoints = _plannedRoutePoints;
		return true;
	}

	return GatherLanePoints(outPoints);
}

bool TrafficVehicleComponent::BuildWaypointRoute(std::vector<math::Vector3>& outPoints) const
{
	outPoints.clear();

	auto* entity = GetEntity();
	auto* scene = entity != nullptr ? entity->GetScene() : nullptr;
	if (scene == nullptr)
		return false;

	if (_startWaypointEntityName.empty() || _destinationWaypointEntityName.empty())
		return false;

	auto* startEntity = scene->GetEntityByName(_startWaypointEntityName);
	auto* destinationEntity = scene->GetEntityByName(_destinationWaypointEntityName);
	if (startEntity == nullptr || destinationEntity == nullptr)
		return false;

	std::unordered_map<std::string, WaypointGraphNode> graph;
	std::unordered_map<HexEngine::Entity*, std::vector<HexEngine::Entity*>> laneWaypointCache;

	std::vector<TrafficLaneComponent*> lanes;
	scene->GetComponents<TrafficLaneComponent>(lanes);

	for (auto* lane : lanes)
	{
		if (lane == nullptr || lane->GetEntity() == nullptr || lane->GetEntity()->IsPendingDeletion())
			continue;

		std::vector<HexEngine::Entity*> laneWaypoints;
		lane->GatherLaneWaypointEntities(laneWaypoints);
		laneWaypointCache[lane->GetEntity()] = laneWaypoints;

		for (auto* waypoint : laneWaypoints)
		{
			if (waypoint == nullptr || waypoint->IsPendingDeletion())
				continue;

			auto& node = graph[waypoint->GetName()];
			node.position = waypoint->GetWorldTM().Translation();
		}
	}

	for (auto* lane : lanes)
	{
		if (lane == nullptr || lane->GetEntity() == nullptr || lane->GetEntity()->IsPendingDeletion())
			continue;

		const auto cacheIt = laneWaypointCache.find(lane->GetEntity());
		if (cacheIt == laneWaypointCache.end())
			continue;

		const auto& laneWaypoints = cacheIt->second;
		if (laneWaypoints.empty())
			continue;

		for (size_t i = 0; i + 1 < laneWaypoints.size(); ++i)
		{
			auto* a = laneWaypoints[i];
			auto* b = laneWaypoints[i + 1];
			if (a == nullptr || b == nullptr || a->IsPendingDeletion() || b->IsPendingDeletion())
				continue;

			AddWaypointEdge(graph, a->GetName(), b->GetName());
			AddWaypointEdge(graph, b->GetName(), a->GetName());
		}

		if (lane->IsLooping() && laneWaypoints.size() > 2)
		{
			auto* first = laneWaypoints.front();
			auto* last = laneWaypoints.back();
			if (first != nullptr && last != nullptr && !first->IsPendingDeletion() && !last->IsPendingDeletion())
			{
				AddWaypointEdge(graph, last->GetName(), first->GetName());
				AddWaypointEdge(graph, first->GetName(), last->GetName());
			}
		}

		const auto& nextLaneNames = lane->GetNextLaneEntityNames();
		if (!nextLaneNames.empty())
		{
			auto* laneEntity = lane->GetEntity();
			auto* laneTerminal = laneWaypoints.back();
			if (laneEntity != nullptr && laneTerminal != nullptr && !laneTerminal->IsPendingDeletion())
			{
				for (const auto& nextLaneName : nextLaneNames)
				{
					if (nextLaneName.empty())
						continue;

					auto* nextLaneEntity = scene->GetEntityByName(nextLaneName);
					if (nextLaneEntity == nullptr || nextLaneEntity->IsPendingDeletion())
						continue;

					auto* nextLane = nextLaneEntity->GetComponent<TrafficLaneComponent>();
					if (nextLane == nullptr)
						continue;

					const auto nextCacheIt = laneWaypointCache.find(nextLaneEntity);
					if (nextCacheIt == laneWaypointCache.end())
						continue;

					const auto& nextWaypoints = nextCacheIt->second;
					if (nextWaypoints.empty() || nextWaypoints.front() == nullptr || nextWaypoints.front()->IsPendingDeletion())
						continue;

					AddWaypointEdge(graph, laneTerminal->GetName(), nextWaypoints.front()->GetName());
				}
			}
		}
	}

	const std::string startName = startEntity->GetName();
	const std::string destinationName = destinationEntity->GetName();
	if (graph.find(startName) == graph.end() || graph.find(destinationName) == graph.end())
		return false;

	struct OpenNode
	{
		float f = 0.0f;
		std::string name;
	};
	auto cmp = [](const OpenNode& a, const OpenNode& b) { return a.f > b.f; };
	std::priority_queue<OpenNode, std::vector<OpenNode>, decltype(cmp)> open(cmp);
	std::unordered_map<std::string, float> gScore;
	std::unordered_map<std::string, std::string> cameFrom;
	std::unordered_set<std::string> closed;

	for (const auto& [name, node] : graph)
		gScore[name] = std::numeric_limits<float>::infinity();

	gScore[startName] = 0.0f;
	open.push({ (graph[startName].position - graph[destinationName].position).Length(), startName });

	while (!open.empty())
	{
		const OpenNode current = open.top();
		open.pop();

		if (closed.find(current.name) != closed.end())
			continue;
		closed.insert(current.name);

		if (current.name == destinationName)
			break;

		const auto currentIt = graph.find(current.name);
		if (currentIt == graph.end())
			continue;

		const auto& currentNode = currentIt->second;
		for (const auto& neighbourName : currentNode.neighbours)
		{
			const auto neighbourIt = graph.find(neighbourName);
			if (neighbourIt == graph.end())
				continue;

			const float travelCost = (neighbourIt->second.position - currentNode.position).Length();
			const float tentativeG = gScore[current.name] + travelCost;
			if (tentativeG >= gScore[neighbourName])
				continue;

			cameFrom[neighbourName] = current.name;
			gScore[neighbourName] = tentativeG;
			const float h = (neighbourIt->second.position - graph[destinationName].position).Length();
			open.push({ tentativeG + h, neighbourName });
		}
	}

	if (startName != destinationName && cameFrom.find(destinationName) == cameFrom.end())
		return false;

	std::vector<std::string> pathNames;
	pathNames.push_back(destinationName);
	while (pathNames.back() != startName)
	{
		auto it = cameFrom.find(pathNames.back());
		if (it == cameFrom.end())
			return false;
		pathNames.push_back(it->second);
	}
	std::reverse(pathNames.begin(), pathNames.end());

	outPoints.reserve(pathNames.size());
	for (const auto& name : pathNames)
	{
		auto it = graph.find(name);
		if (it != graph.end())
			outPoints.push_back(it->second.position);
	}

	return outPoints.size() >= 2;
}

bool TrafficVehicleComponent::AdvancePlannedRouteIndex(size_t numPoints)
{
	if (numPoints < 2)
	{
		_targetIndex = 0;
		return true;
	}

	if (_invertDirection)
	{
		if (_targetIndex == 0)
			return true;

		--_targetIndex;
		return false;
	}

	++_targetIndex;
	if (_targetIndex >= numPoints)
	{
		_targetIndex = numPoints - 1;
		return true;
	}

	return false;
}

TrafficLaneComponent* TrafficVehicleComponent::ResolveLane()
{
	auto* entity = GetEntity();
	if (entity == nullptr)
		return nullptr;

	auto* scene = entity->GetScene();
	if (scene == nullptr)
		return nullptr;

	if (_laneEntityName.empty())
		return nullptr;

	auto* laneEntity = scene->GetEntityByName(_laneEntityName);
	if (laneEntity == nullptr || laneEntity->IsPendingDeletion())
		return nullptr;

	return laneEntity->GetComponent<TrafficLaneComponent>();
}

bool TrafficVehicleComponent::GatherLanePoints(std::vector<math::Vector3>& outPoints)
{
	auto* lane = ResolveLane();
	if (lane == nullptr)
		return false;

	lane->GatherLanePoints(outPoints);
	if (outPoints.size() >= 2)
		return true;

	// Lane graph can be authored as explicit next-node links.
	// Build a deterministic forward chain to provide at least 2 points for non-route driving mode.
	auto* scene = GetEntity() != nullptr ? GetEntity()->GetScene() : nullptr;
	if (scene == nullptr || outPoints.empty())
		return false;

	std::unordered_set<std::string> visited;
	auto* cursor = lane;
	while (cursor != nullptr && cursor->GetEntity() != nullptr)
	{
		if (!visited.insert(cursor->GetEntity()->GetName()).second)
			break;

		std::string nextName;
		TrafficLaneComponent* nextLane = nullptr;
		if (!TryResolveNextLane(scene, cursor, nextName, nextLane))
			break;

		auto* nextEntity = nextLane->GetEntity();
		if (nextEntity == nullptr || nextEntity->IsPendingDeletion())
			break;

		outPoints.push_back(nextEntity->GetWorldTM().Translation());
		if (outPoints.size() >= 2)
			return true;

		cursor = nextLane;
	}

	return outPoints.size() >= 2;
}

bool TrafficVehicleComponent::TrySwitchToConnectedLane()
{
	auto* currentLane = ResolveLane();
	if (currentLane == nullptr)
		return false;

	auto* scene = GetEntity()->GetScene();
	if (scene == nullptr)
		return false;

	std::string nextLaneName;
	TrafficLaneComponent* nextLane = nullptr;
	if (!TryResolveNextLane(scene, currentLane, nextLaneName, nextLane))
		return false;

	std::vector<math::Vector3> points;
	nextLane->GatherLanePoints(points);
	if (points.empty())
		return false;

	const math::Vector3 vehiclePos = GetEntity()->GetPosition();
	const float startDistanceSq = (points.front() - vehiclePos).LengthSquared();
	const float endDistanceSq = (points.back() - vehiclePos).LengthSquared();

	_laneEntityName = nextLaneName;
	if (points.size() >= 2)
	{
		_invertDirection = (endDistanceSq < startDistanceSq);
		_targetIndex = _invertDirection ? (points.size() - 1) : 0;
	}
	else
	{
		_invertDirection = false;
		_targetIndex = 0;
	}
	return true;
}

bool TrafficVehicleComponent::AdvanceTargetIndex(size_t numPoints)
{
	auto* lane = ResolveLane();
	if (lane == nullptr)
	{
		_targetIndex = 0;
		return true;
	}

	if (numPoints < 2)
	{
		if (!TrySwitchToConnectedLane())
		{
			// If next lanes are configured but currently unresolved, keep retrying instead of treating as route end.
			if (lane->GetNextLaneEntityNames().empty() == false)
			{
				_targetIndex = 0;
				return false;
			}

			_targetIndex = 0;
			return true;
		}
		return false;
	}

	if (_invertDirection)
	{
		if (_targetIndex == 0)
		{
			// In lane-graph mode, explicit next-lane links take precedence over local looping.
			if (TrySwitchToConnectedLane())
			{
				return false;
			}

			if (lane->IsLooping())
			{
				_targetIndex = numPoints - 1;
				return false;
			}
			else
			{
				if (lane->GetNextLaneEntityNames().empty() == false)
				{
					_targetIndex = 0;
					return false;
				}

				_targetIndex = 0;
				return true;
			}
			return false;
		}
		else
		{
			--_targetIndex;
			return false;
		}
	}
	else
	{
		++_targetIndex;
		if (_targetIndex >= numPoints)
		{
			// In lane-graph mode, explicit next-lane links take precedence over local looping.
			if (TrySwitchToConnectedLane())
			{
				return false;
			}

			if (lane->IsLooping())
			{
				_targetIndex = 0;
				return false;
			}
			else
			{
				if (lane->GetNextLaneEntityNames().empty() == false)
				{
					_targetIndex = numPoints - 1;
					return false;
				}

				_targetIndex = numPoints - 1;
				return true;
			}
			return false;
		}
	}

	return false;
}

float TrafficVehicleComponent::ComputeAvoidanceSpeed(const math::Vector3& currentPosition, const math::Vector3& moveDirection, float maxSpeed) const
{
	if (!_avoidanceEnabled)
		return maxSpeed;

	const float lookAhead = std::max(_avoidanceLookAheadDistance, 0.01f);
	const float followDistance = std::clamp(_avoidanceFollowDistance, 0.0f, lookAhead);

	// Half-width of the lane this vehicle should react in. 1.75m (~3.5m
	// lane) is the default city-traffic value and matches the original
	// constant. A future improvement would query the actual lane width
	// from TrafficLaneComponent if the lane carries that field.
	const float laneHalfWidth = 1.75f;
	const float laneHalfWidthSq = laneHalfWidth * laneHalfWidth;

	// Query only nearby vehicles via the spatial grid. With ~32m cells
	// and a typical 10m look-ahead, this hits the home cell + at most
	// one ring of neighbours - O(local-density) instead of O(N).
	// thread_local buffer avoids per-call vector allocation.
	thread_local std::vector<TrafficVehicleComponent*> nearby;
	QueryNearbyVehicles(currentPosition, lookAhead + 2.0f, nearby);

	float nearestAheadDistance = std::numeric_limits<float>::max();

	for (auto* other : nearby)
	{
		if (other == nullptr || other == this)
			continue;
		auto* otherEnt = other->GetEntity();
		if (otherEnt == nullptr || otherEnt->IsPendingDeletion())
			continue;

		const math::Vector3 otherPos = otherEnt->GetWorldTM().Translation();
		const math::Vector3 toOther = otherPos - currentPosition;

		// Forward projection. Drop strictly-behind and beyond look-ahead.
		// (Old code allowed slightly-behind via `-followDistance*0.5`,
		// which leaked false positives from cars that just passed us.)
		const float projectedDistance = toOther.Dot(moveDirection);
		if (projectedDistance < 0.0f || projectedDistance > lookAhead)
			continue;

		// Lateral squared-distance check via Pythagoras. Skip the sqrt -
		// compare squared values directly. Vehicles outside our lane
		// (beyond laneHalfWidth either side of our movement direction)
		// don't influence our braking.
		const float distSq = toOther.LengthSquared();
		const float lateralSq = std::max(0.0f, distSq - projectedDistance * projectedDistance);
		if (lateralSq > laneHalfWidthSq)
			continue;

		nearestAheadDistance = std::min(nearestAheadDistance, projectedDistance);
	}

	if (nearestAheadDistance == std::numeric_limits<float>::max())
		return maxSpeed;
	if (nearestAheadDistance <= followDistance)
		return 0.0f;

	// Smooth ramp from `followDistance` -> `lookAhead` mapping speed
	// 0 -> maxSpeed. Smoothstep gives nicer braking than the old linear
	// ramp (no sudden jolt as a vehicle enters the look-ahead band).
	const float denom = std::max(lookAhead - followDistance, 0.001f);
	const float alpha = std::clamp((nearestAheadDistance - followDistance) / denom, 0.0f, 1.0f);
	const float smoothed = alpha * alpha * (3.0f - 2.0f * alpha);
	return maxSpeed * smoothed;
}

void TrafficVehicleComponent::Update(float frameTime)
{
	UpdateComponent::Update(frameTime);

	if (frameTime <= 0.0f)
		return;

	auto* transform = GetEntity()->GetComponent<HexEngine::Transform>();
	if (transform == nullptr)
		return;

	std::vector<math::Vector3> points;
	if (!GatherPlannedRoute(points))
		return;

	// First Update after a scene load: place the vehicle ONTO the lane. Loaded
	// vehicles were never positioned by the spawner, so their saved/prefab
	// transform is unreliable (frequently under the map). Snap to the nearest
	// lane point and aim at the next one in the travel direction, then drive
	// normally from there. Guarded so spawner-spawned vehicles (which set their
	// own position) never hit this path.
	if (_snapToLaneOnLoad && !points.empty())
	{
		const math::Vector3 loadedPos = transform->GetPosition();
		size_t nearest = 0;
		float bestSq = 1e30f;
		for (size_t i = 0; i < points.size(); ++i)
		{
			const float dSq = (points[i] - loadedPos).LengthSquared();
			if (dSq < bestSq) { bestSq = dSq; nearest = i; }
		}
		GetEntity()->ForcePosition(points[nearest]);
		if (_invertDirection)
			_targetIndex = (nearest == 0) ? 0 : (nearest - 1);
		else
			_targetIndex = std::min(nearest + 1, points.size() - 1);
		_snapToLaneOnLoad = false;
	}

	_targetIndex = std::min(_targetIndex, points.size() - 1);
	math::Vector3 targetPoint = points[_targetIndex];
	math::Vector3 currentPosition = transform->GetPosition();

	math::Vector3 toTarget = targetPoint - currentPosition;
	float distanceToTarget = toTarget.Length();

	const float arrivalDistance = std::max(_arrivalDistance, 0.01f);
	if (distanceToTarget <= arrivalDistance)
	{
		const bool reachedRouteEnd = _useWaypointRoute
			? AdvancePlannedRouteIndex(points.size())
			: AdvanceTargetIndex(points.size());

		if (reachedRouteEnd)
		{
			if (_despawnAtRouteEnd)
			{
				GetEntity()->DeleteMe();
			}
			else
			{
				_routeEndReachedEvent = true;
			}
			_currentSpeed = 0.0f;
			return;
		}

		if (!GatherPlannedRoute(points))
			return;

		_targetIndex = std::min(_targetIndex, points.size() - 1);
		targetPoint = points[_targetIndex];
		toTarget = targetPoint - currentPosition;
		distanceToTarget = toTarget.Length();
	}

	if (distanceToTarget <= 0.001f)
		return;

	toTarget.Normalize();

	auto* lane = ResolveLane();
	const float laneSpeed = lane != nullptr ? lane->GetSpeedLimit() : _speed;
	const float maxSpeed = std::max(_useLaneSpeedLimit ? laneSpeed : _speed, 0.0f);
	// Cache the achievable top speed so the audio system can map
	// pitch correctly. Using _speed as the denominator means cars on
	// slow lanes never reach max pitch even at their actual cruise.
	_audioReferenceMaxSpeed = std::max(maxSpeed, 0.01f);
	if (maxSpeed <= 0.0f)
	{
		_currentSpeed = 0.0f;
		return;
	}

	const float desiredSpeed = ComputeAvoidanceSpeed(currentPosition, toTarget, maxSpeed);
	const float accel = std::max(_acceleration, 0.0f);
	const float brake = std::max(_brakingStrength, 0.0f);

	if (_currentSpeed < desiredSpeed)
	{
		_currentSpeed = std::min(_currentSpeed + accel * frameTime, desiredSpeed);
	}
	else
	{
		_currentSpeed = std::max(_currentSpeed - brake * frameTime, desiredSpeed);
	}

	currentPosition += toTarget * (_currentSpeed * frameTime);
	transform->SetPositionNoNotify(currentPosition);

	const float yaw = atan2f(toTarget.x, toTarget.z);
	const auto targetRotation = math::Quaternion::CreateFromYawPitchRoll(yaw, 0.0f, 0.0f);
	// Frame-rate-independent exponential damping. The old `clamp(dt*k, 0, 1)`
	// formula scaled the per-frame blend factor LINEARLY with dt, which
	// meant the effective rotation rate doubled at half FPS and saturated
	// to instant snap at very low FPS. `1 - exp(-k*dt)` gives the same
	// time constant regardless of FPS - identical visual rotation speed
	// at 30, 60, 120 fps.
	const float rotationBlend = 1.0f - std::exp(-std::max(_rotationLerp, 0.01f) * frameTime);
	transform->SetRotationNoNotify(math::Quaternion::Slerp(transform->GetRotation(), targetRotation, rotationBlend));

	// Engine sound + honk logic. Done at the END so _currentSpeed
	// reflects this frame's avoidance-adjusted target speed, not last
	// frame's.
	UpdateAudio(currentPosition, frameTime);
}

void TrafficVehicleComponent::Serialize(json& data, HexEngine::JsonFile* file)
{
	file->Serialize(data, "_laneEntityName", _laneEntityName);
	file->Serialize(data, "_speed", _speed);
	file->Serialize(data, "_acceleration", _acceleration);
	file->Serialize(data, "_rotationLerp", _rotationLerp);
	file->Serialize(data, "_arrivalDistance", _arrivalDistance);
	file->Serialize(data, "_useLaneSpeedLimit", _useLaneSpeedLimit);
	file->Serialize(data, "_invertDirection", _invertDirection);
	file->Serialize(data, "_despawnAtRouteEnd", _despawnAtRouteEnd);
	file->Serialize(data, "_useWaypointRoute", _useWaypointRoute);
	file->Serialize(data, "_startWaypointEntityName", _startWaypointEntityName);
	file->Serialize(data, "_destinationWaypointEntityName", _destinationWaypointEntityName);
	file->Serialize(data, "_drawDebug", _drawDebug);
	file->Serialize(data, "_avoidanceEnabled", _avoidanceEnabled);
	file->Serialize(data, "_avoidanceLookAheadDistance", _avoidanceLookAheadDistance);
	file->Serialize(data, "_avoidanceFollowDistance", _avoidanceFollowDistance);
	file->Serialize(data, "_brakingStrength", _brakingStrength);

	file->Serialize(data, "_engineSoundPath", _engineSoundPath);
	file->Serialize(data, "_honkSoundPaths", _honkSoundPaths);
	file->Serialize(data, "_audioCullDistance", _audioCullDistance);
	file->Serialize(data, "_engineSoundRadius", _engineSoundRadius);
	file->Serialize(data, "_honkSoundRadius", _honkSoundRadius);
	file->Serialize(data, "_engineIdlePitch", _engineIdlePitch);
	file->Serialize(data, "_engineMaxPitch", _engineMaxPitch);
	file->Serialize(data, "_engineIdleVolume", _engineIdleVolume);
	file->Serialize(data, "_engineMaxVolume", _engineMaxVolume);
	file->Serialize(data, "_engineNumGears", _engineNumGears);
	file->Serialize(data, "_engineShiftDamping", _engineShiftDamping);
	file->Serialize(data, "_honkBlockedThreshold", _honkBlockedThreshold);
	file->Serialize(data, "_honkCooldownMin", _honkCooldownMin);
	file->Serialize(data, "_honkCooldownMax", _honkCooldownMax);
}

void TrafficVehicleComponent::Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask)
{
	file->Deserialize(data, "_laneEntityName", _laneEntityName);
	file->Deserialize(data, "_speed", _speed);
	file->Deserialize(data, "_acceleration", _acceleration);
	file->Deserialize(data, "_rotationLerp", _rotationLerp);
	file->Deserialize(data, "_arrivalDistance", _arrivalDistance);
	file->Deserialize(data, "_useLaneSpeedLimit", _useLaneSpeedLimit);
	file->Deserialize(data, "_invertDirection", _invertDirection);
	file->Deserialize(data, "_despawnAtRouteEnd", _despawnAtRouteEnd);
	file->Deserialize(data, "_useWaypointRoute", _useWaypointRoute);
	file->Deserialize(data, "_startWaypointEntityName", _startWaypointEntityName);
	file->Deserialize(data, "_destinationWaypointEntityName", _destinationWaypointEntityName);
	file->Deserialize(data, "_drawDebug", _drawDebug);
	file->Deserialize(data, "_avoidanceEnabled", _avoidanceEnabled);
	file->Deserialize(data, "_avoidanceLookAheadDistance", _avoidanceLookAheadDistance);
	file->Deserialize(data, "_avoidanceFollowDistance", _avoidanceFollowDistance);
	file->Deserialize(data, "_brakingStrength", _brakingStrength);

	file->Deserialize(data, "_engineSoundPath", _engineSoundPath);
	file->Deserialize(data, "_honkSoundPaths", _honkSoundPaths);
	file->Deserialize(data, "_audioCullDistance", _audioCullDistance);
	file->Deserialize(data, "_engineSoundRadius", _engineSoundRadius);
	file->Deserialize(data, "_honkSoundRadius", _honkSoundRadius);
	file->Deserialize(data, "_engineIdlePitch", _engineIdlePitch);
	file->Deserialize(data, "_engineMaxPitch", _engineMaxPitch);
	file->Deserialize(data, "_engineIdleVolume", _engineIdleVolume);
	file->Deserialize(data, "_engineMaxVolume", _engineMaxVolume);
	file->Deserialize(data, "_engineNumGears", _engineNumGears);
	file->Deserialize(data, "_engineShiftDamping", _engineShiftDamping);
	file->Deserialize(data, "_honkBlockedThreshold", _honkBlockedThreshold);
	file->Deserialize(data, "_honkCooldownMin", _honkCooldownMin);
	file->Deserialize(data, "_honkCooldownMax", _honkCooldownMax);
	RestartPath();
	// The lane entity may not be loaded yet at deserialize time (entities load
	// in arbitrary order), so RestartPath above can't position us. Defer the
	// snap-onto-lane to the first Update where the lane resolves - see the
	// _snapToLaneOnLoad handling in Update. Without this a loaded vehicle sits
	// at its raw saved transform (often under the map) and never reaches the
	// road.
	_snapToLaneOnLoad = true;
}

bool TrafficVehicleComponent::CreateWidget(HexEngine::ComponentWidget* widget)
{
	auto* laneName = new HexEngine::EntitySearch(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Lane Entity");
	laneName->SetValue(std::wstring(_laneEntityName.begin(), _laneEntityName.end()));
	laneName->SetOnInputFn([this](HexEngine::EntitySearch* search, const std::wstring& value)
		{
			_laneEntityName = ws2s(value);
			RestartPath();
		});
	laneName->SetOnSelectFn([this](HexEngine::EntitySearch* search, const HexEngine::EntitySearchResult& result)
		{
			_laneEntityName = result.entityName;
			RestartPath();
		});
	laneName->SetPrefabOverrideBinding(GetComponentName(), "/_laneEntityName");

	new HexEngine::Button(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Use Parent Lane", [this, laneName](HexEngine::Button* button) -> bool
		{
			auto* parent = GetEntity() != nullptr ? GetEntity()->GetParent() : nullptr;
			if (parent == nullptr || parent->GetComponent<TrafficLaneComponent>() == nullptr)
				return false;

			_laneEntityName = parent->GetName();
			laneName->SetValue(std::wstring(_laneEntityName.begin(), _laneEntityName.end()));
			RestartPath();
			return true;
		});

	auto* startWaypoint = new HexEngine::EntitySearch(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Route Start Waypoint");
	startWaypoint->SetValue(std::wstring(_startWaypointEntityName.begin(), _startWaypointEntityName.end()));
	startWaypoint->SetOnInputFn([this](HexEngine::EntitySearch* search, const std::wstring& value)
		{
			_startWaypointEntityName = ws2s(value);
			RestartPath();
		});
	startWaypoint->SetOnSelectFn([this](HexEngine::EntitySearch* search, const HexEngine::EntitySearchResult& result)
		{
			_startWaypointEntityName = result.entityName;
			RestartPath();
		});

	auto* destinationWaypoint = new HexEngine::EntitySearch(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Route Destination");
	destinationWaypoint->SetValue(std::wstring(_destinationWaypointEntityName.begin(), _destinationWaypointEntityName.end()));
	destinationWaypoint->SetOnInputFn([this](HexEngine::EntitySearch* search, const std::wstring& value)
		{
			_destinationWaypointEntityName = ws2s(value);
			RestartPath();
		});
	destinationWaypoint->SetOnSelectFn([this](HexEngine::EntitySearch* search, const HexEngine::EntitySearchResult& result)
		{
			_destinationWaypointEntityName = result.entityName;
			RestartPath();
		});

	auto* speed = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Speed", &_speed, 0.0f, 5000.0f, 0.1f, 2);
	auto* acceleration = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Acceleration", &_acceleration, 0.0f, 10000.0f, 0.1f, 2);
	auto* braking = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Braking Strength", &_brakingStrength, 0.0f, 10000.0f, 0.1f, 2);
	auto* rotationLerp = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Rotation Lerp", &_rotationLerp, 0.0f, 100.0f, 0.1f, 2);
	auto* arrivalDistance = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Arrival Distance", &_arrivalDistance, 0.01f, 500.0f, 0.1f, 2);
	auto* useLaneSpeed = new HexEngine::Checkbox(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Use Lane Speed", &_useLaneSpeedLimit);
	auto* invertDirection = new HexEngine::Checkbox(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Invert Direction", &_invertDirection);
	auto* despawnAtRouteEnd = new HexEngine::Checkbox(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Despawn At Route End", &_despawnAtRouteEnd);
	auto* useWaypointRoute = new HexEngine::Checkbox(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Use Waypoint Route", &_useWaypointRoute);
	auto* avoidanceEnabled = new HexEngine::Checkbox(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Avoidance Enabled", &_avoidanceEnabled);
	auto* avoidLookAhead = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Avoid Look Ahead", &_avoidanceLookAheadDistance, 0.1f, 1000.0f, 0.1f, 2);
	auto* avoidFollow = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Avoid Follow Dist", &_avoidanceFollowDistance, 0.0f, 1000.0f, 0.1f, 2);
	auto* drawDebug = new HexEngine::Checkbox(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Draw Debug", &_drawDebug);

	new HexEngine::Button(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Rebuild Route", [this](HexEngine::Button* button) -> bool
		{
			RestartPath();
			return true;
		});

	speed->SetPrefabOverrideBinding(GetComponentName(), "/_speed");
	acceleration->SetPrefabOverrideBinding(GetComponentName(), "/_acceleration");
	braking->SetPrefabOverrideBinding(GetComponentName(), "/_brakingStrength");
	rotationLerp->SetPrefabOverrideBinding(GetComponentName(), "/_rotationLerp");
	arrivalDistance->SetPrefabOverrideBinding(GetComponentName(), "/_arrivalDistance");
	useLaneSpeed->SetPrefabOverrideBinding(GetComponentName(), "/_useLaneSpeedLimit");
	invertDirection->SetPrefabOverrideBinding(GetComponentName(), "/_invertDirection");
	despawnAtRouteEnd->SetPrefabOverrideBinding(GetComponentName(), "/_despawnAtRouteEnd");
	useWaypointRoute->SetPrefabOverrideBinding(GetComponentName(), "/_useWaypointRoute");
	startWaypoint->SetPrefabOverrideBinding(GetComponentName(), "/_startWaypointEntityName");
	destinationWaypoint->SetPrefabOverrideBinding(GetComponentName(), "/_destinationWaypointEntityName");
	avoidanceEnabled->SetPrefabOverrideBinding(GetComponentName(), "/_avoidanceEnabled");
	avoidLookAhead->SetPrefabOverrideBinding(GetComponentName(), "/_avoidanceLookAheadDistance");
	avoidFollow->SetPrefabOverrideBinding(GetComponentName(), "/_avoidanceFollowDistance");
	drawDebug->SetPrefabOverrideBinding(GetComponentName(), "/_drawDebug");

	// --- Audio inspector ---
	auto* engineSoundSearch = new HexEngine::AssetSearch(
		widget, widget->GetNextPos(),
		HexEngine::Point(widget->GetSize().x - 20, 22),
		L"Engine Sound (loop)",
		{ HexEngine::ResourceType::Audio });
	engineSoundSearch->SetValue(std::wstring(_engineSoundPath.begin(), _engineSoundPath.end()));
	engineSoundSearch->SetOnSelectFn([this](HexEngine::AssetSearch* search, const HexEngine::AssetSearchResult& result)
		{
			const fs::path chosen = !result.assetPath.empty() ? result.assetPath : result.absolutePath;
			_engineSoundPath = chosen.generic_string();
			// Path changed - drop any cached loop so the next Update
			// reloads it. StopEngineSound() bails cleanly if nothing
			// is playing.
			StopEngineSound();
			_engineSound.reset();
		});
	engineSoundSearch->SetPrefabOverrideBinding(GetComponentName(), "/_engineSoundPath");

	new HexEngine::ArrayElement<std::string>(
		widget,
		widget->GetNextPos(),
		HexEngine::Point(widget->GetSize().x - 20, 132),
		L"Honk Variants",
		_honkSoundPaths,
		[](HexEngine::Element* parent, std::string& item, int32_t index)
		{
			auto* honkSearch = new HexEngine::AssetSearch(
				parent,
				HexEngine::Point(0, 0),
				HexEngine::Point(parent->GetSize().x, 22),
				L"Honk",
				{ HexEngine::ResourceType::Audio });
			honkSearch->SetValue(std::wstring(item.begin(), item.end()));
			honkSearch->SetOnSelectFn([&item](HexEngine::AssetSearch* s, const HexEngine::AssetSearchResult& result)
				{
					const fs::path chosen = !result.assetPath.empty() ? result.assetPath : result.absolutePath;
					item = chosen.generic_string();
				});
		},
		[]() -> std::string { return std::string(); },
		[](const std::string& item, int32_t index) -> int32_t { return 34; },
		[](const std::string& item, int32_t index) -> std::wstring { return std::format(L"Honk {}", index + 1); });

	auto* audioCull       = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Audio Cull Distance", &_audioCullDistance, 1.0f, 1000.0f, 1.0f, 1);
	auto* engineRadius    = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Engine Radius",        &_engineSoundRadius, 1.0f, 500.0f, 0.5f, 1);
	auto* honkRadius      = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Honk Radius",          &_honkSoundRadius,   1.0f, 1000.0f, 1.0f, 1);
	// Pitch sliders are in DirectX semi-octave units [-1, 1] (NOT a
	// frequency multiplier). The old 0.1..4.0 range was a holdover from
	// when this used a Unity-style multiplier convention; saving any
	// value > 1 there would tank the audio thread until the runtime
	// clamp in SoundEffect::SetPitch caught it.
	auto* engineIdlePitch = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Engine Idle Pitch (semi-oct)", &_engineIdlePitch, -1.0f, 1.0f, 0.01f, 2);
	auto* engineMaxPitch  = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Engine Max Pitch (semi-oct)",  &_engineMaxPitch,  -1.0f, 1.0f, 0.01f, 2);
	auto* engineIdleVol   = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Engine Idle Volume",   &_engineIdleVolume,  0.0f, 1.0f, 0.01f, 2);
	auto* engineMaxVol    = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Engine Max Volume",    &_engineMaxVolume,   0.0f, 1.0f, 0.01f, 2);
	auto* engineGears     = new HexEngine::DragInt  (widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Engine Num Gears",     &_engineNumGears, 1, 8, 1);
	auto* engineShiftDamp = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Engine Shift Damping", &_engineShiftDamping, 0.0f, 60.0f, 0.5f, 1);
	auto* honkThreshold   = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Honk After Blocked (s)", &_honkBlockedThreshold, 0.1f, 30.0f, 0.1f, 2);
	auto* honkCdMin       = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Honk Cooldown Min (s)",   &_honkCooldownMin, 0.5f, 60.0f, 0.1f, 2);
	auto* honkCdMax       = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Honk Cooldown Max (s)",   &_honkCooldownMax, 0.5f, 60.0f, 0.1f, 2);

	audioCull      ->SetPrefabOverrideBinding(GetComponentName(), "/_audioCullDistance");
	engineRadius   ->SetPrefabOverrideBinding(GetComponentName(), "/_engineSoundRadius");
	honkRadius     ->SetPrefabOverrideBinding(GetComponentName(), "/_honkSoundRadius");
	engineIdlePitch->SetPrefabOverrideBinding(GetComponentName(), "/_engineIdlePitch");
	engineMaxPitch ->SetPrefabOverrideBinding(GetComponentName(), "/_engineMaxPitch");
	engineIdleVol  ->SetPrefabOverrideBinding(GetComponentName(), "/_engineIdleVolume");
	engineMaxVol   ->SetPrefabOverrideBinding(GetComponentName(), "/_engineMaxVolume");
	engineGears    ->SetPrefabOverrideBinding(GetComponentName(), "/_engineNumGears");
	engineShiftDamp->SetPrefabOverrideBinding(GetComponentName(), "/_engineShiftDamping");
	honkThreshold  ->SetPrefabOverrideBinding(GetComponentName(), "/_honkBlockedThreshold");
	honkCdMin      ->SetPrefabOverrideBinding(GetComponentName(), "/_honkCooldownMin");
	honkCdMax      ->SetPrefabOverrideBinding(GetComponentName(), "/_honkCooldownMax");

	return true;
}

void TrafficVehicleComponent::OnRenderEditorGizmo(bool isSelected, bool& isHovering)
{
	if (!HexEngine::g_pEnv->IsEditorMode())
		return;

	if (!_drawDebug && !isSelected)
		return;

	std::vector<math::Vector3> points;
	if (!GatherPlannedRoute(points) || points.empty())
		return;

	_targetIndex = std::min(_targetIndex, points.size() - 1);
	const math::Vector3 from = GetEntity()->GetWorldTM().Translation();
	const math::Vector3 to = points[_targetIndex];

	HexEngine::g_pEnv->_debugRenderer->DrawLine(from, to, math::Color(HEX_RGB_TO_FLOAT3(52, 152, 219), 1.0f));
	DrawPointMarker(to, 0.4f, math::Color(HEX_RGB_TO_FLOAT3(52, 152, 219), 1.0f));

	if (_useWaypointRoute && points.size() > 1)
	{
		for (size_t i = 0; i + 1 < points.size(); ++i)
		{
			HexEngine::g_pEnv->_debugRenderer->DrawLine(points[i], points[i + 1], math::Color(HEX_RGBA_TO_FLOAT4(52, 152, 219, 90)));
		}
	}
}