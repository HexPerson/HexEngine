#include "TrafficVehicleComponent.hpp"
#include "VehicleComponent.hpp"
#include <chrono>
#include "TrafficLaneComponent.hpp"
#include "LanePathSmoothing.hpp"
#include "TrafficSignalControllerComponent.hpp"
#include "CityAssetPins.hpp"
#include <HexEngine.Core/Audio/AudioManager.hpp>
#include <HexEngine.Core/GUI/Elements/ArrayElement.hpp>
#include <HexEngine.Core/GUI/Elements/AssetSearch.hpp>
#include <functional>
#include <limits>
#include <queue>
#include <unordered_map>
#include <unordered_set>

std::vector<TrafficVehicleComponent*> TrafficVehicleComponent::s_allVehicles;

namespace
{
	// Player's ridden vehicle (TrafficVehicleComponent::SetPlayerObstacle).
	bool s_playerObstacleValid = false;
	math::Vector3 s_playerObstaclePos;
	math::Vector3 s_playerObstacleVel;
}

void TrafficVehicleComponent::SetPlayerObstacle(bool valid, const math::Vector3& pos, const math::Vector3& vel)
{
	s_playerObstacleValid = valid;
	s_playerObstaclePos = pos;
	s_playerObstacleVel = vel;
}

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
		float cornerRadius = 0.0f;
		std::vector<std::string> neighbours;
		int32_t inCount = 0;
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
		{
			neighbours.push_back(to);
			toIt->second.inCount += 1;
		}
	}

	// The lane graph (one node per lane entity, directed edges along "Next
	// Lanes"). Planning used to rebuild it from every lane component for each
	// route; trips now compare several candidate end nodes (parking access), so
	// it is cached and rebuilt only when the scene or its lane count changes
	// (lanes don't move in play; in the editor it refreshes every second).
	struct LaneGraphCache
	{
		HexEngine::Scene* scene = nullptr;
		size_t laneCount = 0;
		uint32_t topologyRevision = 0;	// TrafficLaneComponent open/close changes
		std::chrono::steady_clock::time_point builtAt{};
		std::unordered_map<std::string, WaypointGraphNode> graph;
	};

	const std::unordered_map<std::string, WaypointGraphNode>& GetLaneGraph(HexEngine::Scene* scene)
	{
		static LaneGraphCache cache;
		std::vector<TrafficLaneComponent*> lanes;
		if (scene != nullptr)
			scene->GetComponents<TrafficLaneComponent>(lanes);

		const auto now = std::chrono::steady_clock::now();
		const bool editing = HexEngine::g_pEnv != nullptr && HexEngine::g_pEnv->IsEditorMode() && !HexEngine::g_pEnv->IsGameRunning();
		const uint32_t revision = TrafficLaneComponent::GetTopologyRevision();
		const bool stale = cache.scene != scene || cache.laneCount != lanes.size() ||
			cache.topologyRevision != revision ||
			(editing && now - cache.builtAt > std::chrono::seconds(1));
		if (!stale)
			return cache.graph;

		cache.scene = scene;
		cache.laneCount = lanes.size();
		cache.topologyRevision = revision;
		cache.builtAt = now;
		cache.graph.clear();
		if (scene == nullptr)
			return cache.graph;

		auto& graph = cache.graph;
		for (auto* lane : lanes)
		{
			if (lane == nullptr || lane->GetEntity() == nullptr || lane->GetEntity()->IsPendingDeletion() || lane->IsClosed())
				continue;
			auto& node = graph[lane->GetEntity()->GetName()];
			node.position = lane->GetEntity()->GetWorldTM().Translation();
			node.cornerRadius = lane->GetCornerRadius();
		}
		for (auto* lane : lanes)
		{
			if (lane == nullptr || lane->GetEntity() == nullptr || lane->GetEntity()->IsPendingDeletion() || lane->IsClosed())
				continue;
			// A lane is a single node, so its only edges are its "Next Lanes".
			for (const auto& nextLaneName : lane->GetNextLaneEntityNames())
			{
				if (nextLaneName.empty())
					continue;
				auto* nextLaneEntity = scene->GetEntityByName(nextLaneName);
				auto* nextLaneComp = nextLaneEntity != nullptr ? nextLaneEntity->GetComponent<TrafficLaneComponent>() : nullptr;
				if (nextLaneEntity == nullptr || nextLaneEntity->IsPendingDeletion() || nextLaneComp == nullptr || nextLaneComp->IsClosed())
					continue;
				AddWaypointEdge(graph, lane->GetEntity()->GetName(), nextLaneName);
			}
		}
		return cache.graph;
	}

	// A* over the lane graph. outLength = driven length along the nodes.
	bool PlanOnLaneGraph(const std::unordered_map<std::string, WaypointGraphNode>& graph, const std::string& startName,
		const std::string& destinationName, std::vector<std::string>* outPath, float* outLength)
	{
		const auto startIt = graph.find(startName);
		const auto destIt = graph.find(destinationName);
		if (startIt == graph.end() || destIt == graph.end())
			return false;
		const math::Vector3 goal = destIt->second.position;

		struct OpenNode
		{
			float f = 0.0f;
			const std::string* name = nullptr;
		};
		auto cmp = [](const OpenNode& a, const OpenNode& b) { return a.f > b.f; };
		std::priority_queue<OpenNode, std::vector<OpenNode>, decltype(cmp)> open(cmp);
		std::unordered_map<std::string, float> gScore;
		std::unordered_map<std::string, const std::string*> cameFrom;
		std::unordered_set<std::string> closed;

		gScore[startName] = 0.0f;
		open.push({ (startIt->second.position - goal).Length(), &startIt->first });
		bool found = startName == destinationName;
		while (!open.empty() && !found)
		{
			const OpenNode current = open.top();
			open.pop();
			if (!closed.insert(*current.name).second)
				continue;
			if (*current.name == destinationName)
			{
				found = true;
				break;
			}
			const auto& currentNode = graph.at(*current.name);
			const float currentG = gScore[*current.name];
			for (const auto& neighbourName : currentNode.neighbours)
			{
				const auto neighbourIt = graph.find(neighbourName);
				if (neighbourIt == graph.end())
					continue;
				const float tentativeG = currentG + (neighbourIt->second.position - currentNode.position).Length();
				auto gIt = gScore.find(neighbourName);
				if (gIt != gScore.end() && tentativeG >= gIt->second)
					continue;
				gScore[neighbourName] = tentativeG;
				cameFrom[neighbourName] = current.name;
				open.push({ tentativeG + (neighbourIt->second.position - goal).Length(), &neighbourIt->first });
			}
		}
		if (!found)
			return false;

		if (outLength != nullptr)
			*outLength = gScore[destinationName];
		if (outPath != nullptr)
		{
			outPath->clear();
			outPath->push_back(destinationName);
			while (outPath->back() != startName)
			{
				auto it = cameFrom.find(outPath->back());
				if (it == cameFrom.end())
					return false;
				outPath->push_back(*it->second);
			}
			std::reverse(outPath->begin(), outPath->end());
		}
		return true;
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
			if (nextLane == nullptr || nextLane->IsClosed())
				continue;	// closed = into a locked district (DistrictGateComponent)

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
	_enginePitchOffset = HexEngine::GetRandomFloat(-0.1f, 0.1f);
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
		_honkChance = copy->_honkChance;
		_parked = copy->_parked;
		_honkVolume = copy->_honkVolume;
	}

	s_allVehicles.push_back(this);
	// Fresh personality even on clones - traffic should still vary.
	_honkPersonality = HexEngine::GetRandomFloat(0.7f, 1.4f);
	_enginePitchOffset = HexEngine::GetRandomFloat(-0.1f, 0.1f);
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
		if (v == nullptr || v->_parked) continue;
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
		if (v == nullptr || v->_parked) continue;
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
		auto master = CityAssetPins::Sound(_engineSoundPath);
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

