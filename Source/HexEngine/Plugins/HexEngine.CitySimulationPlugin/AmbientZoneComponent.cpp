#include "AmbientZoneComponent.hpp"
#include <HexEngine.Core/Entity/Entity.hpp>
#include <HexEngine.Core/Graphics/DebugRenderer.hpp>
#include <HexEngine.Core/GUI/Elements/AssetSearch.hpp>
#include <HexEngine.Core/GUI/Elements/Checkbox.hpp>
#include <HexEngine.Core/GUI/Elements/ComponentWidget.hpp>
#include <HexEngine.Core/GUI/Elements/DragFloat.hpp>
#include <HexEngine.Core/GUI/Elements/DragInt.hpp>
#include <HexEngine.Core/GUI/Elements/LineEdit.hpp>
#include <algorithm>
#include <cmath>
#include <sstream>

AmbientZoneComponent::AmbientZoneComponent(HexEngine::Entity* entity) :
	BaseComponent(entity)
{
}

AmbientZoneComponent::AmbientZoneComponent(HexEngine::Entity* entity, AmbientZoneComponent* copy) :
	BaseComponent(entity)
{
	if (copy != nullptr)
	{
		_halfExtents = copy->_halfExtents;
		_blendDistance = copy->_blendDistance;
		_priority = copy->_priority;
		_occlusion = copy->_occlusion;
		_interior = copy->_interior;
		_dayBed = copy->_dayBed;
		_nightBed = copy->_nightBed;
		_bedVolume = copy->_bedVolume;
		_rainDuck = copy->_rainDuck;
		_oneShotsCsv = copy->_oneShotsCsv;
		_oneShotTimes = copy->_oneShotTimes;
		_oneShotMinInterval = copy->_oneShotMinInterval;
		_oneShotMaxInterval = copy->_oneShotMaxInterval;
		_oneShotMinDistance = copy->_oneShotMinDistance;
		_oneShotMaxDistance = copy->_oneShotMaxDistance;
		_oneShotVolume = copy->_oneShotVolume;
		_oneShotRadius = copy->_oneShotRadius;
		_drawDebug = copy->_drawDebug;
		RebuildOneShots();
	}
}

float AmbientZoneComponent::ComputeWeight(const math::Vector3& listenerWorld) const
{
	auto* entity = GetEntity();
	if (entity == nullptr)
		return 0.0f;

	// Into the box's local frame (rotation + scale of the entity apply), then
	// measure how far outside the box the listener is - in world metres, so the
	// blend distance doesn't stretch with the entity scale.
	const math::Matrix world = entity->GetWorldTM();
	const math::Matrix inv = world.Invert();
	const math::Vector3 local = math::Vector3::Transform(listenerWorld, inv);
	const math::Vector3 clamped(
		std::clamp(local.x, -_halfExtents.x, _halfExtents.x),
		std::clamp(local.y, -_halfExtents.y, _halfExtents.y),
		std::clamp(local.z, -_halfExtents.z, _halfExtents.z));
	const float outside = math::Vector3::Distance(math::Vector3::Transform(clamped, world), listenerWorld);
	if (outside <= 0.001f)
		return 1.0f;
	if (_blendDistance <= 0.01f || outside >= _blendDistance)
		return 0.0f;
	const float t = 1.0f - outside / _blendDistance;
	return t * t * (3.0f - 2.0f * t);
}

void AmbientZoneComponent::RebuildOneShots()
{
	_oneShots.clear();
	std::stringstream ss(_oneShotsCsv);
	std::string item;
	while (std::getline(ss, item, ','))
	{
		const auto b = item.find_first_not_of(" \t\r\n");
		const auto e = item.find_last_not_of(" \t\r\n");
		if (b != std::string::npos)
			_oneShots.push_back(item.substr(b, e - b + 1));
	}
}

void AmbientZoneComponent::Serialize(json& data, HexEngine::JsonFile* file)
{
	file->Serialize(data, "_halfExtents", _halfExtents);
	file->Serialize(data, "_blendDistance", _blendDistance);
	file->Serialize(data, "_priority", _priority);
	file->Serialize(data, "_occlusion", _occlusion);
	file->Serialize(data, "_interior", _interior);
	file->Serialize(data, "_dayBed", _dayBed);
	file->Serialize(data, "_nightBed", _nightBed);
	file->Serialize(data, "_bedVolume", _bedVolume);
	file->Serialize(data, "_rainDuck", _rainDuck);
	file->Serialize(data, "_oneShotsCsv", _oneShotsCsv);
	int32_t times = static_cast<int32_t>(_oneShotTimes);
	file->Serialize(data, "_oneShotTimes", times);
	file->Serialize(data, "_oneShotMinInterval", _oneShotMinInterval);
	file->Serialize(data, "_oneShotMaxInterval", _oneShotMaxInterval);
	file->Serialize(data, "_oneShotMinDistance", _oneShotMinDistance);
	file->Serialize(data, "_oneShotMaxDistance", _oneShotMaxDistance);
	file->Serialize(data, "_oneShotVolume", _oneShotVolume);
	file->Serialize(data, "_oneShotRadius", _oneShotRadius);
	file->Serialize(data, "_drawDebug", _drawDebug);
}

