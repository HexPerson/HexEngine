#pragma once

#include <HexEngine.Core/HexEngine.hpp>

// A positional ambient loop (rustling tree, water against the quay, pylon hum,
// workshop machinery) placed on an entity. It never plays by itself: the
// AmbientSoundscapeComponent gives voices only to the nearest emitters within
// their radius (up to its voice budget), so a whole street of trees costs a
// handful of voices, not hundreds.
class AmbientEmitterComponent : public HexEngine::BaseComponent
{
public:
	CREATE_COMPONENT_ID(AmbientEmitterComponent);
	DEFINE_COMPONENT_CTOR(AmbientEmitterComponent);

	virtual void Serialize(json& data, HexEngine::JsonFile* file) override;
	virtual void Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask = 0) override;
	virtual bool CreateWidget(HexEngine::ComponentWidget* widget) override;
	virtual void OnRenderEditorGizmo(bool isSelected, bool& isHovering) override;

	const std::string& GetSoundPath() const { return _soundPath; }
	float GetVolume() const { return _volume; }
	float GetRadius() const { return _radius; }
	int32_t GetTimes() const { return _times; }          // 0 always, 1 day, 2 night
	float GetWindScale() const { return _windScale; }
	float GetRainScale() const { return _rainScale; }
	math::Vector3 GetOffset() const { return _offset; }

private:
	std::string _soundPath;
	float _volume = 1.0f;
	float _radius = 30.0f;
	int32_t _times = 0;
	float _windScale = 0.0f;   // 0: wind-independent; 1: silent in still air, full in a strong wind
	float _rainScale = 1.0f;   // volume multiplier at full rain
	math::Vector3 _offset = math::Vector3::Zero;   // local offset from the entity origin (e.g. up into a tree canopy)
};