namespace
{
	// City-wide honk etiquette. A queue of cars that all see "blocked" at the
	// same moment used to honk together, and every jam became a wall of horns.
	// Recent honks (where + when) let a car stay quiet if someone nearby just
	// honked, and a global gap keeps the soundscape from ever being mostly horn.
	struct RecentHonk { math::Vector3 pos; double time; };
	std::vector<RecentHonk>& RecentHonks()
	{
		static std::vector<RecentHonk> honks;
		return honks;
	}

	double HonkClockSeconds()
	{
		using namespace std::chrono;
		return duration<double>(steady_clock::now().time_since_epoch()).count();
	}

	constexpr double kHonkNearbySuppressSeconds = 10.0;
	constexpr float kHonkNearbySuppressRadius = 35.0f;
	constexpr double kHonkGlobalGapSeconds = 2.5;

	bool HonkAllowed(const math::Vector3& pos)
	{
		const double now = HonkClockSeconds();
		auto& honks = RecentHonks();
		honks.erase(std::remove_if(honks.begin(), honks.end(),
			[now](const RecentHonk& h) { return now - h.time > kHonkNearbySuppressSeconds; }), honks.end());

		for (const auto& h : honks)
		{
			if (now - h.time < kHonkGlobalGapSeconds)
				return false;
			if (math::Vector3::DistanceSquared(h.pos, pos) < kHonkNearbySuppressRadius * kHonkNearbySuppressRadius)
				return false;
		}
		return true;
	}

	void RecordHonk(const math::Vector3& pos)
	{
		RecentHonks().push_back({ pos, HonkClockSeconds() });
	}
}

