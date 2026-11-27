#include "TransitLineComponent.hpp"
#include "TransitStopComponent.hpp"
#include "TransitVehicleComponent.hpp"
#include "TrafficLaneComponent.hpp"
#include "CityAssetPins.hpp"
#include <HexEngine.Core/Entity/Entity.hpp>
#include <HexEngine.Core/Entity/Component/Transform.hpp>
#include <HexEngine.Core/FileSystem/PrefabLoader.hpp>
#include <HexEngine.Core/Graphics/DebugRenderer.hpp>
#include <HexEngine.Core/GUI/Elements/AssetSearch.hpp>
#include <HexEngine.Core/GUI/Elements/Checkbox.hpp>
#include <HexEngine.Core/GUI/Elements/ComponentWidget.hpp>
#include <HexEngine.Core/GUI/Elements/DragFloat.hpp>
#include <HexEngine.Core/GUI/Elements/DragInt.hpp>
#include <HexEngine.Core/GUI/Elements/EntitySearch.hpp>
#include <HexEngine.Core/GUI/Elements/LineEdit.hpp>
#include <HexEngine.Core/Scene/Scene.hpp>
#include <HexEngine.Core/Scene/SceneManager.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace
{
	std::string Trim(const std::string& v)
	{
		const auto b = v.find_first_not_of(" \t");
		if (b == std::string::npos)
			return {};
		return v.substr(b, v.find_last_not_of(" \t") - b + 1);
	}

	std::string NearestLaneNode(HexEngine::Scene* scene, const math::Vector3& pos)
	{
		std::vector<TrafficLaneComponent*> lanes;
		scene->GetComponents<TrafficLaneComponent>(lanes);
		float best = std::numeric_limits<float>::max();
		std::string bestName;
		for (auto* lane : lanes)
		{
			if (lane == nullptr || lane->GetEntity() == nullptr || lane->GetEntity()->IsPendingDeletion())
				continue;
			const float d = (lane->GetEntity()->GetWorldTM().Translation() - pos).LengthSquared();
			if (d < best)
			{
				best = d;
				bestName = lane->GetEntity()->GetName();
			}
		}
		return bestName;
	}
}

TransitLineComponent::TransitLineComponent(HexEngine::Entity* entity) :
	UpdateComponent(entity)
{
}

TransitLineComponent::TransitLineComponent(HexEngine::Entity* entity, TransitLineComponent* copy) :
	UpdateComponent(entity)
{
	if (copy != nullptr)
	{
		_displayName = copy->_displayName;
		_mode = copy->_mode;
		_stopsCsv = copy->_stopsCsv;
		_closedLoop = copy->_closedLoop;
		_vehiclePrefab = copy->_vehiclePrefab;
		_vehicleCount = copy->_vehicleCount;
		_dwellSeconds = copy->_dwellSeconds;
		_trackPathEntityName = copy->_trackPathEntityName;
		_trainSpeed = copy->_trainSpeed;
		_trainAcceleration = copy->_trainAcceleration;
		_trainDeceleration = copy->_trainDeceleration;
		_carriagesPerTrain = copy->_carriagesPerTrain;
		_carriageSpacing = copy->_carriageSpacing;
		_enabled = copy->_enabled;
		RebuildStops();
	}
}

TransitLineComponent* TransitLineComponent::FindLine(HexEngine::Scene* scene, const std::string& lineEntityName)
{
	if (scene == nullptr || lineEntityName.empty())
		return nullptr;
	auto* e = scene->GetEntityByName(lineEntityName);
	return e != nullptr ? e->GetComponent<TransitLineComponent>() : nullptr;
}

const std::string& TransitLineComponent::GetDisplayName() const
{
	return _displayName.empty() ? GetEntity()->GetName() : _displayName;
}

void TransitLineComponent::RebuildStops()
{
	_stops.clear();
	std::stringstream ss(_stopsCsv);
	std::string token;
	while (std::getline(ss, token, ','))
	{
		token = Trim(token);
		if (!token.empty())
			_stops.push_back(token);
	}
	_stopLaneWaypoints.assign(_stops.size(), std::string());
	_trackBuilt = false;
}

const std::string& TransitLineComponent::GetStopEntityName(int32_t index) const
{
	static const std::string kEmpty;
	return (index >= 0 && index < (int32_t)_stops.size()) ? _stops[index] : kEmpty;
}

