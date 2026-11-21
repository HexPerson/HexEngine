#pragma once

#include <HexEngine.Core/HexEngine.hpp>

// An area of the city with its own ambience: a box (in the entity's local
// frame, so it can be rotated and scaled with the transform) that fades in over
// _blendDistance metres around it. AmbientSoundscapeComponent mixes every zone
// the listener is in or near:
//  - BEDS: a day and a night loop (stereo, 2D), crossfaded by game time. Leave
//    one empty for a day-only / night-only layer; set both to the same file
//    for an around-the-clock one.
//  - ONE-SHOTS: random sounds from _oneShotsCsv, placed around the listener at
//    _oneShotMinDistance.._oneShotMaxDistance while the listener is in the zone.
// Zones are layered by _priority: a higher zone masks lower ones by its weight
// times _occlusion (a park inside downtown replaces the city bed; an interior
// zone with _occlusion < 1 leaves the street faintly audible).
// _interior zones only count while the listener is under a roof.
class AmbientZoneComponent : public HexEngine::BaseComponent
{
public:
	CREATE_COMPONENT_ID(AmbientZoneComponent);
	DEFINE_COMPONENT_CTOR(AmbientZoneComponent);

	enum class TimeMask : int32_t
	{
		Always = 0,
		Day = 1,
		Night = 2,
	};

	virtual void Serialize(json& data, HexEngine::JsonFile* file) override;
	virtual void Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask = 0) override;
	virtual bool CreateWidget(HexEngine::ComponentWidget* widget) override;
	virtual void OnDebugRender() override;
	virtual void OnRenderEditorGizmo(bool isSelected, bool& isHovering) override;

	// 1 inside the box, falling smoothly to 0 at _blendDistance outside it.
	float ComputeWeight(const math::Vector3& listenerWorld) const;

	int32_t GetPriority() const { return _priority; }
	bool IsInterior() const { return _interior; }
	float GetOcclusion() const { return _occlusion; }
	const std::string& GetDayBed() const { return _dayBed; }
	const std::string& GetNightBed() const { return _nightBed; }
	float GetBedVolume() const { return _bedVolume; }
	float GetRainDuck() const { return _rainDuck; }

	const std::vector<std::string>& GetOneShots() const { return _oneShots; }
	TimeMask GetOneShotTimes() const { return _oneShotTimes; }
	float GetOneShotMinInterval() const { return _oneShotMinInterval; }
	float GetOneShotMaxInterval() const { return _oneShotMaxInterval; }
	float GetOneShotMinDistance() const { return _oneShotMinDistance; }
	float GetOneShotMaxDistance() const { return _oneShotMaxDistance; }
	float GetOneShotVolume() const { return _oneShotVolume; }
	float GetOneShotRadius() const { return _oneShotRadius; }

private:
	void RebuildOneShots();
	void DrawBox() const;

	math::Vector3 _halfExtents = math::Vector3(50.0f, 40.0f, 50.0f);
	float _blendDistance = 25.0f;
	int32_t _priority = 0;
	float _occlusion = 1.0f;
	bool _interior = false;

	std::string _dayBed;
	std::string _nightBed;
	float _bedVolume = 1.0f;
	float _rainDuck = 0.0f;           // 0..1: how much full rain turns the beds down (birds hide)

	std::string _oneShotsCsv;
	std::vector<std::string> _oneShots;
	TimeMask _oneShotTimes = TimeMask::Always;
	float _oneShotMinInterval = 15.0f;
	float _oneShotMaxInterval = 45.0f;
	float _oneShotMinDistance = 20.0f;
	float _oneShotMaxDistance = 60.0f;
	float _oneShotVolume = 0.8f;
	float _oneShotRadius = 90.0f;

	bool _drawDebug = false;   // always draw; otherwise only while selected
};