void TrafficVehicleComponent::TryHonk(const math::Vector3& worldPos)
{
	if (!HonkAllowed(worldPos)) return;
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
	auto master = CityAssetPins::Sound(chosenPath);
	if (!master) return;
	auto honk = master->CreatePlaybackClone();
	if (!honk) return;
	honk->SetRadius(_honkSoundRadius);
	honk->SetVolume(std::clamp(_honkVolume, 0.0f, 1.0f));
	// Slight per-honk pitch variation for natural-sounding traffic.
	// In DirectX semi-octave units [-1, 1]; ±0.05 = ~3.5% frequency shift.
	honk->SetPitch(HexEngine::GetRandomFloat(-0.05f, 0.05f));
	HexEngine::g_pEnv->_audioManager->Play(honk, worldPos);
	RecordHonk(worldPos);
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
	// Rolling wheels: only where someone could see them turn.
	if (distSqToCamera < 120.0f * 120.0f && _currentSpeed > 0.01f)
	{
		if (auto* vehicle = GetEntity()->GetComponent<VehicleComponent>(); vehicle != nullptr)
			vehicle->SpinWheels(_currentSpeed * frameTime);
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
		// Engine load: speeding up works the engine harder (higher, louder), lifting
		// off drops it - otherwise a car at steady speed sits on one pitch forever.
		const float accelNow = frameTime > 0.0f ? (_currentSpeed - _audioPrevSpeed) / frameTime : 0.0f;
		_audioPrevSpeed = _currentSpeed;
		_engineLoad = std::lerp(_engineLoad, std::clamp(accelNow / 3.0f, -1.0f, 1.0f), 1.0f - std::exp(-3.0f * frameTime));
		_engineWobblePhase += frameTime * (0.35f + std::abs(_enginePitchOffset) * 4.0f);
		const float wobble = 0.03f * std::sin(_engineWobblePhase);
		const float loadPitch = 0.2f * std::max(_engineLoad, 0.0f) - 0.08f * std::max(-_engineLoad, 0.0f);
		const float targetPitch   = std::lerp(_engineIdlePitch, _engineMaxPitch, gearLocal) + _enginePitchOffset + loadPitch + wobble;

		// Smooth the abrupt drop at gear boundaries so the "shift"
		// doesn't crackle. Exponential damping - same frame-rate
		// independent pattern used for vehicle rotation.
		const float shiftBlend    = 1.0f - std::exp(-std::max(_engineShiftDamping, 0.01f) * frameTime);
		_engineSmoothedPitch      = std::lerp(_engineSmoothedPitch, targetPitch, shiftBlend);

		const float vol = std::lerp(_engineIdleVolume, _engineMaxVolume, speedAlpha) * (1.0f + 0.35f * std::max(_engineLoad, 0.0f) - 0.15f * std::max(-_engineLoad, 0.0f));
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
	if (!_honkerRolled)
	{
		_honkerRolled = true;
		_isHonker = HexEngine::GetRandomFloat(0.0f, 1.0f) < std::clamp(_honkChance, 0.0f, 1.0f);
	}

	const bool isMoving = _currentSpeed > 0.5f;
	const bool wantsToMove = _speed > 0.1f;
	if (wantsToMove && !isMoving && !_queuedAtSignal && _isHonker)
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
	// Off-lane legs belong to one trip: whoever re-targets the route sets them again.
	_leadInPoints.clear();
	_leadOutPoints.clear();
	_reverseLeadInPoints = 0;
}

void TrafficVehicleComponent::SetOffLaneLegs(const std::vector<math::Vector3>& leadIn, int32_t reverseLeadInPoints, const std::vector<math::Vector3>& leadOut)
{
	_leadInPoints = leadIn;
	_leadOutPoints = leadOut;
	_reverseLeadInPoints = std::clamp(reverseLeadInPoints, 0, (int32_t)leadIn.size());
}

bool TrafficVehicleComponent::IsOffLane() const
{
	if (!_useWaypointRoute || _plannedRoutePoints.empty())
		return false;
	return _targetIndex <= _leadInEnd || _targetIndex >= _leadOutStart;
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
	_cornerPoints.clear();
	_cornerIndex = 0;
	_cornerSpeedScale = 1.0f;
	_pendingNextLaneName.clear();
	_chainedLaneNames.clear();
	_leadInEnd = 0;
	_leadOutStart = std::numeric_limits<size_t>::max();

	if (_useWaypointRoute)
	{
		std::vector<math::Vector3> route;
		std::vector<float> cornerRadii;
		if (BuildWaypointRoute(route, cornerRadii) && route.size() >= 2)
		{
			// Pre-bake the corner fillets straight into the planned route so
			// the vehicle sweeps corners instead of point-turning at nodes.
			std::vector<math::Vector3> laneRoute;
			LanePathSmoothing::SmoothPolyline(route, cornerRadii, laneRoute);

			// Off-lane legs (out of a parking bay / driveway, into one) wrap the
			// lane route. They are driven slowly and are not smoothed: bays are
			// entered square.
			auto appendDistinct = [this](const math::Vector3& p)
			{
				if (_plannedRoutePoints.empty() || (p - _plannedRoutePoints.back()).LengthSquared() > 0.04f)
					_plannedRoutePoints.push_back(p);
			};
			for (const auto& p : _leadInPoints)
				appendDistinct(p);
			_leadInEnd = _plannedRoutePoints.empty() ? 0 : _plannedRoutePoints.size() - 1;
			_reverseUntilIndex = (size_t)std::min<int32_t>(_reverseLeadInPoints, (int32_t)_leadInEnd);
			for (const auto& p : laneRoute)
				appendDistinct(p);
			_leadOutStart = _leadOutPoints.empty() ? std::numeric_limits<size_t>::max() : _plannedRoutePoints.size();
			for (const auto& p : _leadOutPoints)
				appendDistinct(p);
			if (_plannedRoutePoints.size() < 2)
				_plannedRoutePoints.clear();
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

bool TrafficVehicleComponent::BuildWaypointRoute(std::vector<math::Vector3>& outPoints, std::vector<float>& outCornerRadii) const
{
	outPoints.clear();
	outCornerRadii.clear();

	auto* entity = GetEntity();
	auto* scene = entity != nullptr ? entity->GetScene() : nullptr;
	if (scene == nullptr || _startWaypointEntityName.empty() || _destinationWaypointEntityName.empty())
		return false;

	const auto& graph = GetLaneGraph(scene);
	std::vector<std::string> pathNames;
	if (!PlanOnLaneGraph(graph, _startWaypointEntityName, _destinationWaypointEntityName, &pathNames, nullptr))
		return false;

	outPoints.reserve(pathNames.size());
	outCornerRadii.reserve(pathNames.size());
	for (const auto& name : pathNames)
	{
		auto it = graph.find(name);
		if (it != graph.end())
		{
			outPoints.push_back(it->second.position);
			outCornerRadii.push_back(it->second.cornerRadius);
		}
	}

	return outPoints.size() >= 2;
}

float TrafficVehicleComponent::EstimateLaneRouteLength(HexEngine::Scene* scene, const std::string& startWaypointEntityName, const std::string& destinationWaypointEntityName)
{
	if (scene == nullptr)
		return -1.0f;
	float length = 0.0f;
	if (!PlanOnLaneGraph(GetLaneGraph(scene), startWaypointEntityName, destinationWaypointEntityName, nullptr, &length))
		return -1.0f;
	return length;
}

void TrafficVehicleComponent::GatherLaneNodes(HexEngine::Scene* scene, std::vector<LaneNodeInfo>& out)
{
	out.clear();
	if (scene == nullptr)
		return;
	const auto& graph = GetLaneGraph(scene);
	out.reserve(graph.size());
	for (const auto& [name, node] : graph)
	{
		LaneNodeInfo info;
		info.name = name;
		info.position = node.position;
		info.inCount = node.inCount;
		info.outCount = (int32_t)node.neighbours.size();
		info.next = node.neighbours;
		if (!node.neighbours.empty())
		{
			auto it = graph.find(node.neighbours.front());
			if (it != graph.end())
				info.nextPosition = it->second.position;
		}
		out.push_back(std::move(info));
	}
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

	auto* scene = GetEntity() != nullptr ? GetEntity()->GetScene() : nullptr;

	// Reuse the cached chain if it's still valid. Without this the round-
	// robin branch cursor gets re-rolled EVERY FRAME (each TryResolveNextLane
	// call advances it), so a lane with multiple next-lanes would flicker its
	// target between branches - and the corner fillet needs the chain names
	// anyway to find the lane component at the target node.
	if (scene != nullptr && _chainedLaneNames.size() >= 2 && _chainedLaneNames.front() == _laneEntityName)
	{
		outPoints.clear();
		bool valid = true;
		for (const auto& name : _chainedLaneNames)
		{
			auto* chainEntity = scene->GetEntityByName(name);
			if (chainEntity == nullptr || chainEntity->IsPendingDeletion())
			{
				valid = false;
				break;
			}
			outPoints.push_back(chainEntity->GetWorldTM().Translation());
		}
		if (valid)
			return true;
	}
	_chainedLaneNames.clear();

	lane->GatherLanePoints(outPoints);
	if (outPoints.size() >= 2)
		return true;

	// Lane graph can be authored as explicit next-node links.
	// Build a deterministic forward chain to provide at least 2 points for non-route driving mode.
	if (scene == nullptr || outPoints.empty())
		return false;

	_chainedLaneNames.push_back(_laneEntityName);

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
		_chainedLaneNames.push_back(nextName);
		if (outPoints.size() >= 2)
			return true;

		cursor = nextLane;
	}

	if (outPoints.size() >= 2)
		return true;

	_chainedLaneNames.clear();
	return false;
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

	// If a corner fillet already picked the branch, take that SAME branch -
	// re-rolling the round-robin here would send the vehicle down a different
	// lane than the curve it just drove was aiming at.
	if (!_pendingNextLaneName.empty())
	{
		auto* pendingEntity = scene->GetEntityByName(_pendingNextLaneName);
		if (pendingEntity != nullptr && !pendingEntity->IsPendingDeletion())
		{
			if (auto* pendingLane = pendingEntity->GetComponent<TrafficLaneComponent>())
			{
				nextLane = pendingLane;
				nextLaneName = _pendingNextLaneName;
			}
		}
		_pendingNextLaneName.clear();
	}

	if (nextLane == nullptr && !TryResolveNextLane(scene, currentLane, nextLaneName, nextLane))
		return false;

	std::vector<math::Vector3> points;
	nextLane->GatherLanePoints(points);
	if (points.empty())
		return false;

	const math::Vector3 vehiclePos = GetEntity()->GetPosition();
	const float startDistanceSq = (points.front() - vehiclePos).LengthSquared();
	const float endDistanceSq = (points.back() - vehiclePos).LengthSquared();

	_laneEntityName = nextLaneName;
	_chainedLaneNames.clear();
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

TrafficLaneComponent* TrafficVehicleComponent::ResolveLaneAt(size_t pointIndex)
{
	auto* scene = GetEntity() != nullptr ? GetEntity()->GetScene() : nullptr;
	if (scene == nullptr || pointIndex >= _chainedLaneNames.size())
		return nullptr;

	auto* laneEntity = scene->GetEntityByName(_chainedLaneNames[pointIndex]);
	if (laneEntity == nullptr || laneEntity->IsPendingDeletion())
		return nullptr;

	return laneEntity->GetComponent<TrafficLaneComponent>();
}

bool TrafficVehicleComponent::PeekNextLanePoint(TrafficLaneComponent* fromLane, math::Vector3& outPoint)
{
	auto* scene = GetEntity() != nullptr ? GetEntity()->GetScene() : nullptr;
	if (scene == nullptr)
		return false;

	if (!_pendingNextLaneName.empty())
	{
		auto* pendingEntity = scene->GetEntityByName(_pendingNextLaneName);
		if (pendingEntity != nullptr && !pendingEntity->IsPendingDeletion())
		{
			outPoint = pendingEntity->GetWorldTM().Translation();
			return true;
		}
		_pendingNextLaneName.clear();
	}

	if (fromLane == nullptr)
		return false;

	std::string nextName;
	TrafficLaneComponent* nextLane = nullptr;
	if (!TryResolveNextLane(scene, fromLane, nextName, nextLane))
		return false;

	// Remember the branch so the actual lane switch (after the corner
	// completes) exits onto the lane the fillet was built for.
	_pendingNextLaneName = nextName;
	outPoint = nextLane->GetEntity()->GetWorldTM().Translation();
	return true;
}

bool TrafficVehicleComponent::TryStartCorner(const std::vector<math::Vector3>& points, const math::Vector3& currentPosition, const math::Vector3& targetPoint, float distanceToTarget)
{
	auto* lane = ResolveLane();
	if (lane == nullptr)
		return false;

	// The radius belongs to the node we're TURNING AT (the target), not the
	// lane we're currently driving on - that's the node the level author sets
	// the radius on. Fall back to the current lane if the chain can't resolve.
	auto* targetLane = ResolveLaneAt(_targetIndex);
	const float radius = targetLane != nullptr ? targetLane->GetCornerRadius() : lane->GetCornerRadius();
	if (radius <= 0.1f || distanceToTarget > radius || distanceToTarget <= 0.05f)
		return false;

	// Where do we head after the target node?
	math::Vector3 nextPoint;
	bool haveNext = false;
	if (!_invertDirection && _targetIndex + 1 < points.size())
	{
		nextPoint = points[_targetIndex + 1];
		haveNext = true;
	}
	else if (_invertDirection && _targetIndex > 0)
	{
		nextPoint = points[_targetIndex - 1];
		haveNext = true;
	}
	else
	{
		// Terminal node - the direction after it comes from the lane sitting
		// AT the target node: ITS next-lane link says where the road goes on.
		// (Peeking the current lane would just return the target itself.)
		// Loop wrap-around is the fallback, matching AdvanceTargetIndex's
		// precedence: explicit next-lane links win over local looping.
		haveNext = PeekNextLanePoint(targetLane != nullptr ? targetLane : lane, nextPoint);
		if (!haveNext && lane->IsLooping() && points.size() > 2)
		{
			nextPoint = _invertDirection ? points[points.size() - 1] : points[0];
			haveNext = true;
		}
	}
	if (!haveNext)
		return false;

	math::Vector3 dirIn = targetPoint - currentPosition;
	math::Vector3 dirOut = nextPoint - targetPoint;
	const float lenOut = dirOut.Length();
	if (lenOut <= 0.001f)
		return false;
	dirIn.Normalize();
	dirOut *= (1.0f / lenOut);

	const float cosTurn = dirIn.Dot(dirOut);
	if (cosTurn > 0.99f)
		return false; // effectively straight - normal arrival handles it

	const float exitTrim = std::min(radius, lenOut * 0.45f);
	const math::Vector3 exitPoint = targetPoint + dirOut * exitTrim;

	const float turnAlpha = std::clamp((1.0f - cosTurn) * 0.5f, 0.0f, 1.0f);
	const int32_t samples = 4 + static_cast<int32_t>(turnAlpha * 8.0f);

	_cornerPoints.clear();
	LanePathSmoothing::AppendBezierSamples(currentPosition, targetPoint, exitPoint, samples, _cornerPoints);
	_cornerIndex = 0;
	// Sharper corners slow the vehicle more (down to ~35% of lane speed for
	// a U-turn); gentle bends barely slow at all.
	_cornerSpeedScale = std::clamp(1.0f - turnAlpha * 0.65f, 0.35f, 1.0f);
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

float TrafficVehicleComponent::ComputeAvoidanceSpeed(const math::Vector3& currentPosition, const math::Vector3& moveDirection, float maxSpeed)
{
	_debugAvoidanceBlocked = false;
	_avoidanceBlockerQueued = false;

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
	bool bypassStillAhead = false;

	HexEngine::Entity* lastBlocker = nullptr;

	for (auto* other : nearby)
	{
		if (other == nullptr || other == this)
			continue;
		auto* otherEnt = other->GetEntity();
		if (otherEnt == nullptr || otherEnt->IsPendingDeletion())
			continue;

		const math::Vector3 otherPos = otherEnt->GetWorldTM().Translation();
		const math::Vector3 toOther = otherPos - currentPosition;

		// Vertical gate: a car on an overpass/underpass isn't an obstacle
		// even if it sits directly over our path.
		if (std::abs(toOther.y) > 3.0f)
			continue;

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

		// Still passing the car we broke a gridlock against: keep ignoring it.
		if (other == _gridlockBypass)
		{
			bypassStillAhead = true;
			continue;
		}

		// --- Direction-aware filtering ---
		// Lanes are one-way, so genuine same-lane traffic always travels
		// roughly WITH us. Without these checks, oncoming cars in the
		// opposite lane (and junction cross-traffic) that graze the narrow
		// corridor read as obstacles: both vehicles brake at each other and
		// deadlock with nothing actually blocking either path.
		math::Vector3 otherHeading = other->_lastMoveDirection;
		if (otherHeading.LengthSquared() < 0.001f)
			otherHeading = math::Vector3::TransformNormal(math::Vector3(0.0f, 0.0f, 1.0f), otherEnt->GetWorldTM());
		if (otherHeading.LengthSquared() > 0.001f)
			otherHeading.Normalize();

		const float headingDot = otherHeading.Dot(moveDirection);
		const bool otherMoving = other->_currentSpeed > 0.5f;

		// Moving oncoming traffic passes us in its own lane - never brake
		// to a stop for it.
		if (otherMoving && headingDot < -0.35f)
			continue;

		if (!otherMoving && other->_blockedTime > 1.0f && headingDot < 0.5f)
		{
			// The other car is stopped, stuck waiting itself, and NOT simply
			// queued ahead of us in our own lane (same-lane queues have
			// headingDot ~1 and must still be honoured). If it's pointing
			// toward us this is a mutual standoff - break it
			// deterministically: the lower-pointer vehicle proceeds.
			const float dist = std::max(std::sqrt(distSq), 0.01f);
			const float facingUs = -otherHeading.Dot(toOther) / dist;
			if (facingUs > 0.2f && std::less<const TrafficVehicleComponent*>()(this, other))
				continue;
		}

		// Gridlock breaker. Junction cross-traffic forms rings - each car held
		// by the next, perpendicular one - that the head-on rule above never
		// matches (nobody faces anybody), so whole junctions froze for minutes
		// and the drivers bailed out on foot. When we have waited a while and
		// the blocker is stopped and waiting too, and it is NOT simply queued
		// ahead of us in our own direction, exactly one car of the pair
		// proceeds (pointer order). Around any ring at least one link
		// qualifies, so the ring unwinds; same-direction queues are honoured.
		constexpr float kGridlockPatienceSeconds = 2.0f;
		if (!otherMoving && headingDot < 0.7f &&
			_waitTime > kGridlockPatienceSeconds && other->_waitTime > kGridlockPatienceSeconds &&
			std::less<const TrafficVehicleComponent*>()(this, other))
		{
			// Remember it: once we move our wait resets, and without this we
			// would brake for the same car again next frame and stop-start
			// through the junction in 2 s steps.
			_gridlockBypass = other;
			bypassStillAhead = true;
			continue;
		}

		if (projectedDistance < nearestAheadDistance)
		{
			nearestAheadDistance = projectedDistance;
			_debugBlockerPos = otherPos;
			lastBlocker = otherEnt;
			_avoidanceBlockerQueued = other->_queuedAtSignal;
		}
	}

	if (!bypassStillAhead)
		_gridlockBypass = nullptr;

	// The player's bike / vehicle in our lane ahead (see SetPlayerObstacle).
	if (s_playerObstacleValid)
	{
		const math::Vector3 toPlayer = s_playerObstaclePos - currentPosition;
		const float projected = toPlayer.Dot(moveDirection);
		if (std::abs(toPlayer.y) <= 3.0f && projected > 0.0f && projected <= lookAhead)
		{
			const float lateralSq = std::max(0.0f, toPlayer.LengthSquared() - projected * projected);
			// Narrower than a car's corridor: a bike hugging the kerb or the
			// centre line beside us is being passed, not followed.
			const float half = laneHalfWidth * 0.75f;
			const float playerAlong = s_playerObstacleVel.Dot(moveDirection);
			if (lateralSq <= half * half && playerAlong < _currentSpeed + 0.5f)
			{
				// A bike is ~2 m shorter than a car: measure to its rear.
				const float gap = std::max(0.0f, projected - 1.0f);
				if (gap < nearestAheadDistance)
				{
					nearestAheadDistance = gap;
					_debugBlockerPos = s_playerObstaclePos;
					_avoidanceBlockerQueued = false;
				}
			}
		}
	}

	if (nearestAheadDistance == std::numeric_limits<float>::max())
		return maxSpeed;
	if (nearestAheadDistance <= followDistance)
	{
		_debugAvoidanceBlocked = true;
		return 0.0f;
	}

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

	if (_parked)
	{
		StopEngineSound();
		return;
	}

	if (frameTime <= 0.0f)
		return;

	auto* transform = GetEntity()->GetComponent<HexEngine::Transform>();
	if (transform == nullptr)
		return;

	// Held (a bus dwelling at a stop): stand still with the engine idling but
	// stay a solid obstacle for the traffic behind, unlike parked.
	if (_held)
	{
		_currentSpeed = 0.0f;
		_waitTime = 0.0f;
		UpdateAudio(transform->GetPosition(), frameTime);
		return;
	}

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

	// --- Corner fillet driving (lane-graph mode only; planned waypoint
	// routes are pre-smoothed in RestartPath). Entering the lane's corner
	// radius spawns a bezier micro-path through the node; while it's active
	// the vehicle chases its samples instead of the node itself.
	if (!_useWaypointRoute)
	{
		if (_cornerPoints.empty())
		{
			TryStartCorner(points, currentPosition, targetPoint, distanceToTarget);
		}

		if (!_cornerPoints.empty())
		{
			const float sampleArrival = std::max(arrivalDistance * 0.5f, 0.2f);
			while (_cornerIndex < _cornerPoints.size() && (_cornerPoints[_cornerIndex] - currentPosition).Length() <= sampleArrival)
				++_cornerIndex;

			if (_cornerIndex >= _cornerPoints.size())
			{
				// Corner complete - we're already on the outgoing leg past
				// the node, so process the node arrival now.
				_cornerPoints.clear();
				_cornerIndex = 0;
				_cornerSpeedScale = 1.0f;

				const bool reachedRouteEnd = AdvanceTargetIndex(points.size());
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
			}
			else
			{
				targetPoint = _cornerPoints[_cornerIndex];
			}

			toTarget = targetPoint - currentPosition;
			distanceToTarget = toTarget.Length();
		}
	}

	if (_cornerPoints.empty() && distanceToTarget <= arrivalDistance)
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
	_lastMoveDirection = toTarget;

	auto* lane = ResolveLane();
	const float laneSpeed = lane != nullptr ? lane->GetSpeedLimit() : _speed;
	float maxSpeed = std::max(_useLaneSpeedLimit ? laneSpeed : _speed, 0.0f);
	// Cache the achievable top speed so the audio system can map
	// pitch correctly. Using _speed as the denominator means cars on
	// slow lanes never reach max pitch even at their actual cruise.
	// Taken before the corner slow-down so turning doesn't jump the pitch.
	_audioReferenceMaxSpeed = std::max(maxSpeed, 0.01f);
	// Slow through active corner fillets - sharper turn, stronger slow-down.
	if (!_cornerPoints.empty())
		maxSpeed *= _cornerSpeedScale;
	// Pulling out of / into a parking bay or driveway: walking pace, backing out slower.
	const bool offLane = IsOffLane();
	const bool reversing = _useWaypointRoute && _targetIndex >= 1 && _targetIndex <= _reverseUntilIndex;
	if (offLane)
		maxSpeed = std::min(maxSpeed, reversing ? _offLaneSpeed * 0.6f : _offLaneSpeed);
	if (maxSpeed <= 0.0f)
	{
		_currentSpeed = 0.0f;
		return;
	}

	float desiredSpeed = ComputeAvoidanceSpeed(currentPosition, toTarget, maxSpeed);

	// A waypoint route that ends in a stop (parking, a bus stop) brakes
	// smoothly to the end point instead of halting dead on arrival:
	// v <= sqrt(2 * a * remaining).
	if (_useWaypointRoute && !_despawnAtRouteEnd && _stopDeceleration > 0.0f && !points.empty())
	{
		float remaining = distanceToTarget;
		for (size_t i = _targetIndex; i + 1 < points.size(); ++i)
		{
			remaining += math::Vector3::Distance(points[i], points[i + 1]);
			if (remaining > 200.0f)
				break;
		}
		const float stopCap = std::sqrt(2.0f * _stopDeceleration * std::max(0.0f, remaining - arrivalDistance)) + 0.6f;
		desiredSpeed = std::min(desiredSpeed, stopCap);
	}

	const float accel = std::max(_acceleration, 0.0f);
	const float brake = std::max(_brakingStrength, 0.0f);

	// Traffic signals: stop at the line on red (and on amber when we can). Not on an
	// off-lane leg: a car park aisle near a junction isn't behind its stop line.
	_queuedAtSignal = false;
	if (!offLane)
	{
		const bool avoidanceBinds = desiredSpeed < maxSpeed - 0.01f;
		bool heldBySignal = false;
		const float signalCap = TrafficSignalControllerComponent::ComputeVehicleSpeedCap(
			currentPosition, toTarget, _currentSpeed, maxSpeed, brake, heldBySignal);
		const bool signalBinds = heldBySignal && signalCap <= desiredSpeed + 0.01f;
		desiredSpeed = std::min(desiredSpeed, signalCap);
		_queuedAtSignal = signalBinds || (avoidanceBinds && _avoidanceBlockerQueued);
	}

	if (_currentSpeed < desiredSpeed)
	{
		_currentSpeed = std::min(_currentSpeed + accel * frameTime, desiredSpeed);
	}
	else
	{
		_currentSpeed = std::max(_currentSpeed - brake * frameTime, desiredSpeed);
	}

	// Never step past the current target. On a long frame (hitch, load spike)
	// speed * dt overshot the waypoint / corner sample; next frame the target
	// was BEHIND the car, toTarget flipped and the heading slerped round towards
	// it - the car spun for a moment (Trello #48). The node is consumed next frame.
	currentPosition += toTarget * std::min(_currentSpeed * frameTime, distanceToTarget);
	transform->SetPositionNoNotify(currentPosition);

	if (_speed > 0.1f && _currentSpeed <= 0.5f && !_queuedAtSignal)
		_waitTime += frameTime;
	else
		_waitTime = 0.0f;

	// Backing out of a bay: the car keeps facing away from where it is going.
	const math::Vector3 noseDirection = reversing ? -toTarget : toTarget;
	const float yaw = atan2f(noseDirection.x, noseDirection.z);
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
	file->Serialize(data, "_honkChance", _honkChance);
	file->Serialize(data, "_parked", _parked);
	file->Serialize(data, "_honkVolume", _honkVolume);
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
	file->Deserialize(data, "_honkChance", _honkChance);
	file->Deserialize(data, "_parked", _parked);
	file->Deserialize(data, "_honkVolume", _honkVolume);
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
	auto* honkCdMin       = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Honk Cooldown Min (s)",   &_honkCooldownMin, 0.5f, 120.0f, 0.1f, 2);
	auto* honkCdMax       = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Honk Cooldown Max (s)",   &_honkCooldownMax, 0.5f, 120.0f, 0.1f, 2);
	auto* honkChance      = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Honk Chance (drivers)",   &_honkChance, 0.0f, 1.0f, 0.01f, 2);
	auto* honkVolume      = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Honk Volume",             &_honkVolume, 0.0f, 1.0f, 0.01f, 2);
	honkChance->SetPrefabOverrideBinding(GetComponentName(), "/_honkChance");
	honkVolume->SetPrefabOverrideBinding(GetComponentName(), "/_honkVolume");

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

	// Active corner fillet path (cyan).
	if (_cornerIndex < _cornerPoints.size())
	{
		const math::Color cornerColour = math::Color(HEX_RGB_TO_FLOAT3(26, 188, 156), 1.0f);
		HexEngine::g_pEnv->_debugRenderer->DrawLine(from, _cornerPoints[_cornerIndex], cornerColour);
		for (size_t i = _cornerIndex; i + 1 < _cornerPoints.size(); ++i)
			HexEngine::g_pEnv->_debugRenderer->DrawLine(_cornerPoints[i], _cornerPoints[i + 1], cornerColour);
	}

	// Avoidance block (red): line + box to the exact entity we're braking
	// for. If a car is "stopped for no reason", this points at the reason.
	if (_debugAvoidanceBlocked)
	{
		const math::Color blockColour = math::Color(HEX_RGB_TO_FLOAT3(231, 76, 60), 1.0f);
		HexEngine::g_pEnv->_debugRenderer->DrawLine(from + math::Vector3(0.0f, 1.0f, 0.0f), _debugBlockerPos + math::Vector3(0.0f, 1.0f, 0.0f), blockColour);
		DrawPointMarker(_debugBlockerPos, 1.2f, blockColour);
	}

	if (_useWaypointRoute && points.size() > 1)
	{
		for (size_t i = 0; i + 1 < points.size(); ++i)
		{
			HexEngine::g_pEnv->_debugRenderer->DrawLine(points[i], points[i + 1], math::Color(HEX_RGBA_TO_FLOAT4(52, 152, 219, 90)));
		}
	}
}