TransitStopComponent* TransitLineComponent::GetStop(int32_t index) const
{
	const std::string& name = GetStopEntityName(index);
	if (name.empty())
		return nullptr;
	auto* e = GetEntity()->GetScene()->GetEntityByName(name);
	return e != nullptr ? e->GetComponent<TransitStopComponent>() : nullptr;
}

int32_t TransitLineComponent::FindStopIndex(const std::string& stopEntityName) const
{
	for (int32_t i = 0; i < (int32_t)_stops.size(); ++i)
		if (_stops[i] == stopEntityName)
			return i;
	return -1;
}

int32_t TransitLineComponent::NextStop(int32_t index, int32_t& direction) const
{
	const int32_t n = (int32_t)_stops.size();
	if (n < 2)
		return index;
	if (direction == 0)
		direction = 1;
	if (_closedLoop)
		return ((index + direction) % n + n) % n;
	int32_t next = index + direction;
	if (next < 0 || next >= n)
	{
		direction = -direction;
		next = index + direction;
	}
	return std::clamp(next, 0, n - 1);
}

bool TransitLineComponent::IsAhead(int32_t from, int32_t direction, int32_t to) const
{
	if (from == to || from < 0 || to < 0)
		return false;
	if (_closedLoop)
		return true;
	return direction >= 0 ? to > from : to < from;
}

float TransitLineComponent::RideDistance(int32_t from, int32_t direction, int32_t to) const
{
	float total = 0.0f;
	int32_t i = from;
	int32_t dir = direction;
	for (int32_t guard = 0; guard < 2 * (int32_t)_stops.size() + 2 && i != to; ++guard)
	{
		const int32_t next = NextStop(i, dir);
		auto* a = GetStop(i);
		auto* b = GetStop(next);
		if (a != nullptr && b != nullptr)
			total += math::Vector3::Distance(a->GetEntity()->GetWorldTM().Translation(), b->GetEntity()->GetWorldTM().Translation());
		i = next;
	}
	return i == to ? total : std::numeric_limits<float>::max();
}

const std::string& TransitLineComponent::GetStopLaneWaypoint(int32_t index)
{
	static const std::string kEmpty;
	if (index < 0 || index >= (int32_t)_stops.size())
		return kEmpty;
	if (_stopLaneWaypoints.size() != _stops.size())
		_stopLaneWaypoints.assign(_stops.size(), std::string());
	if (_stopLaneWaypoints[index].empty())
	{
		if (auto* stop = GetStop(index); stop != nullptr)
		{
			const auto candidates = stop->GetLaneWaypointCandidates();
			_stopLaneWaypoints[index] = !candidates.empty()
				? candidates.front()
				: NearestLaneNode(GetEntity()->GetScene(), stop->GetEntity()->GetWorldTM().Translation());
		}
	}
	return _stopLaneWaypoints[index];
}

std::vector<std::string> TransitLineComponent::GetStopLaneWaypointCandidates(int32_t index)
{
	std::vector<std::string> out;
	if (auto* stop = GetStop(index); stop != nullptr)
		out = stop->GetLaneWaypointCandidates();
	if (out.empty())
	{
		const std::string& fallback = GetStopLaneWaypoint(index);
		if (!fallback.empty())
			out.push_back(fallback);
	}
	return out;
}

