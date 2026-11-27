#include "ParkingLotComponent.hpp"
#include <HexEngine.Core/Entity/Entity.hpp>
#include <HexEngine.Core/Graphics/DebugRenderer.hpp>
#include <HexEngine.Core/GUI/Elements/Checkbox.hpp>
#include <HexEngine.Core/GUI/Elements/ComponentWidget.hpp>
#include <HexEngine.Core/GUI/Elements/DragFloat.hpp>
#include <HexEngine.Core/GUI/Elements/DragInt.hpp>
#include <HexEngine.Core/GUI/Elements/EntitySearch.hpp>
#include <HexEngine.Core/GUI/Elements/LineEdit.hpp>
#include <algorithm>
#include <cmath>

ParkingLotComponent::ParkingLotComponent(HexEngine::Entity* entity) :
	BaseComponent(entity)
{
}

ParkingLotComponent::ParkingLotComponent(HexEngine::Entity* entity, ParkingLotComponent* copy) :
	BaseComponent(entity)
{
	if (copy != nullptr)
	{
		_enabled = copy->_enabled;
		_privateTo = copy->_privateTo;
		_bays = copy->_bays;
		_bayCount = copy->_bayCount;
		_baySpacing = copy->_baySpacing;
		_doubleRow = copy->_doubleRow;
		_aisleDistance = copy->_aisleDistance;
		_entrance = copy->_entrance;
		_useGate = copy->_useGate;
		_gate = copy->_gate;
		_accessWaypointsCsv = copy->_accessWaypointsCsv;
		_maxWalkDistance = copy->_maxWalkDistance;
	}
}

namespace
{
	constexpr float kPi = 3.14159265358979f;

	math::Vector3 FlatUnit(math::Vector3 v)
	{
		v.y = 0.0f;
		const float len = v.Length();
		return len > 1e-5f ? v / len : math::Vector3(0.0f, 0.0f, 1.0f);
	}
}

math::Quaternion ParkingLotComponent::WorldRotation() const
{
	math::Vector3 scale, translation;
	math::Quaternion rotation;
	math::Matrix world = GetEntity()->GetWorldTM();
	world.Decompose(scale, rotation, translation);
	return rotation;
}

int32_t ParkingLotComponent::GetBayCount() const
{
	if (!_bays.empty())
		return (int32_t)_bays.size();
	return std::max(0, _bayCount) * (_doubleRow ? 2 : 1);
}

void ParkingLotComponent::GetLocalBay(int32_t bay, math::Vector3& outLocal, float& outYawRadians) const
{
	if (!_bays.empty())
	{
		outLocal = _bays[(size_t)bay].position;
		outYawRadians = _bays[(size_t)bay].yawDegrees * kPi / 180.0f;
		return;
	}
	// Generated: row 0 along +X noses -Z; row 1 across the aisle noses +Z. Both rows
	// pull in from the same aisle line at z = _aisleDistance.
	const int32_t perRow = std::max(1, _bayCount);
	const int32_t row = bay / perRow;
	const int32_t index = bay % perRow;
	outLocal = math::Vector3((float)index * _baySpacing, 0.0f, row == 0 ? 0.0f : 2.0f * _aisleDistance);
	outYawRadians = row == 0 ? kPi : 0.0f;
}

bool ParkingLotComponent::GetBay(int32_t bay, math::Vector3& outPosition, math::Vector3& outNose) const
{
	if (GetEntity() == nullptr || bay < 0 || bay >= GetBayCount())
		return false;
	math::Vector3 local;
	float yaw = 0.0f;
	GetLocalBay(bay, local, yaw);
	const math::Quaternion rotation = WorldRotation();
	outPosition = GetEntity()->GetWorldTM().Translation() + math::Vector3::Transform(local, rotation);
	outNose = FlatUnit(math::Vector3::Transform(math::Vector3(std::sin(yaw), 0.0f, std::cos(yaw)), rotation));
	return true;
}

math::Vector3 ParkingLotComponent::GetAislePoint(int32_t bay) const
{
	math::Vector3 position, nose;
	if (!GetBay(bay, position, nose))
		return GetEntrancePoint();
	return position - nose * _aisleDistance;
}

math::Vector3 ParkingLotComponent::GetEntrancePoint() const
{
	if (GetEntity() == nullptr)
		return math::Vector3::Zero;
	return GetEntity()->GetWorldTM().Translation() + math::Vector3::Transform(_entrance, WorldRotation());
}

math::Vector3 ParkingLotComponent::GetGatePoint() const
{
	if (GetEntity() == nullptr)
		return math::Vector3::Zero;
	return GetEntity()->GetWorldTM().Translation() + math::Vector3::Transform(_gate, WorldRotation());
}

void ParkingLotComponent::BuildArrivalLeg(int32_t bay, std::vector<math::Vector3>& out) const
{
	out.clear();
	math::Vector3 position, nose;
	if (!GetBay(bay, position, nose))
		return;
	out.push_back(GetEntrancePoint());
	if (_useGate)
		out.push_back(GetGatePoint());
	out.push_back(GetAislePoint(bay));
	// Straighten up before the bay so the car rolls in square rather than diagonally.
	out.push_back(position - nose * std::min(2.5f, _aisleDistance * 0.5f));
	out.push_back(position);
}

