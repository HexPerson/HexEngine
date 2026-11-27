#include "BuildingLightsComponent.hpp"
#include "PlaceOfWorkComponent.hpp"
#include "ResidenceComponent.hpp"
#include "RoutineAgentComponent.hpp"
#include <HexEngine.Core/Entity/Entity.hpp>
#include <HexEngine.Core/Entity/Component/DayNightCycleComponent.hpp>
#include <HexEngine.Core/GUI/Elements/Checkbox.hpp>
#include <HexEngine.Core/GUI/Elements/ComponentWidget.hpp>
#include <HexEngine.Core/GUI/Elements/DragFloat.hpp>
#include <HexEngine.Core/Scene/Scene.hpp>
#include <algorithm>
#include <cmath>

namespace
{
	uint32_t Fnv(const std::string& s, uint32_t h = 2166136261u)
	{
		for (char c : s) { h ^= (uint8_t)c; h *= 16777619u; }
		return h;
	}
	// FNV alone barely moves these bits when names differ only in their last
	// character ("House_Maple_1" vs "_2"), so every house of a type got the same
	// bedtime and lit the same rooms. Avalanche first (murmur3 finaliser).
	float H01(uint32_t h)
	{
		h ^= h >> 16; h *= 0x85ebca6bu;
		h ^= h >> 13; h *= 0xc2b2ae35u;
		h ^= h >> 16;
		return (float)((h >> 8) & 0xFFFF) / 65535.0f;
	}
	bool ContainsNoCase(const std::string& s, const char* needle)
	{
		std::string a = s, b = needle;
		std::transform(a.begin(), a.end(), a.begin(), ::tolower);
		std::transform(b.begin(), b.end(), b.begin(), ::tolower);
		return a.find(b) != std::string::npos;
	}
}

BuildingLightsComponent::BuildingLightsComponent(HexEngine::Entity* entity) :
	UpdateComponent(entity)
{
}

BuildingLightsComponent::BuildingLightsComponent(HexEngine::Entity* entity, BuildingLightsComponent* copy) :
	UpdateComponent(entity)
{
	if (copy != nullptr)
	{
		_dayThreshold = copy->_dayThreshold;
		_bedtimeMin = copy->_bedtimeMin;
		_bedtimeMax = copy->_bedtimeMax;
		_wakeHour = copy->_wakeHour;
		_simulatedOccupancy = copy->_simulatedOccupancy;
		_roomLitChance = copy->_roomLitChance;
		_closedNightLight = copy->_closedNightLight;
		_fadeSeconds = copy->_fadeSeconds;
		_previewInEditor = copy->_previewInEditor;
		_alwaysOpen = copy->_alwaysOpen;
	}
}

void BuildingLightsComponent::CollectLights()
{
	_lights.clear();
	_collected = true;
	auto* root = GetEntity();
	if (root == nullptr)
		return;
	_seed = Fnv(root->GetName());
	std::vector<HexEngine::Entity*> stack{ root };
	while (!stack.empty())
	{
		auto* e = stack.back(); stack.pop_back();
		if (auto* light = e->GetComponentDerived<HexEngine::Light>(); light != nullptr)
		{
			LightState s;
			s.baseMultiplier = light->GetLightMultiplier();
			s.current = s.target = s.pendingTarget = 1.0f;
			s.hash = Fnv(e->GetName(), _seed);
			s.isMain = ContainsNoCase(e->GetName(), "HouseLight") || ContainsNoCase(e->GetName(), "MainLight");
			_lights.emplace_back(e->GetId(), s);
		}
		for (auto* child : e->GetChildren())
			if (child != nullptr && !child->IsPendingDeletion())
				stack.push_back(child);
	}
	// Main light(s) first: index 0 doubles as a closed workplace's night light.
	std::stable_sort(_lights.begin(), _lights.end(), [](const auto& a, const auto& b) { return a.second.isMain > b.second.isMain; });
}

float BuildingLightsComponent::ComputeNight(float h) const
{
	// Same dusk/dawn ramps as the soundscape and traffic density.
	if (h >= 6.5f && h < 17.5f) return 0.0f;
	if (h >= 17.5f && h < 18.5f) return h - 17.5f;
	if (h >= 5.5f && h < 6.5f) return 1.0f - (h - 5.5f);
	return 1.0f;
}

