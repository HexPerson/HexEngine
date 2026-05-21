#pragma once

#include <array>
#include <vector>

#include <HexEngine.Core/HexEngine.hpp>
#include "WeatherTypes.hpp"

namespace HexEngine::Weather
{
	class WeatherZoneComponent;

	enum class WeatherAudioLoopSlot : uint32_t
	{
		Rain = 0,
		HeavyRain,
		Snow,
		Wind,
		BlizzardWind,
		Sandstorm,
		Count
	};

	struct WeatherLoopAudioConfig
	{
		std::string assetPath;
		float volume = 1.0f;
		float fadeInSeconds = 1.0f;
		float fadeOutSeconds = 1.5f;
		float pitch = 0.0f;
	};

	struct WeatherOneShotAudioConfig
	{
		std::string assetPath;
		float volume = 1.0f;
		float pitchMin = -0.08f;
		float pitchMax = 0.08f;
		float minDistance = 180.0f;
		float maxDistance = 420.0f;
		float radius = 900.0f;
	};

	class WeatherControllerComponent final : public UpdateComponent
	{
	public:
		CREATE_COMPONENT_ID(WeatherControllerComponent);
		DEFINE_COMPONENT_CTOR(WeatherControllerComponent);
		virtual ~WeatherControllerComponent() = default;

		virtual void Update(float frameTime) override;
		virtual void Destroy() override;
		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;
		virtual bool CreateWidget(ComponentWidget* widget) override;

		void ApplyPreset(WeatherPresetId presetId);
		const WeatherState& GetCurrentState() const { return _currentState; }

		// Manually advances the random-cycle picker by one step, ignoring the timer.
		// Exposed for the editor "Cycle Now" button and so external code (a game's
		// debug menu, a cutscene, etc.) can force a weather change.
		void AdvanceRandomCycle();

	private:
		// Picks the next preset for random cycling. Honors _naturalProgression: when
		// true, picks a neighbour of the current preset from a hand-authored adjacency
		// graph (Clear -> Overcast -> Rain -> HeavyRain -> ...). When false, any
		// non-Custom preset is fair game. Skips presets the user disabled in the
		// per-preset toggle mask.
		WeatherPresetId PickNextCyclePreset(WeatherPresetId current) const;

		WeatherState ResolveDesiredState(Scene* scene, const math::Vector3& samplePosition) const;
		void ApplyStateToScene(Scene* scene, const WeatherState& state);
		void UpdatePrecipitation(Scene* scene, Camera* camera, const WeatherState& currentState, const WeatherState& targetState, float frameTime);
		void UpdateLightning(Scene* scene, Camera* camera, const WeatherState& currentState, const WeatherState& targetState, float frameTime);
		void UpdateAudio(Scene* scene, Camera* camera, const WeatherState& currentState, const WeatherState& targetState, float frameTime);
		void CleanupHelperEntity(Entity*& entity);
		Entity* EnsurePrecipitationEntity(Scene* scene, size_t index);
		std::shared_ptr<ParticleEffect> BuildParticleEffect(const WeatherState& state) const;
		float ComputeDesiredLoopGain(WeatherAudioLoopSlot slot, const WeatherState& state) const;
		void EnsureLoopSoundLoaded(WeatherAudioLoopSlot slot);
		void UpdateLoopSound(WeatherAudioLoopSlot slot, float desiredGain, float frameTime);
		void TriggerThunderOneShot();

	private:
		struct WeatherLoopAudioRuntime
		{
			std::shared_ptr<SoundEffect> sound;
			std::string loadedAssetPath;
			float currentGain = 0.0f;
			bool looping = false;
		};

		struct ActiveThunderSoundRuntime
		{
			std::shared_ptr<SoundEffect> sound;
			float baseVolume = 0.0f;
			float basePitch = 0.0f;
		};