void ParkingLotComponent::BuildDepartureLeg(int32_t bay, std::vector<math::Vector3>& out, int32_t& outReversePoints) const
{
	out.clear();
	outReversePoints = 0;
	math::Vector3 position, nose;
	if (!GetBay(bay, position, nose))
		return;
	out.push_back(position);
	out.push_back(GetAislePoint(bay));	// backed out into the aisle
	outReversePoints = 1;
	if (_useGate)
		out.push_back(GetGatePoint());
	out.push_back(GetEntrancePoint());
}

void ParkingLotComponent::EnsureOccupancy() const
{
	if ((int32_t)_occupants.size() != GetBayCount())
		_occupants.resize((size_t)GetBayCount());
}

bool ParkingLotComponent::IsBayFree(int32_t bay) const
{
	EnsureOccupancy();
	return _enabled && bay >= 0 && bay < GetBayCount() && _occupants[(size_t)bay].empty();
}

int32_t ParkingLotComponent::CountFreeBays() const
{
	EnsureOccupancy();
	if (!_enabled)
		return 0;
	return (int32_t)std::count_if(_occupants.begin(), _occupants.end(), [](const std::string& s) { return s.empty(); });
}

const std::string& ParkingLotComponent::GetBayOccupant(int32_t bay) const
{
	static const std::string kEmpty;
	EnsureOccupancy();
	return bay >= 0 && bay < GetBayCount() ? _occupants[(size_t)bay] : kEmpty;
}

int32_t ParkingLotComponent::FindFreeBay(const math::Vector3& nearPoint) const
{
	if (!_enabled)
		return -1;
	EnsureOccupancy();
	int32_t best = -1;
	float bestDist = 0.0f;
	for (int32_t i = 0; i < GetBayCount(); ++i)
	{
		if (!_occupants[(size_t)i].empty())
			continue;
		math::Vector3 position, nose;
		if (!GetBay(i, position, nose))
			continue;
		const float d = (position - nearPoint).LengthSquared();
		if (best < 0 || d < bestDist)
		{
			best = i;
			bestDist = d;
		}
	}
	return best;
}

bool ParkingLotComponent::Reserve(int32_t bay, const std::string& carName)
{
	EnsureOccupancy();
	if (bay < 0 || bay >= GetBayCount())
		return false;
	auto& occupant = _occupants[(size_t)bay];
	if (!occupant.empty() && occupant != carName)
		return false;
	occupant = carName;
	return true;
}

void ParkingLotComponent::Release(int32_t bay, const std::string& carName)
{
	EnsureOccupancy();
	if (bay >= 0 && bay < GetBayCount() && _occupants[(size_t)bay] == carName)
		_occupants[(size_t)bay].clear();
}

void ParkingLotComponent::ReleaseAll()
{
	_occupants.clear();
}

void ParkingLotComponent::OnRenderEditorGizmo(bool isSelected, bool& isHovering)
{
	(void)isHovering;
	if (!isSelected || HexEngine::g_pEnv == nullptr || !HexEngine::g_pEnv->IsEditorMode() || HexEngine::g_pEnv->_debugRenderer == nullptr)
		return;
	auto* dr = HexEngine::g_pEnv->_debugRenderer;
	const math::Color bayColour(0.3f, 0.75f, 1.0f, 0.95f);
	const math::Color takenColour(1.0f, 0.45f, 0.3f, 0.95f);
	const math::Color pathColour(1.0f, 0.85f, 0.2f, 0.9f);
	const math::Vector3 lift(0.0f, 0.15f, 0.0f);
	const math::Vector3 entrance = GetEntrancePoint() + lift;
	dr->DrawLine(entrance, entrance + math::Vector3(0.0f, 2.0f, 0.0f), pathColour);
	if (_useGate)
	{
		const math::Vector3 gate = GetGatePoint() + lift;
		dr->DrawLine(gate, gate + math::Vector3(0.0f, 1.5f, 0.0f), pathColour);
	}
	EnsureOccupancy();
	for (int32_t i = 0; i < GetBayCount(); ++i)
	{
		math::Vector3 position, nose;
		if (!GetBay(i, position, nose))
			continue;
		const math::Vector3 side(nose.z, 0.0f, -nose.x);
		const math::Vector3 c = position + lift;
		const math::Color colour = _occupants[(size_t)i].empty() ? bayColour : takenColour;
		// A car-sized box (2.2 x 4.8 m) with a nose tick, and the pull-in path.
		const math::Vector3 a = c - side * 1.1f - nose * 2.4f, b = c + side * 1.1f - nose * 2.4f;
		const math::Vector3 d = c - side * 1.1f + nose * 2.4f, e = c + side * 1.1f + nose * 2.4f;
		dr->DrawLine(a, b, colour);
		dr->DrawLine(b, e, colour);
		dr->DrawLine(e, d, colour);
		dr->DrawLine(d, a, colour);
		dr->DrawLine(c, c + nose * 2.9f, colour);
		std::vector<math::Vector3> leg;
		BuildArrivalLeg(i, leg);
		for (size_t k = 0; k + 1 < leg.size(); ++k)
			dr->DrawLine(leg[k] + lift, leg[k + 1] + lift, pathColour);
	}
}

