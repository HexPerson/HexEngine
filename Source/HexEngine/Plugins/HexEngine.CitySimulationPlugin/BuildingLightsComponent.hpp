#pragma once

#include <HexEngine.Core/HexEngine.hpp>
#include <HexEngine.Core/Entity/Component/Light.hpp>
#include <vector>

// Switches a building's interior lights like people live and work there.
// Sits on the building root next to its ResidenceComponent / PlaceOfWorkComponent
// and drives every Light underneath it (room lights + the main shadowed light):
//  - HOME: lights come on after dusk while someone who lives here is home and
//    awake, go off at a per-household bedtime (22:00-00:30), and come back on
//    briefly on dark mornings. Homes nobody lives in get a simulated household
//    (most evenings occupied). Not every room is lit, and rooms switch one by one.
//  - WORKPLACE: lit during opening hours; closed after dark, only a dim night
//    light stays on.
//  - Daytime: off unless a workplace is open.
// Only runs while the game is running (or with _previewInEditor); the authored
// light levels are the "on" level, and play-stop restores them.
class BuildingLightsComponent : public HexEngine::UpdateComponent
{
public:
	CREATE_COMPONENT_ID(BuildingLightsComponent);
	DEFINE_COMPONENT_CTOR(BuildingLightsComponent);

	virtual void Update(float frameTime) override;
	virtual void Serialize(json& data, HexEngine::JsonFile* file) override;
	virtual void Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask = 0) override;
	virtual bool CreateWidget(HexEngine::ComponentWidget* widget) override;

private:
	struct LightState
	{
		float baseMultiplier = 1.0f;
		float current = 1.0f;       // 0..1 of base
		float target = 1.0f;
		float switchDelay = 0.0f;   // seconds until target applies (stagger)
		float pendingTarget = 1.0f;
		uint32_t hash = 0;
		bool isMain = false;        // the building's main (shadowed) light: always part of 'on'
	};

	void CollectLights();
	float ComputeNight(float hour) const;
	bool IsHomeOccupied(float hour) const;
	float DesiredLevel(const LightState& light, size_t index, float hour, float night, bool occupied, bool isHome, bool isWork, bool open) const;

	float _dayThreshold = 0.35f;        // night factor above which interior lights are wanted
	float _bedtimeMin = 22.0f;
	float _bedtimeMax = 24.5f;
	float _wakeHour = 6.0f;
	float _simulatedOccupancy = 0.75f;  // chance an unassigned home is occupied on a given evening
	float _roomLitChance = 0.7f;        // share of rooms lit while the household is up
	float _closedNightLight = 0.3f;     // workplace night light level (one light) when closed
	float _fadeSeconds = 0.35f;
	bool _previewInEditor = false;
	bool _alwaysOpen = false;           // workplace lit around the clock (hospital) whatever its staff shifts

	// runtime
	std::vector<std::pair<HexEngine::EntityId, LightState>> _lights;
	bool _collected = false;
	float _evalTimer = 0.0f;
	uint32_t _seed = 0;
};