bool TransitLineComponent::EnsureTrack()
{
	if (_trackBuilt)
		return _trackLength > 0.01f;
	_trackBuilt = true;
	_trackPoints.clear();
	_trackCumulative.clear();
	_stopTrackDistances.clear();
	_trackLength = 0.0f;

	auto* scene = GetEntity()->GetScene();
	auto* path = scene != nullptr && !_trackPathEntityName.empty() ? scene->GetEntityByName(_trackPathEntityName) : nullptr;
	if (path == nullptr)
		return false;
	std::vector<HexEngine::Entity*> waypoints;
	for (auto* child : path->GetChildren())
		if (child != nullptr && !child->IsPendingDeletion())
			waypoints.push_back(child);
	std::sort(waypoints.begin(), waypoints.end(), [](HexEngine::Entity* a, HexEngine::Entity* b) { return a->GetName() < b->GetName(); });
	for (auto* wp : waypoints)
		_trackPoints.push_back(wp->GetWorldTM().Translation());
	if (_trackPoints.size() < 2)
		return false;
	if (_closedLoop)
		_trackPoints.push_back(_trackPoints.front());

	_trackCumulative.push_back(0.0f);
	for (size_t i = 1; i < _trackPoints.size(); ++i)
		_trackCumulative.push_back(_trackCumulative.back() + math::Vector3::Distance(_trackPoints[i - 1], _trackPoints[i]));
	_trackLength = _trackCumulative.back();

	// Each stop: the closest point of the track to the stop entity.
	for (size_t s = 0; s < _stops.size(); ++s)
	{
		auto* stop = GetStop((int32_t)s);
		float bestDistance = 0.0f;
		if (stop != nullptr)
		{
			const math::Vector3 p = stop->GetEntity()->GetWorldTM().Translation();
			float best = std::numeric_limits<float>::max();
			for (size_t i = 0; i + 1 < _trackPoints.size(); ++i)
			{
				const math::Vector3 a = _trackPoints[i], b = _trackPoints[i + 1];
				const math::Vector3 ab = b - a;
				const float len2 = std::max(ab.LengthSquared(), 1e-6f);
				const float t = std::clamp((p - a).Dot(ab) / len2, 0.0f, 1.0f);
				const math::Vector3 q = a + ab * t;
				math::Vector3 flat = q - p;
				flat.y *= 0.25f;	// platforms sit beside AND below/above the rails
				const float d = flat.LengthSquared();
				if (d < best)
				{
					best = d;
					bestDistance = _trackCumulative[i] + std::sqrt(len2) * t;
				}
			}
		}
		_stopTrackDistances.push_back(bestDistance);
	}
	return _trackLength > 0.01f;
}

float TransitLineComponent::GetStopTrackDistance(int32_t index) const
{
	return (index >= 0 && index < (int32_t)_stopTrackDistances.size()) ? _stopTrackDistances[index] : 0.0f;
}

math::Vector3 TransitLineComponent::SampleTrack(float d) const
{
	if (_trackPoints.empty())
		return math::Vector3::Zero;
	if (_closedLoop && _trackLength > 0.0f)
		d = std::fmod(std::fmod(d, _trackLength) + _trackLength, _trackLength);
	else
		d = std::clamp(d, 0.0f, _trackLength);
	auto it = std::upper_bound(_trackCumulative.begin(), _trackCumulative.end(), d);
	size_t i = (size_t)std::max<ptrdiff_t>(0, (it - _trackCumulative.begin()) - 1);
	i = std::min(i, _trackPoints.size() - 2);
	const float seg = std::max(1e-4f, _trackCumulative[i + 1] - _trackCumulative[i]);
	const float t = std::clamp((d - _trackCumulative[i]) / seg, 0.0f, 1.0f);
	return _trackPoints[i] + (_trackPoints[i + 1] - _trackPoints[i]) * t;
}

void TransitLineComponent::Update(float frameTime)
{
	UpdateComponent::Update(frameTime);
	const bool running = HexEngine::g_pEnv != nullptr && HexEngine::g_pEnv->IsGameRunning();
	if (!running)
	{
		if (_spawned)
		{
			_spawned = false;
			_vehicleNames.clear();
			_trackBuilt = false;
			for (int32_t i = 0; i < GetStopCount(); ++i)
				if (auto* stop = GetStop(i))
					stop->ClearWaiting();
		}
		return;
	}
	if (!_spawned)
	{
		_spawned = true;
		if (_enabled)
			SpawnVehicles();
	}
}