void ParkingLotComponent::Serialize(json& data, HexEngine::JsonFile* file)
{
	file->Serialize(data, "_enabled", _enabled);
	file->Serialize(data, "_privateTo", _privateTo);
	file->Serialize(data, "_bayCount", _bayCount);
	file->Serialize(data, "_baySpacing", _baySpacing);
	file->Serialize(data, "_doubleRow", _doubleRow);
	file->Serialize(data, "_aisleDistance", _aisleDistance);
	file->Serialize(data, "_entrance", _entrance);
	file->Serialize(data, "_useGate", _useGate);
	file->Serialize(data, "_gate", _gate);
	file->Serialize(data, "_accessWaypointsCsv", _accessWaypointsCsv);
	file->Serialize(data, "_maxWalkDistance", _maxWalkDistance);
	json bays = json::array();
	for (const auto& bay : _bays)
		bays.push_back(json::array({ bay.position.x, bay.position.y, bay.position.z, bay.yawDegrees }));
	data["_bays"] = bays;
}

void ParkingLotComponent::Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask)
{
	(void)mask;
	file->Deserialize(data, "_enabled", _enabled);
	file->Deserialize(data, "_privateTo", _privateTo);
	file->Deserialize(data, "_bayCount", _bayCount);
	file->Deserialize(data, "_baySpacing", _baySpacing);
	file->Deserialize(data, "_doubleRow", _doubleRow);
	file->Deserialize(data, "_aisleDistance", _aisleDistance);
	file->Deserialize(data, "_entrance", _entrance);
	file->Deserialize(data, "_useGate", _useGate);
	file->Deserialize(data, "_gate", _gate);
	file->Deserialize(data, "_accessWaypointsCsv", _accessWaypointsCsv);
	file->Deserialize(data, "_maxWalkDistance", _maxWalkDistance);
	_bays.clear();
	if (data.contains("_bays") && data["_bays"].is_array())
	{
		for (const auto& item : data["_bays"])
		{
			if (!item.is_array() || item.size() < 4)
				continue;
			Bay bay;
			bay.position = math::Vector3(item[0].get<float>(), item[1].get<float>(), item[2].get<float>());
			bay.yawDegrees = item[3].get<float>();
			_bays.push_back(bay);
		}
	}
	_occupants.clear();
}

bool ParkingLotComponent::CreateWidget(HexEngine::ComponentWidget* widget)
{
	using namespace HexEngine;
	const int32_t w = widget->GetSize().x - 20;
	new Checkbox(widget, widget->GetNextPos(), Point(w, 18), L"Enabled", &_enabled);

	auto* privateTo = new EntitySearch(widget, widget->GetNextPos(), Point(w, 18), L"Private To (residence)");
	privateTo->SetValue(std::wstring(_privateTo.begin(), _privateTo.end()));
	privateTo->SetOnInputFn([this](EntitySearch*, const std::wstring& value) { _privateTo = ws2s(value); });
	privateTo->SetOnSelectFn([this](EntitySearch*, const EntitySearchResult& result) { _privateTo = result.entityName; });

	new DragInt(widget, widget->GetNextPos(), Point(w, 18), L"Bays per Row (no bay list)", &_bayCount, 0, 64, 1);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Bay Spacing (m)", &_baySpacing, 2.0f, 6.0f, 0.05f, 2);
	new Checkbox(widget, widget->GetNextPos(), Point(w, 18), L"Double Row", &_doubleRow);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Aisle Distance (m)", &_aisleDistance, 2.0f, 15.0f, 0.05f, 2);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Entrance X", &_entrance.x, -100.0f, 100.0f, 0.05f, 2);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Entrance Z", &_entrance.z, -100.0f, 100.0f, 0.05f, 2);
	new Checkbox(widget, widget->GetNextPos(), Point(w, 18), L"Use Gate", &_useGate);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Gate X", &_gate.x, -100.0f, 100.0f, 0.05f, 2);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Gate Z", &_gate.z, -100.0f, 100.0f, 0.05f, 2);

	auto* access = new LineEdit(widget, widget->GetNextPos(), Point(w, 18), L"Access Lane Nodes (CSV, blank = auto)");
	access->SetValue(std::wstring(_accessWaypointsCsv.begin(), _accessWaypointsCsv.end()));
	access->SetDoesCallbackWaitForReturn(false);
	access->SetOnInputFn([this](LineEdit*, const std::wstring& value) { _accessWaypointsCsv = ws2s(value); });

	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Serves Within (m)", &_maxWalkDistance, 10.0f, 1000.0f, 1.0f, 0);
	return true;
}
