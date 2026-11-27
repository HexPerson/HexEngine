#include "DistrictGateComponent.hpp"
#include "TrafficLaneComponent.hpp"
#include <HexEngine.Core/Entity/Entity.hpp>
#include <HexEngine.Core/Entity/Component/RigidBody.hpp>
#include <HexEngine.Core/Graphics/DebugRenderer.hpp>
#include <HexEngine.Core/GUI/Elements/Checkbox.hpp>
#include <HexEngine.Core/GUI/Elements/ComponentWidget.hpp>
#include <HexEngine.Core/GUI/Elements/DragFloat.hpp>
#include <HexEngine.Core/GUI/Elements/LineEdit.hpp>
#include <HexEngine.Core/Scene/Scene.hpp>
#include <algorithm>
#include <sstream>

namespace
{
	std::vector<std::string> SplitCsv(const std::string& csv)
	{
		std::vector<std::string> out;
		std::stringstream ss(csv);
		std::string item;
		while (std::getline(ss, item, ','))
		{
			const auto b = item.find_first_not_of(" \t");
			if (b == std::string::npos)
				continue;
			const auto e = item.find_last_not_of(" \t");
			out.push_back(item.substr(b, e - b + 1));
		}
		return out;
	}
}

DistrictGateComponent::DistrictGateComponent(HexEngine::Entity* entity) :
	UpdateComponent(entity)
{
}

DistrictGateComponent::DistrictGateComponent(HexEngine::Entity* entity, DistrictGateComponent* copy) :
	UpdateComponent(entity)
{
	if (copy != nullptr)
	{
		_locked = copy->_locked;
		_districtName = copy->_districtName;
		_closedLanesCsv = copy->_closedLanesCsv;
		_armsCsv = copy->_armsCsv;
		_blocksCsv = copy->_blocksCsv;
		_boundsMin = copy->_boundsMin;
		_boundsMax = copy->_boundsMax;
		_armOpenAngle = copy->_armOpenAngle;
	}
}

bool DistrictGateComponent::Contains(const math::Vector3& p) const
{
	return p.x >= _boundsMin.x && p.x <= _boundsMax.x &&
		p.y >= _boundsMin.y && p.y <= _boundsMax.y &&
		p.z >= _boundsMin.z && p.z <= _boundsMax.z;
}

void DistrictGateComponent::GetGates(HexEngine::Scene* scene, std::vector<DistrictGateComponent*>& out)
{
	out.clear();
	if (scene != nullptr)
		scene->GetComponents<DistrictGateComponent>(out);
}

bool DistrictGateComponent::IsPointLocked(HexEngine::Scene* scene, const math::Vector3& worldPos)
{
	std::vector<DistrictGateComponent*> gates;
	GetGates(scene, gates);
	for (auto* g : gates)
	{
		if (g != nullptr && g->IsLocked() && g->Contains(worldPos))
			return true;
	}
	return false;
}

bool DistrictGateComponent::CanTravel(HexEngine::Scene* scene, const math::Vector3& a, const math::Vector3& b)
{
	std::vector<DistrictGateComponent*> gates;
	GetGates(scene, gates);
	for (auto* g : gates)
	{
		if (g != nullptr && g->IsLocked() && g->Contains(a) != g->Contains(b))
			return false;
	}
	return true;
}

void DistrictGateComponent::Update(float frameTime)
{
	(void)frameTime;
	// Game-time only: the editor never opens the barrier, so a scene saved from the
	// editor always carries the authored (locked) barrier transforms and flags.
	if (HexEngine::g_pEnv == nullptr || !HexEngine::g_pEnv->IsGameRunning())
		return;
	const int32_t want = _locked ? 1 : 0;
	if (want != _appliedState)
		Apply(_locked);
}

