#include "TransitVehicleComponent.hpp"
#include "TransitLineComponent.hpp"
#include "TransitStopComponent.hpp"
#include "TrafficVehicleComponent.hpp"
#include "RoutineAgentComponent.hpp"
#include <HexEngine.Core/Entity/Entity.hpp>
#include <HexEngine.Core/Entity/Component/Transform.hpp>
#include <HexEngine.Core/Entity/Component/Camera.hpp>
#include <HexEngine.Core/Entity/Component/StaticMeshComponent.hpp>
#include <HexEngine.Core/Entity/Component/FirstPersonCameraController.hpp>
#include <HexEngine.Core/Entity/Component/NavigationComponent.hpp>
#include <HexEngine.Core/Entity/Component/RigidBody.hpp>
#include <HexEngine.Core/Physics/IPhysicsSystem.hpp>
#include <HexEngine.Core/Physics/IRigidBody.hpp>
#include <HexEngine.Core/Graphics/DebugRenderer.hpp>
#include <HexEngine.Core/GUI/Elements/ComponentWidget.hpp>
#include <HexEngine.Core/GUI/Elements/DragFloat.hpp>
#include <HexEngine.Core/GUI/Elements/DragInt.hpp>
#include <HexEngine.Core/Scene/Scene.hpp>
#include <HexEngine.Core/Environment/TimeManager.hpp>
#include <HexEngine.Core/Environment/TransitPlayerState.hpp>
#include <HexEngine.Core/Audio/AudioManager.hpp>
#include <HexEngine.Core/Audio/SoundEffect.hpp>
#include <HexEngine.Core/Math/FloatMath.hpp>
#include <algorithm>
#include <cmath>

namespace
{
	constexpr float kDegToRad = 0.0174532925f;
	constexpr float kMaxBoardWaitSeconds = 14.0f;	// a boarder still not at the door: put them aboard
	constexpr float kMaxDwellExtension = 20.0f;
	constexpr float kDoorwayHoldSeconds = 10.0f;

	float YawOf(const math::Matrix& tm)
	{
		const math::Vector3 fwd(tm._31, tm._32, tm._33);
		return std::atan2(fwd.x, fwd.z);
	}

	// A riding NPC moves with the vehicle every frame; its static (triangle-mesh)
	// collider must not shove the player around the cabin. While aboard the
	// body is parked far below the world and the NPC is moved by its transform
	// only (Transform::SetPosition leaves static bodies alone - ForcePosition
	// would drag the body along); getting off ForcePositions it back.
	// (Not SetIsTrigger: PhysX can't make triangle meshes triggers and its
	// error dialog froze the editor.)
	void SetRiderCollision(HexEngine::Entity* e, bool solid)
	{
		auto* rb = e != nullptr ? e->GetComponent<HexEngine::RigidBody>() : nullptr;
		auto* body = rb != nullptr ? rb->GetIRigidBody() : nullptr;
		if (body == nullptr || body->IsCharacterController())
			return;
		const math::Vector3 p = e->GetWorldTM().Translation();
		body->UpdatePosePosition(solid ? p : p - math::Vector3(0.0f, 1000.0f, 0.0f));
	}

	uint32_t HashName(const std::string& s)
	{
		uint32_t h = 2166136261u;
		for (char c : s)
			h = (h ^ (uint8_t)c) * 16777619u;
		return h;
	}
}

TransitVehicleComponent::TransitVehicleComponent(HexEngine::Entity* entity) :
	UpdateComponent(entity)
{
}

TransitVehicleComponent::TransitVehicleComponent(HexEngine::Entity* entity, TransitVehicleComponent* copy) :
	UpdateComponent(entity)
{
	if (copy != nullptr)
	{
		_cabinBoxes = copy->_cabinBoxes;
		_doorBoxes = copy->_doorBoxes;
		_doorExits = copy->_doorExits;
		_standSpots = copy->_standSpots;
		_doorSlides = copy->_doorSlides;
		_doorMeshPrefix = copy->_doorMeshPrefix;
		_doorPartSlides = copy->_doorPartSlides;
		_capacity = copy->_capacity;
		_doorOpenSeconds = copy->_doorOpenSeconds;
		_yawOffsetDegrees = copy->_yawOffsetDegrees;
		_trackHeightOffset = copy->_trackHeightOffset;
		_halfLength = copy->_halfLength;
	}
}

TransitVehicleComponent::~TransitVehicleComponent()
{
	DestroyBodies();
}

void TransitVehicleComponent::Destroy()
{
	// Riders must not be left hanging in mid-air where the vehicle vanished.
	auto* scene = GetEntity() != nullptr ? GetEntity()->GetScene() : nullptr;
	if (scene != nullptr)
	{
		for (const auto& p : _passengers)
		{
			auto* e = scene->GetEntityByName(p.agentName);
			SetRiderCollision(e, true);
			if (auto* agent = e != nullptr ? e->GetComponent<RoutineAgentComponent>() : nullptr)
				agent->SetTransitPhase(RoutineAgentComponent::TransitPhase::Alighted);
		}
	}
	_passengers.clear();
	DestroyBodies();
	UpdateComponent::Destroy();
}

bool TransitVehicleComponent::ParseBox(const std::vector<float>& v, size_t i, math::Vector3& mn, math::Vector3& mx)
{
	if ((i + 1) * 6 > v.size())
		return false;
	mn = math::Vector3(v[i * 6 + 0], v[i * 6 + 1], v[i * 6 + 2]);
	mx = math::Vector3(v[i * 6 + 3], v[i * 6 + 4], v[i * 6 + 5]);
	return true;
}

TransitLineComponent* TransitVehicleComponent::GetLine() const
{
	return TransitLineComponent::FindLine(GetEntity()->GetScene(), _lineEntityName);
}

TransitVehicleComponent* TransitVehicleComponent::GetLead() const
{
	if (_leadEntityName.empty())
		return const_cast<TransitVehicleComponent*>(this);
	auto* e = GetEntity()->GetScene()->GetEntityByName(_leadEntityName);
	return e != nullptr ? e->GetComponent<TransitVehicleComponent>() : nullptr;
}