		WeatherPresetId _globalPresetId = WeatherPresetId::Clear;
		WeatherState _globalState = MakePresetState(WeatherPresetId::Clear);
		WeatherState _currentState = MakePresetState(WeatherPresetId::Clear);
		WeatherState _transitionSourceState = MakePresetState(WeatherPresetId::Clear);
		WeatherState _transitionTargetState = MakePresetState(WeatherPresetId::Clear);
		float _transitionElapsed = 0.0f;
		float _transitionDuration = 0.0f;
		float _defaultTransitionSeconds = 2.0f;
		bool _previewEnabled = true;
		std::array<WeatherLoopAudioConfig, static_cast<size_t>(WeatherAudioLoopSlot::Count)> _loopAudio = {};
		std::array<WeatherOneShotAudioConfig, 4> _thunderAudio = {};
		float _indoorWeatherVolumeScale = 0.42f;
		float _indoorWeatherPitchOffset = -0.08f;
		float _indoorThunderVolumeScale = 0.68f;
		float _indoorThunderPitchOffset = -0.04f;
		float _skyProbeDistance = 1200.0f;
		float _outdoorExposure = 1.0f;

		// --- Random preset cycling ---
		// When enabled, the controller automatically swaps presets every
		// _cycleIntervalSeconds. _naturalProgression picks neighbours of the current
		// preset from an authored adjacency graph; with it off the next preset is
		// any non-Custom preset chosen uniformly.
		// _enabledPresetMask is a bitmask keyed by WeatherPresetId index (bit (1 <<
		// presetId)). Default has every preset except Custom enabled. The user can
		// flip individual presets off if they want to keep e.g. Sandstorm out of
		// rotation for a city scene.
		bool _randomCyclingEnabled = false;
		bool _naturalProgression = true;
		float _cycleIntervalSeconds = 3600.0f; // 1 in-game hour by default
		float _cycleElapsedSeconds = 0.0f;     // accumulator (not serialised)
		uint32_t _enabledPresetMask = 0xFFFFFFFFu & ~(1u << static_cast<uint32_t>(WeatherPresetId::Custom));
		// rng state for picks; seeded lazily on first cycle so each playthrough gets
		// a different sequence unless the user explicitly pinned a seed via the var.
		mutable uint32_t _cycleRngState = 0;

		// Storage for the per-preset inspector checkboxes. Each entry's `enabled`
		// bool is what the Checkbox binds its bool* to, and the OnCheckFn fires
		// back into _enabledPresetMask. Lives on the component (not in a static)
		// so it survives across inspector rebuilds and doesn't dangle when the
		// component is destroyed.
		struct PresetMaskBinding
		{
			WeatherPresetId presetId = WeatherPresetId::Custom;
			bool enabled = false;
		};
		std::vector<std::shared_ptr<PresetMaskBinding>> _presetMaskBindings;

		std::array<Entity*, 7> _precipitationEntities = {};
		std::array<WeatherLoopAudioRuntime, static_cast<size_t>(WeatherAudioLoopSlot::Count)> _loopAudioRuntime = {};
		std::shared_ptr<SoundEffect> _thunderSound;
		std::vector<ActiveThunderSoundRuntime> _activeThunderSounds;
		std::string _thunderLoadedAssetPath;
		math::Vector3 _pendingThunderPosition = math::Vector3::Zero;
		float _pendingThunderDelay = -1.0f;
		float _pendingThunderVolume = 0.0f;
		float _pendingThunderPitch = 0.0f;
		std::shared_ptr<ParticleEffect> _precipitationEffect;
		std::string _precipitationEffectSignature;
		float _lightningCooldown = 0.0f;
		float _lightningRemaining = 0.0f;
		float _lightningClusterDuration = 0.0f;
		float _lightningFlash = 0.0f;
		float _lightningBoltBaseSeed = 0.0f;
		float _lightningBoltSeed = 0.0f;
		float _lightningBoltProgress = 0.0f;
		float _lightningBoltBaseWidth = 0.018f;
		float _lightningBoltWidth = 0.018f;
		math::Vector3 _lightningBoltBaseDirection = math::Vector3(0.3f, 0.85f, 0.2f);
		math::Vector3 _lightningBoltDirection = math::Vector3(0.3f, 0.85f, 0.2f);
		float _lightningBoltBranching = 0.55f;
		uint32_t _lightningActivePulseIndex = 0;
		uint32_t _lightningPulseCount = 0;
		std::array<float, 4> _lightningPulseOffsets = {};
		std::array<float, 4> _lightningPulseDurations = {};
		std::array<float, 4> _lightningPulseIntensities = {};
		std::array<float, 4> _lightningPulseSeedOffsets = {};
		std::array<float, 4> _lightningPulseWidthScales = {};
		std::array<float, 4> _lightningPulseBranchings = {};
		std::array<math::Vector3, 4> _lightningPulseDirectionOffsets = {};
	};
}