void TransitLineComponent::SpawnVehicles()
{
	auto* scene = GetEntity()->GetScene();
	const int32_t n = GetStopCount();
	if (scene == nullptr || n < 2 || _vehiclePrefab.empty() || _vehicleCount <= 0)
	{
		LOG_WARN("TransitLine '%s': needs 2+ stops, a vehicle prefab and a vehicle count (stops %d)", GetEntity()->GetName().c_str(), n);
		return;
	}
	if (GetMode() == Mode::Train && !EnsureTrack())
	{
		LOG_WARN("TransitLine '%s': train line has no track (path entity '%s')", GetEntity()->GetName().c_str(), _trackPathEntityName.c_str());
		return;
	}

	CityAssetPins::Prefab(_vehiclePrefab);
	auto sharedScene = HexEngine::g_pEnv->_sceneManager->GetCurrentScene();
	auto spawnOne = [&](const std::string& desiredName) -> TransitVehicleComponent*
	{
		auto spawned = HexEngine::g_pEnv->_prefabLoader->LoadPrefab(sharedScene, _vehiclePrefab);
		HexEngine::Entity* root = nullptr;
		for (auto* e : spawned)
		{
			if (e == nullptr)
				continue;
			e->SetFlag(HexEngine::EntityFlags::DoNotSave);
			// Transient movers are not GI geometry (spawning one used to rebuild every
			// GI clipmap's triangle list); flagged in the spawn frame, before the
			// scene resolves the add (Scene::ProcessPendingGiMeshes).
			for (auto* smc : e->GetComponents<HexEngine::StaticMeshComponent>())
				if (smc != nullptr)
					smc->SetExcludeFromGI(true);
			if (root == nullptr && e->GetParent() == nullptr)
				root = e;
		}
		if (root == nullptr)
			return nullptr;
		// Every vehicle needs its own name: agents, the player controller and
		// trailing carriages find vehicles by name.
		scene->RenameEntity(root, desiredName);
		auto* vehicle = root->GetComponent<TransitVehicleComponent>();
		if (vehicle == nullptr)
			vehicle = root->AddComponent<TransitVehicleComponent>();
		_vehicleNames.push_back(root->GetName());
		return vehicle;
	};
	for (int32_t k = 0; k < _vehicleCount; ++k)
	{
		// Spread the fleet evenly over the stops; on a ping-pong line every
		// other vehicle starts heading back.
		const int32_t stopIndex = (k * n) / _vehicleCount;
		const int32_t direction = (_closedLoop || (k % 2) == 0) ? 1 : -1;
		const std::string base = GetEntity()->GetName() + (GetMode() == Mode::Train ? "_Train" : "_Bus") + std::to_string(k + 1);
		auto* lead = spawnOne(base);
		if (lead == nullptr)
			continue;
		lead->AssignToLine(GetEntity()->GetName(), stopIndex, direction, k);
		if (GetMode() == Mode::Train)
		{
			for (int32_t c = 1; c < _carriagesPerTrain; ++c)
				if (auto* carriage = spawnOne(base + "_Car" + std::to_string(c + 1)))
					carriage->AssignAsCarriage(lead->GetEntity()->GetName(), c, _carriageSpacing);
		}
	}
	LOG_INFO("TransitLine '%s': %d vehicle(s) in service on %d stops", GetDisplayName().c_str(), (int32_t)_vehicleNames.size(), n);
}

void TransitLineComponent::OnRenderEditorGizmo(bool isSelected, bool& isHovering)
{
	(void)isHovering;
	if (!isSelected || HexEngine::g_pEnv == nullptr || HexEngine::g_pEnv->_debugRenderer == nullptr)
		return;
	const math::Color colour(0.2f, 0.8f, 1.0f, 0.9f);
	for (int32_t i = 0; i + 1 < GetStopCount() + (_closedLoop ? 1 : 0); ++i)
	{
		auto* a = GetStop(i);
		auto* b = GetStop((i + 1) % GetStopCount());
		if (a != nullptr && b != nullptr)
			HexEngine::g_pEnv->_debugRenderer->DrawLine(a->GetEntity()->GetWorldTM().Translation() + math::Vector3(0, 3, 0),
				b->GetEntity()->GetWorldTM().Translation() + math::Vector3(0, 3, 0), colour);
	}
	if (GetMode() == Mode::Train)
	{
		_trackBuilt = false;
		if (EnsureTrack())
			for (size_t i = 0; i + 1 < _trackPoints.size(); ++i)
				HexEngine::g_pEnv->_debugRenderer->DrawLine(_trackPoints[i], _trackPoints[i + 1], math::Color(1.0f, 0.4f, 0.8f, 0.9f));
		_trackBuilt = false;
	}
}

void TransitLineComponent::Serialize(json& data, HexEngine::JsonFile* file)
{
	file->Serialize(data, "_displayName", _displayName);
	file->Serialize(data, "_mode", _mode);
	file->Serialize(data, "_stopsCsv", _stopsCsv);
	file->Serialize(data, "_closedLoop", _closedLoop);
	file->Serialize(data, "_vehiclePrefab", _vehiclePrefab);
	file->Serialize(data, "_vehicleCount", _vehicleCount);
	file->Serialize(data, "_dwellSeconds", _dwellSeconds);
	file->Serialize(data, "_trackPathEntityName", _trackPathEntityName);
	file->Serialize(data, "_trainSpeed", _trainSpeed);
	file->Serialize(data, "_trainAcceleration", _trainAcceleration);
	file->Serialize(data, "_trainDeceleration", _trainDeceleration);
	file->Serialize(data, "_carriagesPerTrain", _carriagesPerTrain);
	file->Serialize(data, "_carriageSpacing", _carriageSpacing);
	file->Serialize(data, "_enabled", _enabled);
}