TransitVehicleComponent* TransitVehicleComponent::FindPlayerVehicle(HexEngine::Scene* scene)
{
	if (scene == nullptr || scene->GetMainCamera() == nullptr || scene->GetMainCamera()->GetEntity() == nullptr)
		return nullptr;
	auto* fpcc = scene->GetMainCamera()->GetEntity()->GetComponent<HexEngine::FirstPersonCameraController>();
	auto* carrier = fpcc != nullptr ? fpcc->GetCarrier() : nullptr;
	auto* vehicle = carrier != nullptr ? carrier->GetComponent<TransitVehicleComponent>() : nullptr;
	return vehicle != nullptr ? vehicle->GetLead() : nullptr;
}

void TransitVehicleComponent::AssignToLine(const std::string& lineEntityName, int32_t stopIndex, int32_t direction, int32_t ordinal)
{
	_lineEntityName = lineEntityName;
	_leadEntityName.clear();
	_stopIndex = stopIndex;
	_nextStopIndex = stopIndex;
	_direction = direction == 0 ? 1 : direction;
	_ordinal = ordinal;
	_passengers.clear();
	_pendingBoarders.clear();

	auto* line = GetLine();
	if (line == nullptr)
		return;

	if (line->GetMode() == TransitLineComponent::Mode::Train)
	{
		line->EnsureTrack();
		_trackDistance = line->GetStopTrackDistance(stopIndex);
		_speed = 0.0f;
		PlaceOnTrack(_trackDistance);
	}
	else
	{
		// Pull up at the stop's lane node, facing the next stop.
		auto* scene = GetEntity()->GetScene();
		_currentNodeName = line->GetStopLaneWaypoint(stopIndex);
		auto* node = scene->GetEntityByName(_currentNodeName);
		int32_t dir = _direction;
		auto* nextNode = scene->GetEntityByName(line->GetStopLaneWaypoint(line->NextStop(stopIndex, dir)));
		if (node != nullptr)
		{
			const math::Vector3 p = node->GetWorldTM().Translation();
			GetEntity()->ForcePosition(p);
			if (nextNode != nullptr)
			{
				const math::Vector3 d = nextNode->GetWorldTM().Translation() - p;
				if (d.x * d.x + d.z * d.z > 1e-4f)
					GetEntity()->ForceRotation(math::Quaternion::CreateFromYawPitchRoll(std::atan2(d.x, d.z), 0.0f, 0.0f));
			}
		}
		if (auto* traffic = GetEntity()->GetComponent<TrafficVehicleComponent>())
		{
			traffic->SetParked(false);
			traffic->SetHeld(true);
			traffic->SetUseWaypointRoute(true);
			traffic->SetDespawnAtRouteEnd(false);
		}
	}

	if (auto* stop = line->GetStop(stopIndex))
	{
		const math::Vector3 local = math::Vector3::Transform(stop->GetEntity()->GetWorldTM().Translation(), GetEntity()->GetWorldTM().Invert());
		_openSide = local.x >= 0.0f ? 1 : -1;
	}

	// Start mid-dwell so the fleet doesn't leave every stop in the same frame.
	_state = State::Dwelling;
	_arrivalProcessed = false;
	_stateTime = line->GetDwellSeconds() * 0.5f * (float)(ordinal % 3) / 3.0f;
	_doorOpen = 1.0f;
	_doorTarget = 1.0f;
}

void TransitVehicleComponent::AssignAsCarriage(const std::string& leadEntityName, int32_t index, float spacing)
{
	_leadEntityName = leadEntityName;
	_carriageIndex = std::max(1, index);
	_carriageSpacing = spacing;
	_lineEntityName.clear();
	_state = State::Idle;
}

void TransitVehicleComponent::EnsureBodies()
{
	if (_bodiesBuilt || HexEngine::g_pEnv == nullptr || HexEngine::g_pEnv->_physicsSystem == nullptr)
		return;
	_bodiesBuilt = true;
	auto* tf = GetEntity()->GetComponent<HexEngine::Transform>();
	if (tf == nullptr)
		return;
	const math::Vector3 scale = tf->GetScale();

	auto build = [&](const std::vector<float>& boxes) -> HexEngine::IRigidBody*
	{
		if (boxes.size() < 6)
			return nullptr;
		auto* body = HexEngine::g_pEnv->_physicsSystem->CreateRigidBody(tf, nullptr, HexEngine::IRigidBody::BodyType::Kinematic);
		if (body == nullptr)
			return nullptr;
		body->SetPoseWritebackEnabled(false);
		math::Vector3 mn, mx;
		for (size_t i = 0; ParseBox(boxes, i, mn, mx); ++i)
		{
			mn *= scale;
			mx *= scale;
			const math::Vector3 c = (mn + mx) * 0.5f;
			const math::Vector3 e = (mx - mn) * 0.5f;
			body->AddBoxCollider(dx::BoundingBox(c, math::Vector3(std::fabs(e.x), std::fabs(e.y), std::fabs(e.z))), true);
		}
		return body;
	};
	_cabinBody = build(_cabinBoxes);
	// Doors per side, so only the platform side opens (a train has doors on both).
	std::vector<float> neg, pos;
	{
		math::Vector3 mn, mx;
		for (size_t i = 0; ParseBox(_doorBoxes, i, mn, mx); ++i)
		{
			auto& dst = (mn.x + mx.x) >= 0.0f ? pos : neg;
			for (int32_t k = 0; k < 6; ++k)
				dst.push_back(_doorBoxes[i * 6 + k]);
		}
	}
	_doorBody = build(neg);
	_doorBodyPos = build(pos);
}

void TransitVehicleComponent::DestroyBodies()
{
	if (HexEngine::g_pEnv != nullptr && HexEngine::g_pEnv->_physicsSystem != nullptr)
	{
		if (_cabinBody != nullptr)
			HexEngine::g_pEnv->_physicsSystem->DestroyRigidBody(_cabinBody);
		if (_doorBody != nullptr)
			HexEngine::g_pEnv->_physicsSystem->DestroyRigidBody(_doorBody);
		if (_doorBodyPos != nullptr)
			HexEngine::g_pEnv->_physicsSystem->DestroyRigidBody(_doorBodyPos);
	}
	_cabinBody = nullptr;
	_doorBody = nullptr;
	_doorBodyPos = nullptr;
	_bodiesBuilt = false;
}

