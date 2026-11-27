#pragma once

#include <HexEngine.Core/HexEngine.hpp>
#include <HexEngine.Core/Audio/SoundEffect.hpp>
#include <random>
#include <unordered_map>

class AmbientZoneComponent;
class AmbientEmitterComponent;

// The city's ambience, around the listener (the main camera). One per scene.
// Mixes, every frame:
//  - zone BEDS (AmbientZoneComponent): stereo day/night loops weighted by how
//    far inside each zone the listener is, layered by zone priority, crossfaded
//    by game time, ducked by rain (birds) and muffled indoors;
//  - a TRAFFIC HUM whose level follows the moving traffic near the listener;
//  - positional EMITTERS (AmbientEmitterComponent): only the nearest few within
//    their radius get a voice (_maxEmitterVoices);
//  - zone ONE-SHOTS on random timers (dogs, ...) placed around the listener;
//  - an HOUR BELL from a landmark on every game hour in waking hours.
// Indoors = a roof collider above the listener (rate-limited upward ray).
// Runs while the game is running (or in the editor with _previewInEditor).
class AmbientSoundscapeComponent : public HexEngine::UpdateComponent
{
public:
	CREATE_COMPONENT_ID(AmbientSoundscapeComponent);
	DEFINE_COMPONENT_CTOR(AmbientSoundscapeComponent);
	virtual ~AmbientSoundscapeComponent();

	virtual void Update(float frameTime) override;
	virtual void Destroy() override;
	virtual void Serialize(json& data, HexEngine::JsonFile* file) override;
	virtual void Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask = 0) override;
	virtual bool CreateWidget(HexEngine::ComponentWidget* widget) override;

	// Runtime state, for tools/tests.
	float GetNightFactor() const { return _night; }
	float GetIndoorFactor() const { return _indoor; }
	float GetTrafficDensity() const { return _trafficDensity; }

private:
	struct Voice
	{
		std::shared_ptr<HexEngine::SoundEffect> sound;
		float gain = 0.0f;
		bool playing = false;
	};
	struct OneShot
	{
		std::shared_ptr<HexEngine::SoundEffect> sound;
		float remaining = 0.0f;
	};

	void StopAll();
	std::shared_ptr<HexEngine::SoundEffect> MakeVoice(const std::string& path);
	float ComputeNightFactor(float hour) const;
	float TimeMaskWeight(int32_t mask) const;
	void UpdateIndoor(HexEngine::Scene* scene, HexEngine::Entity* listenerEntity, const math::Vector3& listener, float frameTime);
	void UpdateTraffic(const math::Vector3& listener, float frameTime);
	void UpdateBeds(const std::unordered_map<std::string, float>& targets, float frameTime);
	void UpdateEmitters(HexEngine::Scene* scene, const math::Vector3& listener, float frameTime);
	void UpdateOneShots(const std::vector<std::pair<AmbientZoneComponent*, float>>& zoneWeights, const math::Vector3& listener, float frameTime);
	void UpdateHourBell(HexEngine::Scene* scene, float hour);
	void PlayOneShot(const std::string& path, const math::Vector3& position, float volume, float radius);

	// --- authored ---
	bool _previewInEditor = false;
	float _masterVolume = 1.0f;
	float _fadeSeconds = 2.5f;
	float _dayStartHour = 6.0f;          // dawn ramp starts
	float _nightStartHour = 20.0f;       // dusk ramp starts
	float _transitionHours = 1.0f;
	float _indoorExteriorScale = 0.25f;  // exterior beds/emitters indoors
	int32_t _maxEmitterVoices = 10;

	std::string _trafficDayBed;
	std::string _trafficNightBed;
	float _trafficVolume = 0.8f;
	float _trafficRadius = 90.0f;
	float _trafficFullDensity = 6.0f;    // distance-weighted moving vehicles for full hum

	std::string _hourBell;
	std::string _bellEntityName;
	float _bellVolume = 1.0f;
	float _bellRadius = 700.0f;
	int32_t _bellFirstHour = 7;
	int32_t _bellLastHour = 22;

	// --- runtime ---
	bool _wasRunning = false;
	float _night = 0.0f;
	float _rain = 0.0f;
	float _wind = 0.0f;
	float _indoor = 0.0f;
	bool _roofOverhead = false;
	float _roofProbeCooldown = 0.0f;
	float _trafficDensity = 0.0f;
	float _trafficTarget = 0.0f;
	float _trafficCooldown = 0.0f;
	float _emitterCooldown = 0.0f;
	int32_t _lastBellHour = -1;
	std::unordered_map<std::string, std::shared_ptr<HexEngine::SoundEffect>> _masters;
	std::unordered_map<std::string, Voice> _beds;
	std::unordered_map<AmbientEmitterComponent*, Voice> _emitterVoices;
	std::vector<AmbientEmitterComponent*> _chosenEmitters;
	std::unordered_map<AmbientZoneComponent*, float> _oneShotTimers;
	std::vector<OneShot> _oneShots;
	std::mt19937 _rng{ 0x5eed1234u };
};