bool BuildingLightsComponent::IsHomeOccupied(float hour) const
{
	(void)hour;
	auto* entity = GetEntity();
	auto* scene = entity != nullptr ? entity->GetScene() : nullptr;
	if (scene == nullptr)
		return false;
	const std::string& name = entity->GetName();
	std::vector<RoutineAgentComponent*> agents;
	scene->GetComponents<RoutineAgentComponent>(agents);
	bool hasResidents = false;
	for (auto* agent : agents)
	{
		if (agent == nullptr || agent->GetHomeEntityName() != name)
			continue;
		hasResidents = true;
		if (agent->GetSettledVenueName() == name)
			return true;
	}
	// Nobody in the simulation lives here: a simulated household, in most evenings.
	return !hasResidents && H01(_seed ^ 0x9e3779b9u) < _simulatedOccupancy;
}

float BuildingLightsComponent::DesiredLevel(const LightState& light, size_t index, float hour, float night, bool occupied, bool isHome, bool isWork, bool open) const
{
	const bool dark = night > _dayThreshold;
	if (isWork)
	{
		if (open)
			return 1.0f;
		return (dark && index == 0) ? _closedNightLight : 0.0f;
	}
	if (!dark)
		return 0.0f;
	if (!isHome)
		return 1.0f;   // a plain building: on whenever it's dark
	if (!occupied)
		return 0.0f;

	// Household timetable. Evening hours past midnight count as 24+.
	const float h = hour < 5.0f ? hour + 24.0f : hour;
	const float bedtime = _bedtimeMin + (_bedtimeMax - _bedtimeMin) * H01(_seed);
	const bool lit = light.isMain || H01(light.hash) < _roomLitChance;
	if (h >= 12.0f)
	{
		// Rooms go dark one by one over the last hour before bed; the main
		// light is the last off.
		const float earlyOff = light.isMain ? 0.0f : H01(light.hash * 2654435761u) * 1.0f;
		return (lit && h < bedtime - earlyOff) ? 1.0f : 0.0f;
	}
	// Dark mornings: a couple of lights while getting up.
	if (hour >= _wakeHour && hour < _wakeHour + 1.5f)
		return (light.isMain || H01(light.hash) < _roomLitChance * 0.5f) ? 1.0f : 0.0f;
	return 0.0f;
}

void BuildingLightsComponent::Update(float frameTime)
{
	UpdateComponent::Update(frameTime);

	const bool running = HexEngine::g_pEnv != nullptr &&
		(HexEngine::g_pEnv->IsGameRunning() || (_previewInEditor && HexEngine::g_pEnv->IsEditorMode()));
	auto* entity = GetEntity();
	auto* scene = entity != nullptr ? entity->GetScene() : nullptr;
	if (!running || scene == nullptr)
	{
		_collected = false;   // re-read the authored levels next run (play-stop restored them)
		return;
	}
	if (!_collected)
	{
		CollectLights();
		_evalTimer = 0.0f;
	}
	const float dt = std::clamp(frameTime, 0.0f, 0.25f);

	_evalTimer -= dt;
	if (_evalTimer <= 0.0f)
	{
		_evalTimer = 1.0f;
		float hour = 12.0f;
		std::vector<HexEngine::DayNightCycleComponent*> clocks;
		scene->GetComponents<HexEngine::DayNightCycleComponent>(clocks);
		if (!clocks.empty() && clocks[0] != nullptr)
			hour = clocks[0]->GetCurrentTimeHours();
		const float night = ComputeNight(hour);
		auto* work = entity->GetComponent<PlaceOfWorkComponent>();
		const bool isWork = work != nullptr;
		const bool isHome = entity->GetComponent<ResidenceComponent>() != nullptr;
		const bool open = isWork && (_alwaysOpen || work->IsHourInAnyShift(hour));
		const bool occupied = isHome && IsHomeOccupied(hour);
		for (size_t i = 0; i < _lights.size(); ++i)
		{
			auto& s = _lights[i].second;
			const float want = DesiredLevel(s, i, hour, night, occupied, isHome, isWork, open);
			if (want != s.pendingTarget)
			{
				s.pendingTarget = want;
				s.switchDelay = H01(s.hash ^ 0x5bd1e995u) * 4.0f;   // rooms switch one by one
			}
		}
	}

	const float step = dt / std::max(0.01f, _fadeSeconds);
	for (auto& [id, s] : _lights)
	{
		if (s.switchDelay > 0.0f)
		{
			s.switchDelay -= dt;
			if (s.switchDelay <= 0.0f)
				s.target = s.pendingTarget;
		}
		if (s.current == s.target)
			continue;
		s.current += std::clamp(s.target - s.current, -step, step);
		auto* e = scene->TryGetEntity(id);
		auto* light = (e != nullptr && !e->IsPendingDeletion()) ? e->GetComponentDerived<HexEngine::Light>() : nullptr;
		if (light != nullptr)
			light->SetLightMultiplier(s.baseMultiplier * s.current);
	}
}