void TransitVehicleComponent::CollectDoorMeshes()
{
	if (_doorMeshesCollected)
		return;
	if (!_doorPartSlides.empty())
	{
		struct Rule { std::string prefix; math::Vector3 slide; };
		std::vector<Rule> rules;
		for (const auto& entry : _doorPartSlides)
		{
			const size_t eq = entry.find('=');
			if (eq == std::string::npos)
				continue;
			Rule r;
			r.prefix = entry.substr(0, eq);
			if (sscanf_s(entry.c_str() + eq + 1, "%f,%f,%f", &r.slide.x, &r.slide.y, &r.slide.z) == 3)
				rules.push_back(r);
		}
		// Longest prefix first so "..._cube005_0" can be told from "..._cube005".
		std::sort(rules.begin(), rules.end(), [](const Rule& a, const Rule& b) { return a.prefix.size() > b.prefix.size(); });
		for (auto* child : GetEntity()->GetChildren())
		{
			if (child == nullptr || child->IsPendingDeletion())
				continue;
			for (const auto& r : rules)
			{
				if (child->GetName().rfind(r.prefix, 0) == 0)
				{
					DoorMesh d;
					d.entityName = child->GetName();
					d.closedLocal = child->GetPosition();
					d.slide = r.slide;
					_doorMeshes.push_back(d);
					break;
				}
			}
		}
		_doorMeshesCollected = !_doorMeshes.empty();
		return;
	}
	std::vector<HexEngine::Entity*> doors;
	for (auto* child : GetEntity()->GetChildren())
		if (child != nullptr && !child->IsPendingDeletion() && !_doorMeshPrefix.empty() && child->GetName().rfind(_doorMeshPrefix, 0) == 0)
			doors.push_back(child);
	if (doors.empty())
		return;	// generated children may not exist yet - retry next frame
	std::sort(doors.begin(), doors.end(), [](HexEngine::Entity* a, HexEngine::Entity* b) { return a->GetName() < b->GetName(); });
	for (size_t i = 0; i < doors.size(); ++i)
	{
		DoorMesh d;
		d.entityName = doors[i]->GetName();
		d.closedLocal = doors[i]->GetPosition();
		if ((i + 1) * 3 <= _doorSlides.size())
			d.slide = math::Vector3(_doorSlides[i * 3], _doorSlides[i * 3 + 1], _doorSlides[i * 3 + 2]);
		_doorMeshes.push_back(d);
	}
	_doorMeshesCollected = true;
}

void TransitVehicleComponent::ApplyDoorMeshes()
{
	CollectDoorMeshes();
	auto* scene = GetEntity()->GetScene();
	const float t = _doorOpen * _doorOpen * (3.0f - 2.0f * _doorOpen);
	for (const auto& d : _doorMeshes)
	{
		const int32_t side = d.slide.x > 0.0f ? 1 : (d.slide.x < 0.0f ? -1 : 0);
		const float k = (side == 0 || _openSide == 0 || side == _openSide) ? t : 0.0f;
		if (auto* e = scene->GetEntityByName(d.entityName))
			e->SetPosition(d.closedLocal + d.slide * k);
	}
}

void TransitVehicleComponent::PlaceOnTrack(float distance)
{
	auto* lead = GetLead();
	auto* line = lead != nullptr ? lead->GetLine() : nullptr;
	if (line == nullptr || !line->EnsureTrack())
		return;
	const math::Vector3 centre = line->SampleTrack(distance);
	const math::Vector3 front = line->SampleTrack(distance + _halfLength);
	const math::Vector3 back = line->SampleTrack(distance - _halfLength);
	math::Vector3 dir = front - back;
	const float yaw = (dir.x * dir.x + dir.z * dir.z > 1e-6f) ? std::atan2(dir.x, dir.z) : 0.0f;
	const float horiz = std::sqrt(dir.x * dir.x + dir.z * dir.z);
	const float pitch = horiz > 1e-3f ? -std::atan2(dir.y, horiz) : 0.0f;
	GetEntity()->ForcePosition(centre + math::Vector3(0.0f, _trackHeightOffset, 0.0f));
	GetEntity()->ForceRotation(math::Quaternion::CreateFromYawPitchRoll(yaw + _yawOffsetDegrees * kDegToRad, pitch, 0.0f));
}

void TransitVehicleComponent::BeginLeg()
{
	auto* line = GetLine();
	if (line == nullptr)
		return;
	int32_t dir = _direction;
	_nextStopIndex = line->NextStop(_stopIndex, dir);
	_direction = dir;
	_state = State::Driving;
	_stateTime = 0.0f;
	_stuckTime = 0.0f;
	_arrivalProcessed = false;

	if (line->GetMode() == TransitLineComponent::Mode::Bus)
	{
		auto* traffic = GetEntity()->GetComponent<TrafficVehicleComponent>();
		const std::string from = !_currentNodeName.empty() ? _currentNodeName : line->GetStopLaneWaypoint(_stopIndex);
		if (traffic == nullptr || from.empty())
		{
			LOG_WARN("TransitVehicle '%s': can't route from stop %d (no lane node)", GetEntity()->GetName().c_str(), _stopIndex);
			return;
		}
		traffic->SetHeld(false);
		traffic->SetParked(false);
		traffic->SetUseWaypointRoute(true);
		traffic->SetDespawnAtRouteEnd(false);
		// The next stop may have a node on each lane chain passing it: take the
		// one with the shortest route from here.
		std::string best;
		float bestLength = 1e30f;
		for (const auto& candidate : line->GetStopLaneWaypointCandidates(_nextStopIndex))
		{
			traffic->SetWaypointRouteEndpoints(from, candidate);
			traffic->RestartPath();
			const float length = traffic->GetPlannedRouteLength();
			if (length > 0.0f && length < bestLength)
			{
				bestLength = length;
				best = candidate;
			}
		}
		if (best.empty())
		{
			LOG_WARN("TransitVehicle '%s': no lane route from '%s' to stop %d", GetEntity()->GetName().c_str(), from.c_str(), _nextStopIndex);
			_state = State::Idle;
			return;
		}
		_targetNodeName = best;
		traffic->SetWaypointRouteEndpoints(from, best);
		traffic->RestartPath();
	}
}