void DistrictGateComponent::Apply(bool locked)
{
	auto* entity = GetEntity();
	auto* scene = entity != nullptr ? entity->GetScene() : nullptr;
	if (scene == nullptr)
		return;

	int32_t lanes = 0;
	for (const auto& name : SplitCsv(_closedLanesCsv))
	{
		auto* e = scene->GetEntityByName(name);
		auto* lane = e != nullptr ? e->GetComponent<TrafficLaneComponent>() : nullptr;
		if (lane != nullptr)
		{
			lane->SetClosed(locked);
			++lanes;
		}
	}

	for (const auto& name : SplitCsv(_armsCsv))
	{
		auto* e = scene->GetEntityByName(name);
		if (e == nullptr)
			continue;
		auto it = _armClosedRotation.find(name);
		if (it == _armClosedRotation.end())
			it = _armClosedRotation.emplace(name, e->GetRotation()).first;
		const float angle = locked ? 0.0f : _armOpenAngle * 0.01745329252f;
		// Lift about the arm's own pivot axis (local Z = along the road), arm swings up.
		e->SetRotation(math::Quaternion::CreateFromAxisAngle(math::Vector3::UnitZ, angle) * it->second);
	}

	for (const auto& name : SplitCsv(_blocksCsv))
	{
		auto* e = scene->GetEntityByName(name);
		if (e == nullptr)
			continue;
		const bool hidden = std::find(_hiddenByGate.begin(), _hiddenByGate.end(), name) != _hiddenByGate.end();
		if (!locked && !hidden)
		{
			if (!e->HasFlag(HexEngine::EntityFlags::DoNotRender))
			{
				e->SetFlag(HexEngine::EntityFlags::DoNotRender);
				_hiddenByGate.push_back(name);
			}
			if (auto* rb = e->GetComponent<HexEngine::RigidBody>(); rb != nullptr && rb->GetIRigidBody() != nullptr)
				rb->GetIRigidBody()->SetIsTrigger(true);
		}
		else if (locked && hidden)
		{
			e->ClearFlags(HexEngine::EntityFlags::DoNotRender);
			_hiddenByGate.erase(std::remove(_hiddenByGate.begin(), _hiddenByGate.end(), name), _hiddenByGate.end());
			if (auto* rb = e->GetComponent<HexEngine::RigidBody>(); rb != nullptr && rb->GetIRigidBody() != nullptr)
				rb->GetIRigidBody()->SetIsTrigger(false);
		}
	}

	_appliedState = locked ? 1 : 0;
	LOG_INFO("DistrictGate '%s': %s (%d lane(s) %s)", _districtName.c_str(), locked ? "LOCKED" : "OPEN", lanes, locked ? "closed" : "reopened");
}

void DistrictGateComponent::Serialize(json& data, HexEngine::JsonFile* file)
{
	file->Serialize(data, "_locked", _locked);
	file->Serialize(data, "_districtName", _districtName);
	file->Serialize(data, "_closedLanesCsv", _closedLanesCsv);
	file->Serialize(data, "_armsCsv", _armsCsv);
	file->Serialize(data, "_blocksCsv", _blocksCsv);
	file->Serialize(data, "_boundsMin", _boundsMin);
	file->Serialize(data, "_boundsMax", _boundsMax);
	file->Serialize(data, "_armOpenAngle", _armOpenAngle);
}

void DistrictGateComponent::Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask)
{
	(void)mask;
	file->Deserialize(data, "_locked", _locked);
	file->Deserialize(data, "_districtName", _districtName);
	file->Deserialize(data, "_closedLanesCsv", _closedLanesCsv);
	file->Deserialize(data, "_armsCsv", _armsCsv);
	file->Deserialize(data, "_blocksCsv", _blocksCsv);
	file->Deserialize(data, "_boundsMin", _boundsMin);
	file->Deserialize(data, "_boundsMax", _boundsMax);
	file->Deserialize(data, "_armOpenAngle", _armOpenAngle);
	_appliedState = -1;
}

bool DistrictGateComponent::CreateWidget(HexEngine::ComponentWidget* widget)
{
	using namespace HexEngine;
	const int32_t w = widget->GetSize().x - 20;
	new Checkbox(widget, widget->GetNextPos(), Point(w, 18), L"Locked (authored state; the game unlocks)", &_locked);

	auto text = [&](const wchar_t* label, std::string* target)
		{
			auto* edit = new LineEdit(widget, widget->GetNextPos(), Point(w, 18), label);
			edit->SetValue(std::wstring(target->begin(), target->end()));
			edit->SetDoesCallbackWaitForReturn(false);
			edit->SetOnInputFn([target](LineEdit*, const std::wstring& value) { *target = ws2s(value); });
		};
	text(L"District Name", &_districtName);
	text(L"Closed Lanes (CSV)", &_closedLanesCsv);
	text(L"Barrier Arms (CSV, lift when open)", &_armsCsv);
	text(L"Blocks (CSV, removed when open)", &_blocksCsv);

	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Bounds Min X", &_boundsMin.x, -10000.0f, 10000.0f, 1.0f, 1);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Bounds Min Z", &_boundsMin.z, -10000.0f, 10000.0f, 1.0f, 1);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Bounds Max X", &_boundsMax.x, -10000.0f, 10000.0f, 1.0f, 1);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Bounds Max Z", &_boundsMax.z, -10000.0f, 10000.0f, 1.0f, 1);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Arm Open Angle (deg)", &_armOpenAngle, 0.0f, 90.0f, 1.0f, 0);
	return true;
}

void DistrictGateComponent::OnRenderEditorGizmo(bool isSelected, bool& isHovering)
{
	(void)isHovering;
	if (!isSelected || HexEngine::g_pEnv == nullptr || HexEngine::g_pEnv->_debugRenderer == nullptr)
		return;
	dx::BoundingBox box;
	box.Center = (_boundsMin + _boundsMax) * 0.5f;
	box.Center.y = 32.0f;
	box.Extents = (_boundsMax - _boundsMin) * 0.5f;
	box.Extents.y = 6.0f;
	HexEngine::g_pEnv->_debugRenderer->DrawAABB(box, _locked ? math::Color(1.0f, 0.3f, 0.2f, 1.0f) : math::Color(0.3f, 1.0f, 0.4f, 1.0f));
}