void TransitLineComponent::Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask)
{
	(void)mask;
	file->Deserialize(data, "_displayName", _displayName);
	file->Deserialize(data, "_mode", _mode);
	file->Deserialize(data, "_stopsCsv", _stopsCsv);
	file->Deserialize(data, "_closedLoop", _closedLoop);
	file->Deserialize(data, "_vehiclePrefab", _vehiclePrefab);
	file->Deserialize(data, "_vehicleCount", _vehicleCount);
	file->Deserialize(data, "_dwellSeconds", _dwellSeconds);
	file->Deserialize(data, "_trackPathEntityName", _trackPathEntityName);
	file->Deserialize(data, "_trainSpeed", _trainSpeed);
	file->Deserialize(data, "_trainAcceleration", _trainAcceleration);
	file->Deserialize(data, "_trainDeceleration", _trainDeceleration);
	file->Deserialize(data, "_carriagesPerTrain", _carriagesPerTrain);
	file->Deserialize(data, "_carriageSpacing", _carriageSpacing);
	file->Deserialize(data, "_enabled", _enabled);
	RebuildStops();
}

bool TransitLineComponent::CreateWidget(HexEngine::ComponentWidget* widget)
{
	using namespace HexEngine;
	const int32_t w = widget->GetSize().x - 20;
	auto* name = new LineEdit(widget, widget->GetNextPos(), Point(w, 18), L"Display Name");
	name->SetValue(std::wstring(_displayName.begin(), _displayName.end()));
	name->SetDoesCallbackWaitForReturn(false);
	name->SetOnInputFn([this](LineEdit*, const std::wstring& value) { _displayName = ws2s(value); });
	new DragInt(widget, widget->GetNextPos(), Point(w, 18), L"Mode (0 bus, 1 train)", &_mode, 0, 1, 1);
	auto* stops = new LineEdit(widget, widget->GetNextPos(), Point(w, 18), L"Stops (CSV of stop entities, in order)");
	stops->SetValue(std::wstring(_stopsCsv.begin(), _stopsCsv.end()));
	stops->SetDoesCallbackWaitForReturn(false);
	stops->SetOnInputFn([this](LineEdit*, const std::wstring& value) { _stopsCsv = ws2s(value); RebuildStops(); });
	new Checkbox(widget, widget->GetNextPos(), Point(w, 18), L"Closed Loop (else ping-pong)", &_closedLoop);
	auto* prefab = new LineEdit(widget, widget->GetNextPos(), Point(w, 18), L"Vehicle Prefab");
	prefab->SetValue(std::wstring(_vehiclePrefab.begin(), _vehiclePrefab.end()));
	prefab->SetDoesCallbackWaitForReturn(false);
	prefab->SetOnInputFn([this](LineEdit*, const std::wstring& value) { _vehiclePrefab = ws2s(value); });
	new DragInt(widget, widget->GetNextPos(), Point(w, 18), L"Vehicles", &_vehicleCount, 0, 20, 1);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Dwell (s)", &_dwellSeconds, 2.0f, 120.0f, 0.5f, 1);
	auto* track = new EntitySearch(widget, widget->GetNextPos(), Point(w, 18), L"Track Path (trains)");
	track->SetValue(std::wstring(_trackPathEntityName.begin(), _trackPathEntityName.end()));
	track->SetOnInputFn([this](EntitySearch*, const std::wstring& value) { _trackPathEntityName = ws2s(value); _trackBuilt = false; });
	track->SetOnSelectFn([this](EntitySearch*, const EntitySearchResult& result) { _trackPathEntityName = result.entityName; _trackBuilt = false; });
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Train Speed (m/s)", &_trainSpeed, 1.0f, 60.0f, 0.5f, 1);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Train Accel (m/s2)", &_trainAcceleration, 0.1f, 5.0f, 0.05f, 2);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Train Decel (m/s2)", &_trainDeceleration, 0.1f, 5.0f, 0.05f, 2);
	new DragInt(widget, widget->GetNextPos(), Point(w, 18), L"Carriages per Train", &_carriagesPerTrain, 1, 12, 1);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Carriage Spacing (m)", &_carriageSpacing, 2.0f, 60.0f, 0.1f, 2);
	new Checkbox(widget, widget->GetNextPos(), Point(w, 18), L"In Service", &_enabled);
	return true;
}