void TransitVehicleComponent::ArriveAtStop()
{
	_reopenCount = 0;
	if (auto* line = GetLine())
	{
		if (auto* stop = line->GetStop(_nextStopIndex))
		{
			const math::Vector3 local = math::Vector3::Transform(stop->GetEntity()->GetWorldTM().Translation(), GetEntity()->GetWorldTM().Invert());
			_openSide = local.x >= 0.0f ? 1 : -1;
		}
	}
	_stopIndex = _nextStopIndex;
	if (!_targetNodeName.empty())
		_currentNodeName = _targetNodeName;
	_state = State::Dwelling;
	_stateTime = 0.0f;
	_arrivalProcessed = false;
	_speed = 0.0f;
	if (_playerAboard)
		PlayVehicleSound(_stopChimePath, false);
	if (auto* traffic = GetEntity()->GetComponent<TrafficVehicleComponent>())
		traffic->SetHeld(true);
}

void TransitVehicleComponent::UpdateTrainMotion(float dt)
{
	auto* line = GetLine();
	if (line == nullptr || !line->EnsureTrack())
		return;
	const float target = line->GetStopTrackDistance(_nextStopIndex);
	const float length = line->GetTrackLength();
	float remaining;
	if (line->IsClosedLoop())
	{
		remaining = _direction >= 0 ? target - _trackDistance : _trackDistance - target;
		remaining = std::fmod(std::fmod(remaining, length) + length, length);
	}
	else
	{
		remaining = (target - _trackDistance) * (float)_direction;
	}

	if (remaining <= 0.05f && _speed < 0.5f)
	{
		_trackDistance = target;
		PlaceOnTrack(_trackDistance);
		ArriveAtStop();
		return;
	}

	const float cruise = line->GetTrainSpeed();
	const float decel = std::max(0.1f, line->GetTrainDeceleration());
	const float accel = std::max(0.1f, line->GetTrainAcceleration());
	const float brakeCap = std::sqrt(2.0f * decel * std::max(0.0f, remaining)) + 0.15f;
	const float wanted = std::min(cruise, brakeCap);
	if (_speed < wanted)
		_speed = std::min(wanted, _speed + accel * dt);
	else
		_speed = std::max(wanted, _speed - decel * 1.5f * dt);
	const float step = std::min(remaining, _speed * dt);
	_trackDistance += step * (float)_direction;
	if (line->IsClosedLoop())
		_trackDistance = std::fmod(std::fmod(_trackDistance, length) + length, length);
	PlaceOnTrack(_trackDistance);
}

void TransitVehicleComponent::ProcessStopArrival()
{
	auto* line = GetLine();
	auto* stop = line != nullptr ? line->GetStop(_stopIndex) : nullptr;
	auto* scene = GetEntity()->GetScene();
	if (stop == nullptr || scene == nullptr)
		return;
	const std::string& stopName = stop->GetEntity()->GetName();

	// Off first.
	for (size_t i = 0; i < _passengers.size();)
	{
		if (_passengers[i].alightStopName == stopName)
		{
			auto* e = scene->GetEntityByName(_passengers[i].agentName);
			auto* agent = e != nullptr ? e->GetComponent<RoutineAgentComponent>() : nullptr;
			_passengers.erase(_passengers.begin() + i);
			if (agent != nullptr)
			{
				// A platform the navmesh can't reach (underground / elevated, with
				// the waiting area on the street): riders come out at the waiting area.
				math::Vector3 exit = GetDoorExitWorld(stop->GetWaitCentre());
				if (std::fabs(exit.y - stop->GetWaitCentre().y) > 3.0f)
					exit = stop->GetWaitSpot((uint32_t)i * 7u + 3u);
				SetRiderCollision(e, true);
				LOG_INFO("TransitVehicle '%s': '%s' gets off at %s", GetEntity()->GetName().c_str(), e->GetName().c_str(), stopName.c_str());
				e->ForcePosition(exit);
				agent->SetTransitPhase(RoutineAgentComponent::TransitPhase::Alighted);
				agent->SetTransitVehicleName(std::string());
			}
			continue;
		}
		++i;
	}

	// Then on: people waiting here for this line, going somewhere ahead.
	int32_t dir = _direction;
	const int32_t afterIndex = _stopIndex;
	const int32_t room = std::max(0, _capacity - (int32_t)_passengers.size() - (int32_t)_pendingBoarders.size());
	auto boarders = stop->TakeBoarders(_lineEntityName, room, [&](const std::string& alightStop)
	{
		const int32_t to = line->FindStopIndex(alightStop);
		// Ping-pong lines reverse at the ends: at a terminus the next leg runs
		// the other way, so judge "ahead" by the direction we will leave in.
		int32_t leaveDir = dir;
		line->NextStop(afterIndex, leaveDir);
		return to >= 0 && line->IsAhead(afterIndex, leaveDir, to);
	});
	for (const auto& b : boarders)
	{
		auto* e = scene->GetEntityByName(b.agentName);
		auto* agent = e != nullptr ? e->GetComponent<RoutineAgentComponent>() : nullptr;
		if (agent == nullptr)
			continue;
		const math::Vector3 door = GetDoorExitWorld(e->GetWorldTM().Translation());
		if (std::fabs(door.y - stop->GetWaitCentre().y) > 3.0f)
		{
			// Waiting on the street above / below the platform: straight aboard.
			BoardAgent(agent, b.alightStopName);
			continue;
		}
		agent->SetTransitVehicleName(GetEntity()->GetName());
		agent->SetTransitBoardTarget(door, b.alightStopName);
		agent->SetTransitPhase(RoutineAgentComponent::TransitPhase::Boarding);
		_pendingBoarders.push_back(b.agentName);
	}
}