void BuildingLightsComponent::Serialize(json& data, HexEngine::JsonFile* file)
{
	file->Serialize(data, "_dayThreshold", _dayThreshold);
	file->Serialize(data, "_bedtimeMin", _bedtimeMin);
	file->Serialize(data, "_bedtimeMax", _bedtimeMax);
	file->Serialize(data, "_wakeHour", _wakeHour);
	file->Serialize(data, "_simulatedOccupancy", _simulatedOccupancy);
	file->Serialize(data, "_roomLitChance", _roomLitChance);
	file->Serialize(data, "_closedNightLight", _closedNightLight);
	file->Serialize(data, "_fadeSeconds", _fadeSeconds);
	file->Serialize(data, "_previewInEditor", _previewInEditor);
	file->Serialize(data, "_alwaysOpen", _alwaysOpen);
}

void BuildingLightsComponent::Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask)
{
	(void)mask;
	file->Deserialize(data, "_dayThreshold", _dayThreshold);
	file->Deserialize(data, "_bedtimeMin", _bedtimeMin);
	file->Deserialize(data, "_bedtimeMax", _bedtimeMax);
	file->Deserialize(data, "_wakeHour", _wakeHour);
	file->Deserialize(data, "_simulatedOccupancy", _simulatedOccupancy);
	file->Deserialize(data, "_roomLitChance", _roomLitChance);
	file->Deserialize(data, "_closedNightLight", _closedNightLight);
	file->Deserialize(data, "_fadeSeconds", _fadeSeconds);
	file->Deserialize(data, "_previewInEditor", _previewInEditor);
	file->Deserialize(data, "_alwaysOpen", _alwaysOpen);
	_collected = false;
}

bool BuildingLightsComponent::CreateWidget(HexEngine::ComponentWidget* widget)
{
	using namespace HexEngine;
	const int32_t w = widget->GetSize().x - 20;
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Dark Threshold (night 0..1)", &_dayThreshold, 0.0f, 1.0f, 0.01f, 2);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Bedtime Earliest (h)", &_bedtimeMin, 18.0f, 28.0f, 0.1f, 1);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Bedtime Latest (h)", &_bedtimeMax, 18.0f, 28.0f, 0.1f, 1);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Wake (h)", &_wakeHour, 3.0f, 10.0f, 0.1f, 1);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Simulated Occupancy (no residents)", &_simulatedOccupancy, 0.0f, 1.0f, 0.01f, 2);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Room Lit Chance", &_roomLitChance, 0.0f, 1.0f, 0.01f, 2);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Closed Night Light", &_closedNightLight, 0.0f, 1.0f, 0.01f, 2);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Fade (s)", &_fadeSeconds, 0.01f, 5.0f, 0.01f, 2);
	new Checkbox(widget, widget->GetNextPos(), Point(w, 18), L"Always open (24h: hospital)", &_alwaysOpen);
	new Checkbox(widget, widget->GetNextPos(), Point(w, 18), L"Preview in editor", &_previewInEditor);
	return true;
}