void AmbientZoneComponent::Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask)
{
	(void)mask;
	file->Deserialize(data, "_halfExtents", _halfExtents);
	file->Deserialize(data, "_blendDistance", _blendDistance);
	file->Deserialize(data, "_priority", _priority);
	file->Deserialize(data, "_occlusion", _occlusion);
	file->Deserialize(data, "_interior", _interior);
	file->Deserialize(data, "_dayBed", _dayBed);
	file->Deserialize(data, "_nightBed", _nightBed);
	file->Deserialize(data, "_bedVolume", _bedVolume);
	file->Deserialize(data, "_rainDuck", _rainDuck);
	file->Deserialize(data, "_oneShotsCsv", _oneShotsCsv);
	int32_t times = static_cast<int32_t>(_oneShotTimes);
	file->Deserialize(data, "_oneShotTimes", times);
	_oneShotTimes = static_cast<TimeMask>(std::clamp(times, 0, 2));
	file->Deserialize(data, "_oneShotMinInterval", _oneShotMinInterval);
	file->Deserialize(data, "_oneShotMaxInterval", _oneShotMaxInterval);
	file->Deserialize(data, "_oneShotMinDistance", _oneShotMinDistance);
	file->Deserialize(data, "_oneShotMaxDistance", _oneShotMaxDistance);
	file->Deserialize(data, "_oneShotVolume", _oneShotVolume);
	file->Deserialize(data, "_oneShotRadius", _oneShotRadius);
	file->Deserialize(data, "_drawDebug", _drawDebug);
	RebuildOneShots();
}

bool AmbientZoneComponent::CreateWidget(HexEngine::ComponentWidget* widget)
{
	using namespace HexEngine;
	const int32_t w = widget->GetSize().x - 20;

	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Half Extent X (m)", &_halfExtents.x, 0.5f, 5000.0f, 0.5f, 1);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Half Extent Y (m)", &_halfExtents.y, 0.5f, 1000.0f, 0.5f, 1);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Half Extent Z (m)", &_halfExtents.z, 0.5f, 5000.0f, 0.5f, 1);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Blend Distance (m)", &_blendDistance, 0.0f, 500.0f, 0.5f, 1);
	new DragInt(widget, widget->GetNextPos(), Point(w, 18), L"Priority", &_priority, -100, 100, 1);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Occlusion of lower zones", &_occlusion, 0.0f, 1.0f, 0.01f, 2);
	new Checkbox(widget, widget->GetNextPos(), Point(w, 18), L"Interior (needs a roof overhead)", &_interior);

	auto bindSound = [&](const wchar_t* label, std::string& target)
	{
		auto* search = new AssetSearch(widget, widget->GetNextPos(), Point(w, 84), label, { ResourceType::Audio },
			[&target](AssetSearch*, const AssetSearchResult& result)
			{
				const fs::path& chosen = !result.assetPath.empty() ? result.assetPath : result.absolutePath;
				target = chosen.string();
			});
		if (!target.empty())
			search->SetValue(std::wstring(target.begin(), target.end()));
	};
	bindSound(L"Day Bed", _dayBed);
	bindSound(L"Night Bed", _nightBed);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Bed Volume", &_bedVolume, 0.0f, 2.0f, 0.01f, 2);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Rain Duck", &_rainDuck, 0.0f, 1.0f, 0.01f, 2);

	auto* shots = new LineEdit(widget, widget->GetNextPos(), Point(w, 18), L"One-shots (CSV of sound paths)");
	shots->SetValue(std::wstring(_oneShotsCsv.begin(), _oneShotsCsv.end()));
	shots->SetDoesCallbackWaitForReturn(false);
	shots->SetOnInputFn([this](LineEdit*, const std::wstring& value) { _oneShotsCsv = ws2s(value); RebuildOneShots(); });
	new DragInt(widget, widget->GetNextPos(), Point(w, 18), L"One-shot Times (0 any, 1 day, 2 night)", reinterpret_cast<int32_t*>(&_oneShotTimes), 0, 2, 1);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"One-shot Min Interval (s)", &_oneShotMinInterval, 0.5f, 600.0f, 0.5f, 1);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"One-shot Max Interval (s)", &_oneShotMaxInterval, 0.5f, 900.0f, 0.5f, 1);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"One-shot Min Distance (m)", &_oneShotMinDistance, 0.0f, 500.0f, 0.5f, 1);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"One-shot Max Distance (m)", &_oneShotMaxDistance, 0.0f, 1000.0f, 0.5f, 1);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"One-shot Volume", &_oneShotVolume, 0.0f, 2.0f, 0.01f, 2);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"One-shot Radius (m)", &_oneShotRadius, 1.0f, 2000.0f, 1.0f, 0);
	new Checkbox(widget, widget->GetNextPos(), Point(w, 18), L"Always Draw Box (else when selected)", &_drawDebug);
	return true;
}

void AmbientZoneComponent::OnDebugRender()
{
	if (_drawDebug)
		DrawBox();
}

void AmbientZoneComponent::OnRenderEditorGizmo(bool isSelected, bool& isHovering)
{
	(void)isHovering;
	if (isSelected && !_drawDebug)
		DrawBox();
}

void AmbientZoneComponent::DrawBox() const
{
	if (HexEngine::g_pEnv == nullptr || !HexEngine::g_pEnv->IsEditorMode() || HexEngine::g_pEnv->_debugRenderer == nullptr || GetEntity() == nullptr)
		return;
	const math::Matrix world = GetEntity()->GetWorldTM();
	math::Vector3 scale, translation;
	math::Quaternion rotation;
	math::Matrix(world).Decompose(scale, rotation, translation);
	dx::BoundingOrientedBox box;
	box.Center = translation;
	box.Extents = math::Vector3(_halfExtents.x * scale.x, _halfExtents.y * scale.y, _halfExtents.z * scale.z);
	box.Orientation = rotation;
	const math::Color colour = _interior ? math::Color(1.0f, 0.75f, 0.2f, 0.5f) : math::Color(0.2f, 0.8f, 1.0f, 0.4f);
	HexEngine::g_pEnv->_debugRenderer->DrawOBB(box, colour);
}