bool TransitVehicleComponent::BoardAgent(RoutineAgentComponent* agent, const std::string& alightStopName)
{
	if (agent == nullptr || agent->GetEntity() == nullptr)
		return false;
	const std::string& name = agent->GetEntity()->GetName();
	_pendingBoarders.erase(std::remove(_pendingBoarders.begin(), _pendingBoarders.end(), name), _pendingBoarders.end());
	for (const auto& p : _passengers)
		if (p.agentName == name)
			return true;

	// First free spot.
	const int32_t spotCount = std::max<int32_t>(1, (int32_t)(_standSpots.size() / 3));
	std::vector<bool> used((size_t)spotCount, false);
	for (const auto& p : _passengers)
		if (p.spot >= 0 && p.spot < spotCount)
			used[(size_t)p.spot] = true;
	int32_t spot = (int32_t)(_passengers.size() % (size_t)spotCount);
	for (int32_t i = 0; i < spotCount; ++i)
		if (!used[(size_t)i]) { spot = i; break; }

	Passenger p;
	p.agentName = name;
	p.alightStopName = alightStopName;
	p.spot = spot;
	// Mostly facing forward, some sideways, deterministic per person.
	const uint32_t h = HashName(name);
	p.yaw = ((float)(h % 5u) - 2.0f) * 0.45f;
	_passengers.push_back(p);

	if (auto* nav = agent->GetEntity()->GetComponent<HexEngine::NavigationComponent>())
		nav->ClearPath();
	SetRiderCollision(agent->GetEntity(), false);
	LOG_INFO("TransitVehicle '%s': '%s' boards (%d aboard)", GetEntity()->GetName().c_str(), name.c_str(), (int32_t)_passengers.size());
	agent->SetTransitVehicleName(GetEntity()->GetName());
	agent->SetTransitPhase(RoutineAgentComponent::TransitPhase::Riding);
	UpdatePassengers();
	return true;
}

void TransitVehicleComponent::AlightAgent(RoutineAgentComponent* agent)
{
	if (agent == nullptr || agent->GetEntity() == nullptr)
		return;
	const std::string& name = agent->GetEntity()->GetName();
	_pendingBoarders.erase(std::remove(_pendingBoarders.begin(), _pendingBoarders.end(), name), _pendingBoarders.end());
	const size_t before = _passengers.size();
	_passengers.erase(std::remove_if(_passengers.begin(), _passengers.end(), [&](const Passenger& p) { return p.agentName == name; }), _passengers.end());
	if (_passengers.size() != before)
	{
		SetRiderCollision(agent->GetEntity(), true);
		agent->GetEntity()->ForcePosition(GetDoorExitWorld(agent->GetEntity()->GetWorldTM().Translation()));
	}
	agent->SetTransitVehicleName(std::string());
	agent->SetTransitPhase(RoutineAgentComponent::TransitPhase::Alighted);
}

math::Vector3 TransitVehicleComponent::GetDoorExitWorld(const math::Vector3& from) const
{
	const math::Matrix tm = GetEntity()->GetWorldTM();
	if (_doorExits.size() < 3)
		return tm.Translation();
	math::Vector3 best = tm.Translation();
	float bestD = 1e30f;
	for (size_t i = 0; (i + 1) * 3 <= _doorExits.size(); ++i)
	{
		const math::Vector3 w = math::Vector3::Transform(math::Vector3(_doorExits[i * 3], _doorExits[i * 3 + 1], _doorExits[i * 3 + 2]), tm);
		const float d = (w - from).LengthSquared();
		if (d < bestD)
		{
			bestD = d;
			best = w;
		}
	}
	return best;
}

bool TransitVehicleComponent::IsPlayerInDoorway() const
{
	auto* scene = GetEntity()->GetScene();
	if (scene == nullptr || scene->GetMainCamera() == nullptr || scene->GetMainCamera()->GetEntity() == nullptr)
		return false;
	const math::Vector3 p = scene->GetMainCamera()->GetEntity()->GetWorldTM().Translation();
	const math::Matrix tm = GetEntity()->GetWorldTM();
	for (size_t i = 0; (i + 1) * 3 <= _doorExits.size(); ++i)
	{
		// From just outside the door to just inside it (the door well).
		const math::Vector3 local(_doorExits[i * 3], _doorExits[i * 3 + 1], _doorExits[i * 3 + 2]);
		const math::Vector3 outside = math::Vector3::Transform(local, tm);
		const math::Vector3 inside = math::Vector3::Transform(math::Vector3(local.x * 0.6f, local.y, local.z), tm);
		for (int32_t k = 0; k <= 4; ++k)
		{
			math::Vector3 d = outside + (inside - outside) * ((float)k / 4.0f) - p;
			d.y = 0.0f;
			if (d.LengthSquared() < 0.9f * 0.9f)
				return true;
		}
	}
	return false;
}

void TransitVehicleComponent::UpdatePassengers()
{
	auto* scene = GetEntity()->GetScene();
	if (scene == nullptr || _passengers.empty())
		return;
	const math::Matrix tm = GetEntity()->GetWorldTM();
	const float vehicleYaw = YawOf(tm);
	const int32_t spotCount = (int32_t)(_standSpots.size() / 3);
	for (size_t i = 0; i < _passengers.size();)
	{
		auto& p = _passengers[i];
		auto* e = scene->GetEntityByName(p.agentName);
		if (e == nullptr || e->IsPendingDeletion())
		{
			_passengers.erase(_passengers.begin() + i);
			continue;
		}
		math::Vector3 local = math::Vector3::Zero;
		if (spotCount > 0)
		{
			const int32_t s = p.spot % spotCount;
			local = math::Vector3(_standSpots[s * 3], _standSpots[s * 3 + 1], _standSpots[s * 3 + 2]);
		}
		if (auto* tf = e->GetComponent<HexEngine::Transform>())
		{
			const math::Vector3 world = math::Vector3::Transform(local, tm);
			tf->SetPosition(e->GetParent() != nullptr ? math::Vector3::Transform(world, e->GetParent()->GetWorldTMInvert()) : world);
			tf->SetRotation(math::Quaternion::CreateFromYawPitchRoll(vehicleYaw + p.yaw, 0.0f, 0.0f));
		}
		++i;
	}
}

void TransitVehicleComponent::Update(float frameTime)
{
	UpdateComponent::Update(frameTime);
	const bool running = HexEngine::g_pEnv != nullptr && HexEngine::g_pEnv->IsGameRunning();
	if (!running || frameTime <= 0.0f)
		return;
	const float dt = std::min(frameTime, 0.1f);

	// A mirrored vehicle (the kit bus is flipped for left-hand traffic) would
	// show each wheel's hub facing inwards: mirror the wheels back. They come
	// in symmetric pairs and spin about X, so nothing else changes.
	if (!_wheelsUnmirrored && GetEntity()->GetScale().x < 0.0f)
	{
		bool found = false;
		for (auto* child : GetEntity()->GetChildren())
		{
			if (child != nullptr && child->GetName().rfind("VehicleGen_Wheel", 0) == 0)
			{
				child->SetScale(math::Vector3(-1.0f, 1.0f, 1.0f));
				found = true;
			}
		}
		_wheelsUnmirrored = found;
	}

	if (!_giExcluded)
	{
		// Moving meshes would re-voxelise into GI every frame (GI revision thrash).
		std::vector<HexEngine::Entity*> stack{ GetEntity() };
		while (!stack.empty())
		{
			auto* e = stack.back(); stack.pop_back();
			if (auto* mesh = e->GetComponent<HexEngine::StaticMeshComponent>(); mesh != nullptr)
				mesh->SetExcludeFromGI(true);
			for (auto* child : e->GetChildren())
				if (child != nullptr) stack.push_back(child);
		}
		_giExcluded = true;
	}

	EnsureBodies();

	// Trailing carriage: ride behind the lead and copy its doors.
	if (!_leadEntityName.empty())
	{
		auto* lead = GetLead();
		if (lead == nullptr)
			return;
		const float sign = lead->GetDirection() >= 0 ? -1.0f : 1.0f;
		// Carriages trail the lead along the track in the +distance frame (the
		// train never turns round; the "lead" is simply carriage 0).
		(void)sign;
		PlaceOnTrack(lead->GetTrackDistance() - _carriageSpacing * (float)_carriageIndex);
		_doorTarget = lead->GetDoorOpenAmount() > 0.5f ? 1.0f : 0.0f;
		_doorOpen = lead->GetDoorOpenAmount();
		_openSide = lead->GetOpenSide();
		ApplyDoorMeshes();
		return;
	}

	auto* line = GetLine();
	if (line == nullptr)
		return;
	_stateTime += dt;
	if (_state != _loggedState)
	{
		static const char* kNames[] = { "Idle", "Driving", "Dwelling", "Departing" };
		LOG_DEBUG("TransitVehicle '%s': %s (stop %d -> %d)", GetEntity()->GetName().c_str(), kNames[(int32_t)_state], _stopIndex, _nextStopIndex);
		_loggedState = _state;
	}

	switch (_state)
	{
	case State::Idle:
		break;

	case State::Dwelling:
	{
		_doorTarget = 1.0f;
		if (!_arrivalProcessed && _doorOpen > 0.6f)
		{
			_arrivalProcessed = true;
			ProcessStopArrival();
		}
		// Boarders who haven't reached the door in time are put aboard.
		if (_stateTime > kMaxBoardWaitSeconds && !_pendingBoarders.empty())
		{
			auto pending = _pendingBoarders;
			for (const auto& name : pending)
			{
				auto* e = GetEntity()->GetScene()->GetEntityByName(name);
				auto* agent = e != nullptr ? e->GetComponent<RoutineAgentComponent>() : nullptr;
				if (agent != nullptr && agent->GetTransitPhase() == RoutineAgentComponent::TransitPhase::Boarding)
					BoardAgent(agent, agent->GetTransitAlightStop());
				else
					_pendingBoarders.erase(std::remove(_pendingBoarders.begin(), _pendingBoarders.end(), name), _pendingBoarders.end());
			}
		}
		const float dwell = line->GetDwellSeconds();
		const bool boardingDone = _pendingBoarders.empty();
		const bool playerHold = IsPlayerInDoorway() && _stateTime < dwell + kDoorwayHoldSeconds;
		if (_arrivalProcessed && _stateTime >= dwell && ((boardingDone && !playerHold) || _stateTime >= dwell + kMaxDwellExtension))
		{
			_state = State::Departing;
			_stateTime = 0.0f;
		}
		break;
	}

	case State::Departing:
		_doorTarget = 0.0f;
		// Someone in the doorway as the doors close: open up again (twice at most).
		if (_doorOpen > 0.0f && _reopenCount < 2 && IsPlayerInDoorway())
		{
			++_reopenCount;
			_state = State::Dwelling;
			_stateTime = std::max(0.0f, line->GetDwellSeconds() - 3.0f);
			break;
		}
		if (_doorOpen <= 0.001f && _stateTime > 0.6f)
			BeginLeg();
		break;

	case State::Driving:
	{
		_doorTarget = 0.0f;
		if (line->GetMode() == TransitLineComponent::Mode::Train)
		{
			UpdateTrainMotion(dt);
		}
		else if (auto* traffic = GetEntity()->GetComponent<TrafficVehicleComponent>())
		{
			if (traffic->ConsumeRouteEndReachedEvent())
			{
				ArriveAtStop();
				break;
			}
			// A bus that hasn't moved for a long time (gridlock, broken route)
			// is moved on to the stop - only while nobody is watching from inside.
			const math::Vector3 pos = GetEntity()->GetWorldTM().Translation();
			if ((pos - _lastPosition).LengthSquared() > 0.25f)
			{
				_lastPosition = pos;
				_stuckTime = 0.0f;
			}
			else
			{
				_stuckTime += dt;
			}
			if (_stuckTime > 90.0f && !_playerAboard)
			{
				LOG_WARN("TransitVehicle '%s': stuck for %.0f s on the way to stop %d - moving it on", GetEntity()->GetName().c_str(), _stuckTime, _nextStopIndex);
				if (auto* node = GetEntity()->GetScene()->GetEntityByName(!_targetNodeName.empty() ? _targetNodeName : line->GetStopLaneWaypoint(_nextStopIndex)))
					GetEntity()->ForcePosition(node->GetWorldTM().Translation());
				ArriveAtStop();
			}
		}
		break;
	}
	}

	// Door sounds on the open/close edge (not on spawn: -1 = unseen).
	if (_lastDoorTarget >= 0.0f && _doorTarget != _lastDoorTarget)
		PlayVehicleSound(_doorTarget > 0.5f ? _doorOpenSoundPath : _doorCloseSoundPath, true);
	_lastDoorTarget = _doorTarget;

	// Doors.
	const float rate = 1.0f / std::max(0.1f, _doorOpenSeconds);
	if (_doorOpen < _doorTarget)
		_doorOpen = std::min(_doorTarget, _doorOpen + rate * dt);
	else if (_doorOpen > _doorTarget)
		_doorOpen = std::max(_doorTarget, _doorOpen - rate * dt);
	ApplyDoorMeshes();
}

void TransitVehicleComponent::PlayVehicleSound(const std::string& paths, bool positional)
{
	using namespace HexEngine;
	if (paths.empty() || g_pEnv == nullptr || g_pEnv->_audioManager == nullptr)
		return;
	_activeSounds.erase(std::remove_if(_activeSounds.begin(), _activeSounds.end(),
		[](const std::shared_ptr<SoundEffect>& s) { return s == nullptr || !s->IsPlaying(); }), _activeSounds.end());
	// A comma list is a set of takes.
	std::vector<std::string> takes;
	size_t start = 0;
	while (start <= paths.size())
	{
		size_t end = paths.find(',', start);
		if (end == std::string::npos)
			end = paths.size();
		std::string t = paths.substr(start, end - start);
		t.erase(0, t.find_first_not_of(" 	"));
		t.erase(t.find_last_not_of(" 	") + 1);
		if (!t.empty())
			takes.push_back(std::move(t));
		start = end + 1;
	}
	if (takes.empty())
		return;
	auto master = SoundEffect::Create(takes[(size_t)GetRandomInt(0, (int32_t)takes.size() - 1)]);
	auto clone = master != nullptr ? master->CreatePlaybackClone() : nullptr;
	if (clone == nullptr)
		return;
	clone->SetVolume(std::clamp(_soundVolume, 0.0f, 1.0f));
	clone->SetPitch(GetRandomFloat(-0.03f, 0.03f));
	if (positional)
	{
		clone->SetRadius(22.0f);
		g_pEnv->_audioManager->Play(clone, GetEntity()->GetWorldTM().Translation() + math::Vector3(0.0f, 1.5f, 0.0f));
	}
	else
		g_pEnv->_audioManager->Play(clone);
	_activeSounds.push_back(std::move(clone));
}

void TransitVehicleComponent::PostUpdate(float frameTime)
{
	(void)frameTime;
	if (HexEngine::g_pEnv == nullptr || !HexEngine::g_pEnv->IsGameRunning())
		return;

	// Bodies follow the vehicle AFTER everything moved this frame (the bus is
	// driven by TrafficVehicleComponent's Update, whose order we don't control).
	// Rotation from the transform chain, NOT Matrix::Decompose: the vehicle is
	// mirrored (scale -1 on X) and Decompose may put that reflection on any axis
	// - when it picked Y the cabin colliders were posed upside-down (floor under
	// the road) and the player fell through / walked through the bus.
	const math::Vector3 pos = GetEntity()->GetWorldTM().Translation();
	math::Quaternion rot = GetEntity()->GetRotation();
	for (auto* parent = GetEntity()->GetParent(); parent != nullptr; parent = parent->GetParent())
	{
		rot = rot * parent->GetRotation();
		rot.Normalize();
	}
	if (_cabinBody != nullptr)
	{
		_cabinBody->UpdatePoseRotation(rot);
		_cabinBody->UpdatePosePosition(pos);
	}
	// Open doors (platform side only): park their blockers far below the world.
	const bool open = _doorOpen >= 0.35f;
	if (_doorBody != nullptr)
	{
		_doorBody->UpdatePoseRotation(rot);
		_doorBody->UpdatePosePosition(open && _openSide <= 0 ? pos - math::Vector3(0.0f, 500.0f, 0.0f) : pos);
	}
	if (_doorBodyPos != nullptr)
	{
		_doorBodyPos->UpdatePoseRotation(rot);
		_doorBodyPos->UpdatePosePosition(open && _openSide >= 0 ? pos - math::Vector3(0.0f, 500.0f, 0.0f) : pos);
	}

	UpdatePassengers();

	auto* scene = GetEntity()->GetScene();
	auto* fpcc = (scene != nullptr && scene->GetMainCamera() != nullptr && scene->GetMainCamera()->GetEntity() != nullptr)
		? scene->GetMainCamera()->GetEntity()->GetComponent<HexEngine::FirstPersonCameraController>() : nullptr;
	_playerAboard = fpcc != nullptr && fpcc->GetCarrier() == GetEntity();

	// Publish what the player is riding for the game's HUD and fares.
	if (_playerAboard)
	{
		auto& st = HexEngine::GetTransitPlayerState();
		st.frame = HexEngine::g_pEnv->_timeManager != nullptr ? (uint64_t)HexEngine::g_pEnv->_timeManager->_frameCount : 0;
		auto* lead = GetLead();
		auto* line = lead != nullptr ? lead->GetLine() : nullptr;
		if (line != nullptr)
		{
			st.lineName = line->GetDisplayName();
			st.mode = (int32_t)line->GetMode();
			int32_t dir = lead->GetDirection();
			const bool dwelling = lead->IsDwelling();
			const int32_t next = dwelling ? line->NextStop(lead->GetStopIndex(), dir) : lead->GetNextStopIndex();
			auto* nextStop = line->GetStop(next);
			auto* curStop = dwelling ? line->GetStop(lead->GetStopIndex()) : nullptr;
			st.nextStopName = nextStop != nullptr ? nextStop->GetDisplayName() : std::string();
			st.currentStopName = curStop != nullptr ? curStop->GetDisplayName() : std::string();
		}
		if (!_wasPlayerAboard)
		{
			++st.boardings;
			st.fare = st.mode == (int32_t)TransitLineComponent::Mode::Train ? 3 : 2;
		}
	}
	_wasPlayerAboard = _playerAboard;
}

void TransitVehicleComponent::OnRenderEditorGizmo(bool isSelected, bool& isHovering)
{
	(void)isHovering;
	if (!isSelected || HexEngine::g_pEnv == nullptr || HexEngine::g_pEnv->_debugRenderer == nullptr)
		return;
	const math::Matrix tm = GetEntity()->GetWorldTM();
	auto drawBoxes = [&](const std::vector<float>& boxes, const math::Color& colour)
	{
		math::Vector3 mn, mx;
		for (size_t i = 0; ParseBox(boxes, i, mn, mx); ++i)
		{
			math::Vector3 c[8];
			for (int32_t k = 0; k < 8; ++k)
				c[k] = math::Vector3::Transform(math::Vector3((k & 1) ? mx.x : mn.x, (k & 2) ? mx.y : mn.y, (k & 4) ? mx.z : mn.z), tm);
			const int32_t edges[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
			for (auto& e : edges)
				HexEngine::g_pEnv->_debugRenderer->DrawLine(c[e[0]], c[e[1]], colour);
		}
	};
	drawBoxes(_cabinBoxes, math::Color(0.3f, 0.9f, 1.0f, 0.8f));
	drawBoxes(_doorBoxes, math::Color(1.0f, 0.5f, 0.2f, 0.9f));
	for (size_t i = 0; (i + 1) * 3 <= _standSpots.size(); ++i)
	{
		const math::Vector3 p = math::Vector3::Transform(math::Vector3(_standSpots[i * 3], _standSpots[i * 3 + 1], _standSpots[i * 3 + 2]), tm);
		HexEngine::g_pEnv->_debugRenderer->DrawLine(p, p + math::Vector3(0, 1.7f, 0), math::Color(0.4f, 1.0f, 0.4f, 0.9f));
	}
	for (size_t i = 0; (i + 1) * 3 <= _doorExits.size(); ++i)
	{
		const math::Vector3 p = math::Vector3::Transform(math::Vector3(_doorExits[i * 3], _doorExits[i * 3 + 1], _doorExits[i * 3 + 2]), tm);
		HexEngine::g_pEnv->_debugRenderer->DrawLine(p, p + math::Vector3(0, 2.2f, 0), math::Color(1.0f, 1.0f, 0.2f, 0.9f));
	}
}

void TransitVehicleComponent::Serialize(json& data, HexEngine::JsonFile* file)
{
	file->Serialize(data, "_cabinBoxes", _cabinBoxes);
	file->Serialize(data, "_doorBoxes", _doorBoxes);
	file->Serialize(data, "_doorExits", _doorExits);
	file->Serialize(data, "_standSpots", _standSpots);
	file->Serialize(data, "_doorSlides", _doorSlides);
	file->Serialize(data, "_doorMeshPrefix", _doorMeshPrefix);
	file->Serialize(data, "_doorPartSlides", _doorPartSlides);
	file->Serialize(data, "_capacity", _capacity);
	file->Serialize(data, "_doorOpenSeconds", _doorOpenSeconds);
	file->Serialize(data, "_yawOffsetDegrees", _yawOffsetDegrees);
	file->Serialize(data, "_trackHeightOffset", _trackHeightOffset);
	file->Serialize(data, "_halfLength", _halfLength);
	file->Serialize(data, "_doorOpenSoundPath", _doorOpenSoundPath);
	file->Serialize(data, "_doorCloseSoundPath", _doorCloseSoundPath);
	file->Serialize(data, "_stopChimePath", _stopChimePath);
	file->Serialize(data, "_soundVolume", _soundVolume);
}

void TransitVehicleComponent::Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask)
{
	(void)mask;
	file->Deserialize(data, "_cabinBoxes", _cabinBoxes);
	file->Deserialize(data, "_doorBoxes", _doorBoxes);
	file->Deserialize(data, "_doorExits", _doorExits);
	file->Deserialize(data, "_standSpots", _standSpots);
	file->Deserialize(data, "_doorSlides", _doorSlides);
	file->Deserialize(data, "_doorMeshPrefix", _doorMeshPrefix);
	file->Deserialize(data, "_doorPartSlides", _doorPartSlides);
	file->Deserialize(data, "_capacity", _capacity);
	file->Deserialize(data, "_doorOpenSeconds", _doorOpenSeconds);
	file->Deserialize(data, "_yawOffsetDegrees", _yawOffsetDegrees);
	file->Deserialize(data, "_trackHeightOffset", _trackHeightOffset);
	file->Deserialize(data, "_halfLength", _halfLength);
	file->Deserialize(data, "_doorOpenSoundPath", _doorOpenSoundPath);
	file->Deserialize(data, "_doorCloseSoundPath", _doorCloseSoundPath);
	file->Deserialize(data, "_stopChimePath", _stopChimePath);
	file->Deserialize(data, "_soundVolume", _soundVolume);
	_doorMeshesCollected = false;
	_doorMeshes.clear();
}

bool TransitVehicleComponent::CreateWidget(HexEngine::ComponentWidget* widget)
{
	using namespace HexEngine;
	const int32_t w = widget->GetSize().x - 20;
	new DragInt(widget, widget->GetNextPos(), Point(w, 18), L"Capacity", &_capacity, 0, 400, 1);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Door Open Time (s)", &_doorOpenSeconds, 0.1f, 5.0f, 0.05f, 2);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Yaw Offset (deg, trains)", &_yawOffsetDegrees, -360.0f, 360.0f, 1.0f, 1);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Track Height Offset (trains)", &_trackHeightOffset, -10.0f, 10.0f, 0.01f, 2);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Half Length (trains)", &_halfLength, 0.5f, 50.0f, 0.1f, 2);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Sound Volume", &_soundVolume, 0.0f, 1.0f, 0.01f, 2);
	return true;
}
