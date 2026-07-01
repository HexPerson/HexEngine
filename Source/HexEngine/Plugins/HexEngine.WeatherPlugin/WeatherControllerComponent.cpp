#include "WeatherControllerComponent.hpp"

#include "WeatherZoneComponent.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <climits>
#include <random>
#include <string>

namespace HexEngine::Weather
{
	namespace
	{
		HVar* FindNamedHVar(const char* name)
		{
			return (g_pEnv != nullptr && g_pEnv->_commandManager != nullptr) ? g_pEnv->_commandManager->FindHVar(name) : nullptr;
		}

		void SetNamedHVarFloat(const char* name, float value)
		{
			if (HVar* var = FindNamedHVar(name); var != nullptr)
			{
				var->_val.f32 = value;
				var->Clamp();
			}
		}

		void SetNamedHVarVector3(const char* name, const math::Vector3& value)
		{
			if (HVar* var = FindNamedHVar(name); var != nullptr)
			{
				var->_val.v3 = value;
				var->Clamp();
			}
		}

		void PopulatePresetDropDown(DropDown* dropDown, WeatherPresetId currentPreset, std::function<void(WeatherPresetId)> onPick)
		{
			if (dropDown == nullptr)
				return;

			dropDown->SetValue(GetPresetDisplayName(currentPreset));
			for (int32_t i = static_cast<int32_t>(WeatherPresetId::Custom); i <= static_cast<int32_t>(WeatherPresetId::Sandstorm); ++i)
			{
				const WeatherPresetId presetId = static_cast<WeatherPresetId>(i);
				dropDown->GetContextMenu()->AddItem(new ContextItem(
					GetPresetDisplayName(presetId),
					[dropDown, onPick, presetId](const std::wstring&)
					{
						dropDown->SetValue(GetPresetDisplayName(presetId));
						onPick(presetId);
					}));
			}
		}

		float RandomRange(float minValue, float maxValue)
		{
			static std::mt19937 rng(1337);
			if (maxValue <= minValue)
				return minValue;
			std::uniform_real_distribution<float> dist(minValue, maxValue);
			return dist(rng);
		}

		// Authored transitions for natural progression. Each preset lists the
		// presets it can "evolve" into, picked uniformly at the next cycle step.
		// Bidirectional in most cases (Rain<->HeavyRain) so the weather can both
		// intensify and ease off; one-way only at the extremes (Thunderstorm
		// doesn't fall straight back to Clear without passing through something
		// in between). Skipping Custom everywhere - that's the editor placeholder
		// for a hand-tuned state and shouldn't be entered by the cycle.
		const std::vector<WeatherPresetId>& GetNaturalNeighbours(WeatherPresetId current)
		{
			static const std::vector<WeatherPresetId> kClear        = { WeatherPresetId::Overcast, WeatherPresetId::Hot };
			static const std::vector<WeatherPresetId> kOvercast     = { WeatherPresetId::Clear, WeatherPresetId::Rain, WeatherPresetId::Snow };
			static const std::vector<WeatherPresetId> kRain         = { WeatherPresetId::Overcast, WeatherPresetId::HeavyRain };
			static const std::vector<WeatherPresetId> kHeavyRain    = { WeatherPresetId::Rain, WeatherPresetId::Storm };
			static const std::vector<WeatherPresetId> kStorm        = { WeatherPresetId::HeavyRain, WeatherPresetId::Thunderstorm };
			static const std::vector<WeatherPresetId> kThunderstorm = { WeatherPresetId::Storm, WeatherPresetId::HeavyRain };
			static const std::vector<WeatherPresetId> kSnow         = { WeatherPresetId::Overcast, WeatherPresetId::Blizzard };
			static const std::vector<WeatherPresetId> kBlizzard     = { WeatherPresetId::Snow, WeatherPresetId::Storm };
			static const std::vector<WeatherPresetId> kHot          = { WeatherPresetId::Clear, WeatherPresetId::Sandstorm };
			static const std::vector<WeatherPresetId> kSandstorm    = { WeatherPresetId::Hot };
			static const std::vector<WeatherPresetId> kEmpty;

			switch (current)
			{
			case WeatherPresetId::Clear:        return kClear;
			case WeatherPresetId::Overcast:     return kOvercast;
			case WeatherPresetId::Rain:         return kRain;
			case WeatherPresetId::HeavyRain:    return kHeavyRain;
			case WeatherPresetId::Storm:        return kStorm;
			case WeatherPresetId::Thunderstorm: return kThunderstorm;
			case WeatherPresetId::Snow:         return kSnow;
			case WeatherPresetId::Blizzard:     return kBlizzard;
			case WeatherPresetId::Hot:          return kHot;
			case WeatherPresetId::Sandstorm:    return kSandstorm;
			case WeatherPresetId::Custom:
			default:
				return kEmpty;
			}
		}

		bool IsPresetEnabledForCycling(uint32_t mask, WeatherPresetId presetId)
		{
			const uint32_t bit = 1u << static_cast<uint32_t>(presetId);
			return (mask & bit) != 0;
		}

		// xorshift32 - tiny, fast, and we own the state so the global rng can stay
		// untouched. Seed lazily so each scene start picks a different sequence.
		uint32_t NextRng(uint32_t& state)
		{
			if (state == 0)
				state = static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count() | 1u);
			uint32_t x = state;
			x ^= x << 13;
			x ^= x >> 17;
			x ^= x << 5;
			state = x;
			return x;
		}

		bool NearlyEqualFloat(float a, float b, float epsilon = 0.0001f)
		{
			return std::abs(a - b) <= epsilon;
		}

		bool NearlyEqualVec3(const math::Vector3& a, const math::Vector3& b, float epsilon = 0.0001f)
		{
			return NearlyEqualFloat(a.x, b.x, epsilon) &&
				NearlyEqualFloat(a.y, b.y, epsilon) &&
				NearlyEqualFloat(a.z, b.z, epsilon);
		}

		bool NearlyEqualVec4(const math::Vector4& a, const math::Vector4& b, float epsilon = 0.0001f)
		{
			return NearlyEqualFloat(a.x, b.x, epsilon) &&
				NearlyEqualFloat(a.y, b.y, epsilon) &&
				NearlyEqualFloat(a.z, b.z, epsilon) &&
				NearlyEqualFloat(a.w, b.w, epsilon);
		}

		bool NearlyEqualColor(const math::Color& a, const math::Color& b, float epsilon = 0.0001f)
		{
			return NearlyEqualFloat(a.x, b.x, epsilon) &&
				NearlyEqualFloat(a.y, b.y, epsilon) &&
				NearlyEqualFloat(a.z, b.z, epsilon) &&
				NearlyEqualFloat(a.w, b.w, epsilon);
		}

		const wchar_t* GetLoopSlotDisplayName(WeatherAudioLoopSlot slot)
		{
			switch (slot)
			{
			case WeatherAudioLoopSlot::Rain: return L"Rain Loop";
			case WeatherAudioLoopSlot::HeavyRain: return L"Heavy Rain Loop";
			case WeatherAudioLoopSlot::Snow: return L"Snow Loop";
			case WeatherAudioLoopSlot::Wind: return L"Wind Loop";
			case WeatherAudioLoopSlot::BlizzardWind: return L"Blizzard Wind Loop";
			case WeatherAudioLoopSlot::Sandstorm: return L"Sandstorm Loop";
			default: return L"Weather Loop";
			}
		}

		const char* GetLoopSlotJsonName(WeatherAudioLoopSlot slot)
		{
			switch (slot)
			{
			case WeatherAudioLoopSlot::Rain: return "rain";
			case WeatherAudioLoopSlot::HeavyRain: return "heavyRain";
			case WeatherAudioLoopSlot::Snow: return "snow";
			case WeatherAudioLoopSlot::Wind: return "wind";
			case WeatherAudioLoopSlot::BlizzardWind: return "blizzardWind";
			case WeatherAudioLoopSlot::Sandstorm: return "sandstorm";
			default: return "weather";
			}
		}
		
		bool WeatherSurfaceMatches(const WeatherSurfaceResponse& a, const WeatherSurfaceResponse& b)
		{
			return NearlyEqualFloat(a.wetness, b.wetness) &&
				NearlyEqualFloat(a.puddleAmount, b.puddleAmount) &&
				NearlyEqualFloat(a.snowCoverage, b.snowCoverage) &&
				NearlyEqualFloat(a.snowMelt, b.snowMelt) &&
				NearlyEqualFloat(a.dirtAmount, b.dirtAmount) &&
				NearlyEqualFloat(a.temperatureBias, b.temperatureBias);
		}

		bool WeatherStateMatches(const WeatherState& a, const WeatherState& b)
		{
			return a.presetId == b.presetId &&
				a.precipitationType == b.precipitationType &&
				a.enableLightning == b.enableLightning &&
				WeatherSurfaceMatches(a.surface, b.surface) &&
				NearlyEqualVec4(a.ambientLight, b.ambientLight) &&
				NearlyEqualColor(a.fogColour, b.fogColour) &&
				NearlyEqualColor(a.sunColour, b.sunColour) &&
				NearlyEqualFloat(a.sunIntensity, b.sunIntensity) &&
				NearlyEqualFloat(a.zenithExponent, b.zenithExponent) &&
				NearlyEqualFloat(a.anisotropicIntensity, b.anisotropicIntensity) &&
				NearlyEqualFloat(a.atmosphereDensity, b.atmosphereDensity) &&
				NearlyEqualFloat(a.rayleighStrength, b.rayleighStrength) &&
				NearlyEqualFloat(a.mieStrength, b.mieStrength) &&
				NearlyEqualFloat(a.ambientSkyStrength, b.ambientSkyStrength) &&
				NearlyEqualFloat(a.sunHazeStrength, b.sunHazeStrength) &&
				NearlyEqualFloat(a.sunsetWarmStrength, b.sunsetWarmStrength) &&
				NearlyEqualFloat(a.sunsetCoolStrength, b.sunsetCoolStrength) &&
				NearlyEqualFloat(a.sunsetGlowStrength, b.sunsetGlowStrength) &&
				NearlyEqualFloat(a.volumetricScattering, b.volumetricScattering) &&
				NearlyEqualFloat(a.volumetricStrength, b.volumetricStrength) &&
				NearlyEqualFloat(a.fogDensity, b.fogDensity) &&
				NearlyEqualFloat(a.fogStartDistance, b.fogStartDistance) &&
				NearlyEqualFloat(a.fogHeightDensity, b.fogHeightDensity) &&
				NearlyEqualFloat(a.fogHeightFalloff, b.fogHeightFalloff) &&
				NearlyEqualFloat(a.fogHeightPivot, b.fogHeightPivot) &&
				NearlyEqualFloat(a.fogSkyTintInfluence, b.fogSkyTintInfluence) &&
				NearlyEqualFloat(a.cloudDensity, b.cloudDensity) &&
				NearlyEqualFloat(a.cloudCoverage, b.cloudCoverage) &&
				NearlyEqualFloat(a.cloudErosion, b.cloudErosion) &&
				NearlyEqualFloat(a.cloudAmbientStrength, b.cloudAmbientStrength) &&
				NearlyEqualFloat(a.cloudViewAbsorption, b.cloudViewAbsorption) &&
				NearlyEqualFloat(a.cloudShadowStrength, b.cloudShadowStrength) &&
				NearlyEqualFloat(a.cloudAnimationSpeed, b.cloudAnimationSpeed) &&
				NearlyEqualVec3(a.windDirection, b.windDirection) &&
				NearlyEqualFloat(a.windSpeed, b.windSpeed) &&
				NearlyEqualFloat(a.precipitationIntensity, b.precipitationIntensity) &&
				NearlyEqualFloat(a.precipitationAreaRadius, b.precipitationAreaRadius) &&
				NearlyEqualFloat(a.precipitationHeight, b.precipitationHeight) &&
				NearlyEqualFloat(a.lightningIntervalMin, b.lightningIntervalMin) &&
				NearlyEqualFloat(a.lightningIntervalMax, b.lightningIntervalMax) &&
				NearlyEqualFloat(a.lightningDuration, b.lightningDuration) &&
				NearlyEqualFloat(a.lightningIntensity, b.lightningIntensity) &&
				a.enableAurora == b.enableAurora &&
				NearlyEqualFloat(a.auroraIntensity, b.auroraIntensity) &&
				NearlyEqualFloat(a.auroraSpeed, b.auroraSpeed) &&
				NearlyEqualFloat(a.auroraBanding, b.auroraBanding) &&
				NearlyEqualFloat(a.auroraHeight, b.auroraHeight) &&
				NearlyEqualColor(a.auroraColorA, b.auroraColorA) &&
				NearlyEqualColor(a.auroraColorB, b.auroraColorB) &&
				NearlyEqualFloat(a.transitionSeconds, b.transitionSeconds);
		}

		std::string BuildPrecipitationSignature(const WeatherState& state)
		{
			// Do not key runtime precipitation off continuously lerped values, otherwise
			// transitions rebuild and reset the particle effect almost every frame.
			// Intensity and area are QUANTISED into coarse buckets instead of excluded:
			// the emission rate is baked into the effect at build time (the old design
			// kept rate fixed and topped it up with per-frame Trigger() calls, which
			// was frame-rate coupled and caused the spawn burst on preset changes), so
			// the effect must rebuild as intensity ramps - but only a handful of times
			// per transition, and every rebuild prewarms so there's no visible reset.
			const int32_t intensityBucket = static_cast<int32_t>(state.precipitationIntensity * 4.0f + 0.5f);
			const int32_t radiusBucket = static_cast<int32_t>(state.precipitationAreaRadius / 8.0f + 0.5f);
			return std::to_string(static_cast<int32_t>(state.precipitationType)) + ":" +
			       std::to_string(intensityBucket) + ":" +
			       std::to_string(radiusBucket);
		}
	}

	WeatherControllerComponent::WeatherControllerComponent(Entity* entity) :
		UpdateComponent(entity)
	{
		_loopAudio[static_cast<size_t>(WeatherAudioLoopSlot::Rain)].volume = 0.72f;
		_loopAudio[static_cast<size_t>(WeatherAudioLoopSlot::Rain)].fadeInSeconds = 1.8f;
		_loopAudio[static_cast<size_t>(WeatherAudioLoopSlot::Rain)].fadeOutSeconds = 2.4f;

		_loopAudio[static_cast<size_t>(WeatherAudioLoopSlot::HeavyRain)].volume = 1.0f;
		_loopAudio[static_cast<size_t>(WeatherAudioLoopSlot::HeavyRain)].fadeInSeconds = 1.0f;
		_loopAudio[static_cast<size_t>(WeatherAudioLoopSlot::HeavyRain)].fadeOutSeconds = 1.6f;

		_loopAudio[static_cast<size_t>(WeatherAudioLoopSlot::Snow)].volume = 0.55f;
		_loopAudio[static_cast<size_t>(WeatherAudioLoopSlot::Snow)].fadeInSeconds = 2.0f;
		_loopAudio[static_cast<size_t>(WeatherAudioLoopSlot::Snow)].fadeOutSeconds = 2.5f;

		_loopAudio[static_cast<size_t>(WeatherAudioLoopSlot::Wind)].volume = 0.42f;
		_loopAudio[static_cast<size_t>(WeatherAudioLoopSlot::Wind)].fadeInSeconds = 2.5f;
		_loopAudio[static_cast<size_t>(WeatherAudioLoopSlot::Wind)].fadeOutSeconds = 3.0f;

		_loopAudio[static_cast<size_t>(WeatherAudioLoopSlot::BlizzardWind)].volume = 0.92f;
		_loopAudio[static_cast<size_t>(WeatherAudioLoopSlot::BlizzardWind)].fadeInSeconds = 1.2f;
		_loopAudio[static_cast<size_t>(WeatherAudioLoopSlot::BlizzardWind)].fadeOutSeconds = 1.8f;

		_loopAudio[static_cast<size_t>(WeatherAudioLoopSlot::Sandstorm)].volume = 0.95f;
		_loopAudio[static_cast<size_t>(WeatherAudioLoopSlot::Sandstorm)].fadeInSeconds = 1.2f;
		_loopAudio[static_cast<size_t>(WeatherAudioLoopSlot::Sandstorm)].fadeOutSeconds = 1.8f;

		for (auto& thunder : _thunderAudio)
		{
			thunder.volume = 1.0f;
			thunder.pitchMin = -0.08f;
			thunder.pitchMax = 0.08f;
			thunder.minDistance = 180.0f;
			thunder.maxDistance = 420.0f;
			thunder.radius = 900.0f;
		}
	}

	WeatherControllerComponent::WeatherControllerComponent(Entity* entity, WeatherControllerComponent* copy) :
		UpdateComponent(entity, copy)
	{
		if (copy != nullptr)
		{
			_globalPresetId = copy->_globalPresetId;
			_globalState = copy->_globalState;
			_currentState = copy->_currentState;
			_transitionSourceState = copy->_transitionSourceState;
			_transitionTargetState = copy->_transitionTargetState;
			_transitionElapsed = copy->_transitionElapsed;
			_transitionDuration = copy->_transitionDuration;
			_defaultTransitionSeconds = copy->_defaultTransitionSeconds;
			_previewEnabled = copy->_previewEnabled;
			_loopAudio = copy->_loopAudio;
			_thunderAudio = copy->_thunderAudio;
			_indoorWeatherVolumeScale = copy->_indoorWeatherVolumeScale;
			_indoorWeatherPitchOffset = copy->_indoorWeatherPitchOffset;
			_indoorThunderVolumeScale = copy->_indoorThunderVolumeScale;
			_indoorThunderPitchOffset = copy->_indoorThunderPitchOffset;
			_skyProbeDistance = copy->_skyProbeDistance;
		}
	}

	WeatherPresetId WeatherControllerComponent::PickNextCyclePreset(WeatherPresetId current) const
	{
		// Build the candidate list and uniform-pick from it. Two flavours:
		//   natural progression: neighbours of current preset, filtered by mask.
		//   random: every non-Custom preset that's allowed by the mask.
		std::vector<WeatherPresetId> candidates;
		if (_naturalProgression)
		{
			for (auto neighbour : GetNaturalNeighbours(current))
			{
				if (IsPresetEnabledForCycling(_enabledPresetMask, neighbour))
					candidates.push_back(neighbour);
			}
			// If the natural-progression neighbours are all disabled by the mask,
			// fall through to a global pick rather than getting stuck. This keeps
			// the cycle alive even when the user has unchecked a chunk of presets.
		}
		if (candidates.empty())
		{
			for (int32_t i = static_cast<int32_t>(WeatherPresetId::Clear); i <= static_cast<int32_t>(WeatherPresetId::Sandstorm); ++i)
			{
				const auto candidate = static_cast<WeatherPresetId>(i);
				if (candidate == current) continue;
				if (IsPresetEnabledForCycling(_enabledPresetMask, candidate))
					candidates.push_back(candidate);
			}
		}
		if (candidates.empty())
			return current; // Nothing the user enabled - leave the preset alone.

		const uint32_t r = NextRng(_cycleRngState);
		return candidates[r % candidates.size()];
	}

	void WeatherControllerComponent::AdvanceRandomCycle()
	{
		const WeatherPresetId next = PickNextCyclePreset(_globalPresetId);
		if (next != _globalPresetId)
		{
			ApplyPreset(next);
		}
		_cycleElapsedSeconds = 0.0f;
	}

	void WeatherControllerComponent::Update(float frameTime)
	{
		UpdateComponent::Update(frameTime);

		Scene* scene = GetEntity()->GetScene();
		if (scene == nullptr || !_previewEnabled)
			return;

		// Random preset cycling. Accumulate frameTime and step the cycle when the
		// configured interval is reached. Bound the interval at 1s to keep the
		// editor from spinning the cycle on every frame if the user types 0.
		if (_randomCyclingEnabled)
		{
			const float interval = std::max(_cycleIntervalSeconds, 1.0f);
			_cycleElapsedSeconds += frameTime;
			if (_cycleElapsedSeconds >= interval)
			{
				AdvanceRandomCycle();
			}
		}
		else
		{
			_cycleElapsedSeconds = 0.0f;
		}

		Camera* camera = scene->GetMainCamera();
		const math::Vector3 samplePosition =
			(camera != nullptr && camera->GetEntity() != nullptr) ? camera->GetEntity()->GetPosition() : GetEntity()->GetPosition();

		const WeatherState desiredState = ResolveDesiredState(scene, samplePosition);
		if (!WeatherStateMatches(desiredState, _transitionTargetState))
		{
			_transitionSourceState = _currentState;
			_transitionTargetState = desiredState;
			_transitionElapsed = 0.0f;
			_transitionDuration = std::max(0.0f, desiredState.transitionSeconds > 0.0f ? desiredState.transitionSeconds : _defaultTransitionSeconds);
		}

		if (_transitionDuration <= 0.0f)
		{
			_currentState = _transitionTargetState;
			_transitionElapsed = _transitionDuration;
		}
		else
		{
			_transitionElapsed = std::min(_transitionElapsed + frameTime, _transitionDuration);
			const float transitionAlpha = std::clamp(_transitionElapsed / _transitionDuration, 0.0f, 1.0f);
			_currentState = LerpWeatherState(_transitionSourceState, _transitionTargetState, transitionAlpha);
		}

		UpdateLightning(scene, camera, _currentState, desiredState, frameTime);
		ApplyStateToScene(scene, _currentState);
		UpdatePrecipitation(scene, camera, _currentState, desiredState, frameTime);
		UpdateAudio(scene, camera, _currentState, desiredState, frameTime);
	}

	void WeatherControllerComponent::Destroy()
	{
		auto* audioManager = (g_pEnv != nullptr) ? g_pEnv->_audioManager : nullptr;
		for (auto& entity : _precipitationEntities)
			CleanupHelperEntity(entity);
		for (size_t i = 0; i < _loopAudioRuntime.size(); ++i)
		{
			auto& runtime = _loopAudioRuntime[i];
			if (audioManager != nullptr && runtime.sound != nullptr && runtime.looping)
				audioManager->Stop(runtime.sound);
			runtime.sound.reset();
			runtime.loadedAssetPath.clear();
			runtime.currentGain = 0.0f;
			runtime.looping = false;
		}
		if (audioManager != nullptr)
		{
			for (auto& thunderSound : _activeThunderSounds)
			{
				if (thunderSound.sound != nullptr)
					audioManager->Stop(thunderSound.sound);
			}
			if (_thunderSound != nullptr)
				audioManager->Stop(_thunderSound);
		}
		_activeThunderSounds.clear();
		_thunderSound.reset();
		_thunderLoadedAssetPath.clear();
		_precipitationEffect.reset();
		_precipitationEffectSignature.clear();
	}

	void WeatherControllerComponent::Serialize(json& data, JsonFile* file)
	{
		int32_t presetId = static_cast<int32_t>(_globalPresetId);
		file->Serialize(data, "_globalPresetId", presetId);
		file->Serialize(data, "_defaultTransitionSeconds", _defaultTransitionSeconds);
		file->Serialize(data, "_previewEnabled", _previewEnabled);
		file->Serialize(data, "_randomCyclingEnabled", _randomCyclingEnabled);
		file->Serialize(data, "_naturalProgression", _naturalProgression);
		file->Serialize(data, "_cycleIntervalSeconds", _cycleIntervalSeconds);
		file->Serialize(data, "_enabledPresetMask", _enabledPresetMask);
		SerializeWeatherState(_globalState, data["_globalState"], file);

		json& audioData = data["_audio"];
		file->Serialize(audioData, "indoorWeatherVolumeScale", _indoorWeatherVolumeScale);
		file->Serialize(audioData, "indoorWeatherPitchOffset", _indoorWeatherPitchOffset);
		file->Serialize(audioData, "indoorThunderVolumeScale", _indoorThunderVolumeScale);
		file->Serialize(audioData, "indoorThunderPitchOffset", _indoorThunderPitchOffset);
		file->Serialize(audioData, "skyProbeDistance", _skyProbeDistance);
		for (size_t i = 0; i < _loopAudio.size(); ++i)
		{
			const auto slot = static_cast<WeatherAudioLoopSlot>(i);
			json& loopData = audioData[GetLoopSlotJsonName(slot)];
			file->Serialize(loopData, "assetPath", _loopAudio[i].assetPath);
			file->Serialize(loopData, "volume", _loopAudio[i].volume);
			file->Serialize(loopData, "fadeInSeconds", _loopAudio[i].fadeInSeconds);
			file->Serialize(loopData, "fadeOutSeconds", _loopAudio[i].fadeOutSeconds);
			file->Serialize(loopData, "pitch", _loopAudio[i].pitch);
		}

		for (size_t i = 0; i < _thunderAudio.size(); ++i)
		{
			json& thunderData = audioData[std::format("thunder{}", i + 1)];
			file->Serialize(thunderData, "assetPath", _thunderAudio[i].assetPath);
			file->Serialize(thunderData, "volume", _thunderAudio[i].volume);
			file->Serialize(thunderData, "pitchMin", _thunderAudio[i].pitchMin);
			file->Serialize(thunderData, "pitchMax", _thunderAudio[i].pitchMax);
			file->Serialize(thunderData, "minDistance", _thunderAudio[i].minDistance);
			file->Serialize(thunderData, "maxDistance", _thunderAudio[i].maxDistance);
			file->Serialize(thunderData, "radius", _thunderAudio[i].radius);
		}
	}

	void WeatherControllerComponent::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		(void)mask;
		int32_t presetId = static_cast<int32_t>(_globalPresetId);
		file->Deserialize(data, "_globalPresetId", presetId);
		_globalPresetId = static_cast<WeatherPresetId>(presetId);
		file->Deserialize(data, "_defaultTransitionSeconds", _defaultTransitionSeconds);
		file->Deserialize(data, "_previewEnabled", _previewEnabled);
		file->Deserialize(data, "_randomCyclingEnabled", _randomCyclingEnabled);
		file->Deserialize(data, "_naturalProgression", _naturalProgression);
		file->Deserialize(data, "_cycleIntervalSeconds", _cycleIntervalSeconds);
		file->Deserialize(data, "_enabledPresetMask", _enabledPresetMask);
		if (data.contains("_globalState"))
			DeserializeWeatherState(_globalState, data["_globalState"], file);
		if (data.contains("_audio"))
		{
			json& audioData = data["_audio"];
			file->Deserialize(audioData, "indoorWeatherVolumeScale", _indoorWeatherVolumeScale);
			file->Deserialize(audioData, "indoorWeatherPitchOffset", _indoorWeatherPitchOffset);
			file->Deserialize(audioData, "indoorThunderVolumeScale", _indoorThunderVolumeScale);
			file->Deserialize(audioData, "indoorThunderPitchOffset", _indoorThunderPitchOffset);
			file->Deserialize(audioData, "skyProbeDistance", _skyProbeDistance);
			for (size_t i = 0; i < _loopAudio.size(); ++i)
			{
				const auto slot = static_cast<WeatherAudioLoopSlot>(i);
				if (!audioData.contains(GetLoopSlotJsonName(slot)))
					continue;

				json& loopData = audioData[GetLoopSlotJsonName(slot)];
				file->Deserialize(loopData, "assetPath", _loopAudio[i].assetPath);
				file->Deserialize(loopData, "volume", _loopAudio[i].volume);
				file->Deserialize(loopData, "fadeInSeconds", _loopAudio[i].fadeInSeconds);
				file->Deserialize(loopData, "fadeOutSeconds", _loopAudio[i].fadeOutSeconds);
				file->Deserialize(loopData, "pitch", _loopAudio[i].pitch);
			}

			for (size_t i = 0; i < _thunderAudio.size(); ++i)
			{
				const auto thunderKey = std::format("thunder{}", i + 1);
				if (!audioData.contains(thunderKey))
					continue;

				json& thunderData = audioData[thunderKey];
				file->Deserialize(thunderData, "assetPath", _thunderAudio[i].assetPath);
				file->Deserialize(thunderData, "volume", _thunderAudio[i].volume);
				file->Deserialize(thunderData, "pitchMin", _thunderAudio[i].pitchMin);
				file->Deserialize(thunderData, "pitchMax", _thunderAudio[i].pitchMax);
				file->Deserialize(thunderData, "minDistance", _thunderAudio[i].minDistance);
				file->Deserialize(thunderData, "maxDistance", _thunderAudio[i].maxDistance);
				file->Deserialize(thunderData, "radius", _thunderAudio[i].radius);
			}
		}
		// Seed the live state from the preset (not from _globalState which is
		// only the authored "Custom" override and is at default zeros for any
		// other preset). Otherwise _currentState starts at all-zero values
		// after deserialise, and the next Update() detects desiredState !=
		// _transitionTargetState and starts a 2-second lerp from zero to the
		// preset values. That lerp is invisible in the editor because the
		// scene has been running for ages and the lerp has long since
		// completed, but the launcher starts fresh and the screenshot lands
		// during the ramp - leaving e.g. g_weatherSurface.wetness at ~0,
		// which any material graph that reads it (wet road shaders driving
		// smoothness off wetness) interprets as "dry" and renders matte.
		// Reflections that the editor previews don't appear in the shipped
		// launcher until the ramp completes a couple of seconds in.
		//
		// Snapping the state machine to the resolved preset here means the
		// first frame already has the correct atmospheric / surface values,
		// matching what the editor preview shows.
		const WeatherState initialState = (_globalPresetId == WeatherPresetId::Custom)
			? _globalState
			: MakePresetState(_globalPresetId);
		_currentState = initialState;
		_transitionSourceState = initialState;
		_transitionTargetState = initialState;
		_transitionElapsed = 0.0f;
		_transitionDuration = 0.0f;
	}

	bool WeatherControllerComponent::CreateWidget(ComponentWidget* widget)
	{
		auto* preview = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Preview Enabled", &_previewEnabled);
		preview->SetPrefabOverrideBinding(GetComponentName(), "/_previewEnabled");

		auto* preset = new DropDown(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Global Preset");
		PopulatePresetDropDown(preset, _globalPresetId, [this, preset](WeatherPresetId presetId)
		{
			ApplyPreset(presetId);
			preset->SetValue(GetPresetDisplayName(presetId));
		});

		// --- Random preset cycling ---
		// Group all the cycling UI here so it's visually one block. Order: master
		// enable, interval, natural-progression toggle, manual cycle button,
		// per-preset inclusion mask.
		new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Random Cycling", &_randomCyclingEnabled);

		// Interval bound 1 second .. 7 days. The big upper bound is intentional -
		// players often want "one preset per real-world hour" or "per in-game day"
		// and forcing them to lower the cap by hand is annoying. Defaults to
		// 3600s (1 hour) which is the most common case.
		new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Cycle Interval (s)", &_cycleIntervalSeconds, 1.0f, 604800.0f, 1.0f, 1);
		new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Natural Progression", &_naturalProgression);

		new Button(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 22), L"Cycle Now",
			[this, preset](Button*) -> bool
			{
				AdvanceRandomCycle();
				preset->SetValue(GetPresetDisplayName(_globalPresetId));
				return true;
			});

		// Per-preset inclusion mask. Each non-Custom preset gets its own toggle so
		// the user can keep e.g. Sandstorm out of rotation for a city scene that
		// shouldn't ever see desert weather. Bindings are stored as members
		// (_presetMaskBindings) so they survive inspector rebuilds and don't dangle
		// when the component is destroyed - Checkbox holds a raw bool* into the
		// binding, which has to outlive the widget.
		_presetMaskBindings.clear();
		for (int32_t i = static_cast<int32_t>(WeatherPresetId::Clear); i <= static_cast<int32_t>(WeatherPresetId::Sandstorm); ++i)
		{
			const auto presetId = static_cast<WeatherPresetId>(i);
			auto binding = std::make_shared<PresetMaskBinding>();
			binding->presetId = presetId;
			binding->enabled = IsPresetEnabledForCycling(_enabledPresetMask, presetId);
			_presetMaskBindings.push_back(binding);

			std::wstring label = std::wstring(L"  Include: ") + GetPresetDisplayName(presetId);
			auto* cb = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), label, &binding->enabled);
			cb->SetOnCheckFn([this, bindingPtr = binding.get()](Checkbox*, bool)
			{
				const uint32_t bit = 1u << static_cast<uint32_t>(bindingPtr->presetId);
				if (bindingPtr->enabled)
					_enabledPresetMask |= bit;
				else
					_enabledPresetMask &= ~bit;
			});
		}

		new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Transition Seconds", &_defaultTransitionSeconds, 0.0f, 120.0f, 0.05f, 2);
		new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Precip Intensity", &_globalState.precipitationIntensity, 0.0f, 1.0f, 0.01f, 3);
		new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Wetness", &_globalState.surface.wetness, 0.0f, 1.0f, 0.01f, 3);
		new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Puddles", &_globalState.surface.puddleAmount, 0.0f, 1.0f, 0.01f, 3);
		new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Snow Coverage", &_globalState.surface.snowCoverage, 0.0f, 1.0f, 0.01f, 3);
		new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Wind Speed", &_globalState.windSpeed, 0.0f, 200.0f, 0.1f, 2);
		new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Lightning Intensity", &_globalState.lightningIntensity, 0.0f, 10.0f, 0.05f, 2);
		new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Enable Lightning", &_globalState.enableLightning);
		new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Enable Aurora", &_globalState.enableAurora);
		new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Aurora Intensity", &_globalState.auroraIntensity, 0.0f, 4.0f, 0.01f, 3);
		new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Aurora Speed", &_globalState.auroraSpeed, 0.01f, 4.0f, 0.01f, 3);
		new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Aurora Banding", &_globalState.auroraBanding, 0.1f, 4.0f, 0.01f, 3);
		new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Aurora Height", &_globalState.auroraHeight, 0.05f, 0.85f, 0.01f, 3);
		new ColourPicker(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 20), L"Aurora Color A", &_globalState.auroraColorA);
		new ColourPicker(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 20), L"Aurora Color B", &_globalState.auroraColorB);

		for (size_t i = 0; i < _loopAudio.size(); ++i)
		{
			auto slot = static_cast<WeatherAudioLoopSlot>(i);
			auto* search = new AssetSearch(
				widget,
				widget->GetNextPos(),
				Point(widget->GetSize().x - 20, 20),
				GetLoopSlotDisplayName(slot),
				{ ResourceType::Audio },
				[this, i](AssetSearch*, const AssetSearchResult& result)
				{
					_loopAudio[i].assetPath = result.assetPath.string();
				});
			search->SetValue(std::wstring(_loopAudio[i].assetPath.begin(), _loopAudio[i].assetPath.end()));
			new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), std::wstring(GetLoopSlotDisplayName(slot)) + L" Volume", &_loopAudio[i].volume, 0.0f, 2.0f, 0.01f, 3);
			new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), std::wstring(GetLoopSlotDisplayName(slot)) + L" Fade In", &_loopAudio[i].fadeInSeconds, 0.01f, 15.0f, 0.01f, 3);
			new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), std::wstring(GetLoopSlotDisplayName(slot)) + L" Fade Out", &_loopAudio[i].fadeOutSeconds, 0.01f, 15.0f, 0.01f, 3);
			new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), std::wstring(GetLoopSlotDisplayName(slot)) + L" Pitch", &_loopAudio[i].pitch, -1.0f, 1.0f, 0.01f, 3);
		}

		for (size_t i = 0; i < _thunderAudio.size(); ++i)
		{
			const std::wstring label = std::format(L"Thunder {}", i + 1);
			auto* thunderSearch = new AssetSearch(
				widget,
				widget->GetNextPos(),
				Point(widget->GetSize().x - 20, 20),
				label,
				{ ResourceType::Audio },
				[this, i](AssetSearch*, const AssetSearchResult& result)
				{
					_thunderAudio[i].assetPath = result.assetPath.string();
				});
			thunderSearch->SetValue(std::wstring(_thunderAudio[i].assetPath.begin(), _thunderAudio[i].assetPath.end()));
			new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), label + L" Volume", &_thunderAudio[i].volume, 0.0f, 2.0f, 0.01f, 3);
			new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), label + L" Pitch Min", &_thunderAudio[i].pitchMin, -1.0f, 1.0f, 0.01f, 3);
			new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), label + L" Pitch Max", &_thunderAudio[i].pitchMax, -1.0f, 1.0f, 0.01f, 3);
			new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), label + L" Min Dist", &_thunderAudio[i].minDistance, 0.0f, 5000.0f, 1.0f, 1);
			new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), label + L" Max Dist", &_thunderAudio[i].maxDistance, 0.0f, 5000.0f, 1.0f, 1);
			new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), label + L" Radius", &_thunderAudio[i].radius, 0.0f, 8000.0f, 1.0f, 1);
		}

		new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Indoor Weather Vol", &_indoorWeatherVolumeScale, 0.0f, 1.0f, 0.01f, 3);
		new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Indoor Weather Pitch", &_indoorWeatherPitchOffset, -1.0f, 1.0f, 0.01f, 3);
		new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Indoor Thunder Vol", &_indoorThunderVolumeScale, 0.0f, 1.0f, 0.01f, 3);
		new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Indoor Thunder Pitch", &_indoorThunderPitchOffset, -1.0f, 1.0f, 0.01f, 3);
		new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Sky Probe Distance", &_skyProbeDistance, 50.0f, 5000.0f, 1.0f, 1);
		return true;
	}

	void WeatherControllerComponent::ApplyPreset(WeatherPresetId presetId)
	{
		_globalPresetId = presetId;
		_globalState = MakePresetState(presetId);
		_globalState.transitionSeconds = _defaultTransitionSeconds;
		const bool snapImmediately = (_defaultTransitionSeconds <= 0.0f);
		_transitionSourceState = _currentState;
		_transitionTargetState = _globalState;
		_transitionElapsed = 0.0f;
		_transitionDuration = std::max(0.0f, _defaultTransitionSeconds);
		if (snapImmediately)
		{
			_currentState = _globalState;
			_transitionSourceState = _globalState;
			_transitionTargetState = _globalState;
			_lightningCooldown = 0.0f;
			_lightningRemaining = 0.0f;
			_lightningFlash = 0.0f;
			_lightningBoltProgress = 0.0f;

			if (Scene* scene = GetEntity() != nullptr ? GetEntity()->GetScene() : nullptr; scene != nullptr)
			{
				Camera* camera = scene->GetMainCamera();
				UpdateLightning(scene, camera, _currentState, _currentState, 0.0f);
				ApplyStateToScene(scene, _currentState);
				UpdatePrecipitation(scene, camera, _currentState, _currentState, 0.0f);
				UpdateAudio(scene, camera, _currentState, _currentState, 0.0f);
			}
		}
	}

	WeatherState WeatherControllerComponent::ResolveDesiredState(Scene* scene, const math::Vector3& samplePosition) const
	{
		WeatherState desiredState = (_globalPresetId == WeatherPresetId::Custom) ? _globalState : MakePresetState(_globalPresetId);
		desiredState.transitionSeconds = _defaultTransitionSeconds;

		std::vector<WeatherZoneComponent*> zones;
		if (scene == nullptr || !scene->GetComponents<WeatherZoneComponent>(zones))
			return desiredState;

		WeatherZoneComponent* bestZone = nullptr;
		float bestInfluence = 0.0f;
		int32_t bestPriority = INT_MIN;

		for (auto* zone : zones)
		{
			if (zone == nullptr || !zone->IsEnabled())
				continue;

			const float influence = zone->EvaluateInfluence(samplePosition);
			if (influence <= 0.0f)
				continue;

			if (zone->GetPriority() > bestPriority || (zone->GetPriority() == bestPriority && influence > bestInfluence))
			{
				bestZone = zone;
				bestInfluence = influence;
				bestPriority = zone->GetPriority();
			}
		}

		if (bestZone != nullptr)
			return LerpWeatherState(desiredState, bestZone->ResolveState(), bestInfluence);

		return desiredState;
	}

	void WeatherControllerComponent::ApplyStateToScene(Scene* scene, const WeatherState& state)
	{
		if (scene == nullptr)
			return;

		SetNamedHVarFloat("env_zenithExponent", state.zenithExponent);
		SetNamedHVarFloat("env_anisotropicIntensity", state.anisotropicIntensity);
		SetNamedHVarFloat("env_density", state.atmosphereDensity);
		SetNamedHVarFloat("env_rayleighStrength", state.rayleighStrength);
		SetNamedHVarFloat("env_mieStrength", state.mieStrength);
		SetNamedHVarFloat("env_ambientSkyStrength", state.ambientSkyStrength);
		SetNamedHVarFloat("env_sunHazeStrength", state.sunHazeStrength);
		SetNamedHVarFloat("env_sunsetWarmStrength", state.sunsetWarmStrength);
		SetNamedHVarFloat("env_sunsetCoolStrength", state.sunsetCoolStrength);
		SetNamedHVarFloat("env_sunsetGlowStrength", state.sunsetGlowStrength);
		SetNamedHVarFloat("env_volumetricScattering", state.volumetricScattering);
		SetNamedHVarFloat("env_volumetricStrength", state.volumetricStrength);

		SetNamedHVarFloat("r_fogDensity", state.fogDensity);
		SetNamedHVarFloat("r_fogStartDistance", state.fogStartDistance);
		SetNamedHVarFloat("r_fogHeightDensity", state.fogHeightDensity);
		SetNamedHVarFloat("r_fogHeightFalloff", state.fogHeightFalloff);
		SetNamedHVarFloat("r_fogHeightPivot", state.fogHeightPivot);
		SetNamedHVarFloat("r_fogSkyTintInfluence", state.fogSkyTintInfluence);

		SetNamedHVarFloat("r_cloudDensity", state.cloudDensity);
		SetNamedHVarFloat("r_cloudCoverage", state.cloudCoverage);
		SetNamedHVarFloat("r_cloudErosion", state.cloudErosion);
		SetNamedHVarFloat("r_cloudAmbientStrength", state.cloudAmbientStrength);
		SetNamedHVarFloat("r_cloudViewAbsorption", state.cloudViewAbsorption);
		SetNamedHVarFloat("r_cloudShadowStrength", state.cloudShadowStrength);
		SetNamedHVarFloat("r_cloudAnimationSpeed", state.cloudAnimationSpeed);
		SetNamedHVarVector3("r_cloudWindDirection", state.windDirection);
		SetNamedHVarFloat("r_cloudWindSpeed", state.windSpeed);

		const float sceneFlash = std::max(0.0f, _lightningFlash);
		const math::Vector4 flashedAmbient = state.ambientLight + math::Vector4(
			sceneFlash * 0.045f,
			sceneFlash * 0.052f,
			sceneFlash * 0.068f,
			0.0f);
		const math::Color flashedFog = math::Color(
			std::min(1.0f, state.fogColour.x + sceneFlash * 0.045f),
			std::min(1.0f, state.fogColour.y + sceneFlash * 0.050f),
			std::min(1.0f, state.fogColour.z + sceneFlash * 0.068f),
			state.fogColour.w);
		scene->SetAmbientLight(flashedAmbient);
		scene->SetFogColour(flashedFog);

		WeatherSurfaceParams surfaceParams;
		surfaceParams.wetness = state.surface.wetness;
		surfaceParams.puddleAmount = state.surface.puddleAmount;
		surfaceParams.snowCoverage = state.surface.snowCoverage;
		surfaceParams.snowMelt = state.surface.snowMelt;
		surfaceParams.dirtAmount = state.surface.dirtAmount;
		surfaceParams.temperatureBias = state.surface.temperatureBias;
		surfaceParams.precipitationIntensity = state.precipitationIntensity;
		surfaceParams.lightningFlash = _lightningFlash;
		math::Vector3 wind = state.windDirection;
		if (wind.LengthSquared() > 0.0001f)
			wind.Normalize();
		surfaceParams.windDirectionAndSpeed = math::Vector4(wind.x, wind.y, wind.z, state.windSpeed);
		surfaceParams.lightningBoltData = math::Vector4(_lightningFlash, _lightningBoltSeed, _lightningBoltProgress, _lightningBoltWidth);
		surfaceParams.lightningBoltDirection = math::Vector4(_lightningBoltDirection.x, _lightningBoltDirection.y, _lightningBoltDirection.z, _lightningBoltBranching);
		surfaceParams.auroraParams = math::Vector4(
			state.enableAurora ? state.auroraIntensity : 0.0f,
			state.auroraSpeed,
			state.auroraBanding,
			state.auroraHeight);
		surfaceParams.auroraColorA = math::Vector4(state.auroraColorA.x, state.auroraColorA.y, state.auroraColorA.z, state.auroraColorA.w);
		surfaceParams.auroraColorB = math::Vector4(state.auroraColorB.x, state.auroraColorB.y, state.auroraColorB.z, state.auroraColorB.w);
		scene->SetWeatherSurfaceParams(surfaceParams);

		if (DirectionalLight* sunLight = scene->GetSunLight(); sunLight != nullptr)
		{
			sunLight->SetDiffuseColour(state.sunColour);
			sunLight->SetLightStength(state.sunIntensity);
		}
	}

	void WeatherControllerComponent::UpdatePrecipitation(Scene* scene, Camera* camera, const WeatherState& currentState, const WeatherState& targetState, float frameTime)
	{
		if (scene == nullptr)
			return;

		const bool currentActive = (currentState.precipitationType != WeatherPrecipitationType::None && currentState.precipitationIntensity > 0.01f);
		const bool targetActive = (targetState.precipitationType != WeatherPrecipitationType::None && targetState.precipitationIntensity > 0.01f);
		if (!currentActive && !targetActive)
		{
			for (auto& entity : _precipitationEntities)
				CleanupHelperEntity(entity);
			_precipitationEffect.reset();
			_precipitationEffectSignature.clear();
			return;
		}

		const WeatherState& effectState = targetActive ? targetState : currentState;

		const math::Vector3 anchorPosition =
			(camera != nullptr && camera->GetEntity() != nullptr) ? camera->GetEntity()->GetPosition() : GetEntity()->GetPosition();

		// Single emitter entity, box centred around the camera. The previous
		// design spawned up to SEVEN overlapping full-size emitters once
		// intensity crossed 0.8/0.95 - which rebuilt + reset the whole effect
		// mid-transition (one cause of the "initial flurry") and made density
		// lumpy where the boxes overlapped. One emitter with the rate baked
		// for the bucketed intensity covers the same volume predictably.
		const std::string signature = BuildPrecipitationSignature(effectState);
		const bool effectChanged = (_precipitationEffect == nullptr || _precipitationEffectSignature != signature);
		if (effectChanged)
		{
			_precipitationEffect = BuildParticleEffect(effectState);
			_precipitationEffectSignature = signature;
		}

		Entity* helper = EnsurePrecipitationEntity(scene, 0);
		if (helper != nullptr)
		{
			// Anchor the spawn volume so it surrounds the camera vertically
			// rather than hovering overhead: the helper sits at 35% of the
			// precipitation height above the eye and the emitter boxes use
			// ~65% of the height as their Y half-extent, so the volume spans
			// from BELOW eye level up to the authored ceiling. The previous
			// code parked the helper a full precipitationHeight (~20m) up,
			// which (combined with the small-Y secondary emitters) is what
			// produced the visible dense particle band floating above the
			// camera with a sparse zone at eye level.
			helper->ForcePosition(anchorPosition + math::Vector3(0.0f, effectState.precipitationHeight * 0.35f, 0.0f));
			auto* particleComponent = helper->GetComponent<ParticleSystemComponent>();
			if (particleComponent != nullptr)
			{
				if (effectChanged)
				{
					// Prewarm (baked into the emitter descs) refills the
					// whole fall column during SetEffect, so rebuilds on
					// intensity-bucket changes don't visibly reset. No
					// external Trigger() top-up either - the old per-frame
					// Trigger spam was frame-rate coupled and burst-spawned
					// on preset switches ("flurry that settles after a few
					// seconds"); emission.rate now carries the full load.
					particleComponent->SetEffect(_precipitationEffect);
					particleComponent->Reset();
				}
				particleComponent->Play();
			}
		}

		for (size_t i = 1; i < _precipitationEntities.size(); ++i)
			CleanupHelperEntity(_precipitationEntities[i]);
	}

	void WeatherControllerComponent::UpdateLightning(Scene* scene, Camera* camera, const WeatherState& currentState, const WeatherState& targetState, float frameTime)
	{
		if (scene == nullptr)
			return;

		const bool allowLightning = targetState.enableLightning;
		const bool lightningActive = allowLightning || _lightningRemaining > 0.0f || _lightningFlash > 0.0f;
		if (!lightningActive)
		{
			_lightningCooldown = 0.0f;
			_lightningRemaining = 0.0f;
			_lightningClusterDuration = 0.0f;
			_lightningActivePulseIndex = 0;
			_lightningPulseCount = 0;
			_lightningFlash = 0.0f;
			_lightningBoltProgress = 0.0f;
			return;
		}

		const WeatherState& lightningState = allowLightning ? targetState : currentState;

		_lightningCooldown -= frameTime;
		if (_lightningRemaining > 0.0f)
		{
			_lightningRemaining = std::max(0.0f, _lightningRemaining - frameTime);
			const float clusterDuration = std::max(0.01f, _lightningClusterDuration);
			const float clusterElapsed = clusterDuration - _lightningRemaining;
			float activeFlash = 0.0f;
			float activeProgress = 1.0f;
			float activeContribution = 0.0f;
			uint32_t activePulseIndex = 0;

			for (uint32_t i = 0; i < _lightningPulseCount; ++i)
			{
				const float pulseOffset = _lightningPulseOffsets[i];
				const float pulseDuration = std::max(0.01f, _lightningPulseDurations[i]);
				const float localTime = clusterElapsed - pulseOffset;
				if (localTime < 0.0f || localTime > pulseDuration)
					continue;

				const float pulseProgress = std::clamp(localTime / pulseDuration, 0.0f, 1.0f);
				float flashEnvelope = 1.0f;
				if (pulseProgress > 0.12f)
				{
					const float fadeT = std::clamp((pulseProgress - 0.12f) / 0.88f, 0.0f, 1.0f);
					const float smoothFade = fadeT * fadeT * (3.0f - 2.0f * fadeT);
					flashEnvelope = 1.0f - smoothFade;
				}

				const float pulseFlash = _lightningPulseIntensities[i] * flashEnvelope;
				if (pulseFlash > activeFlash)
					activeFlash = pulseFlash;
				if (pulseFlash >= activeContribution)
				{
					activeContribution = pulseFlash;
					activeProgress = pulseProgress;
					activePulseIndex = i;
				}
			}

			_lightningActivePulseIndex = activePulseIndex;
			_lightningFlash = activeFlash;
			_lightningBoltProgress = activeProgress;
			_lightningBoltSeed = _lightningBoltBaseSeed + _lightningPulseSeedOffsets[activePulseIndex];
			_lightningBoltWidth = _lightningBoltBaseWidth * _lightningPulseWidthScales[activePulseIndex];
			_lightningBoltBranching = _lightningPulseBranchings[activePulseIndex];
			_lightningBoltDirection = _lightningBoltBaseDirection + _lightningPulseDirectionOffsets[activePulseIndex];
			if (_lightningBoltDirection.LengthSquared() > 0.0001f)
				_lightningBoltDirection.Normalize();
			else
				_lightningBoltDirection = _lightningBoltBaseDirection;
		}
		else
		{
			_lightningFlash = 0.0f;
			_lightningBoltProgress = 1.0f;
		}

		if (allowLightning && _lightningCooldown <= 0.0f)
		{
			_lightningCooldown = RandomRange(lightningState.lightningIntervalMin, lightningState.lightningIntervalMax);
			_lightningBoltBaseSeed = RandomRange(0.0f, 1024.0f);
			_lightningBoltBaseWidth = RandomRange(0.0065f, 0.0145f);
			_lightningBoltBaseDirection = math::Vector3(
				RandomRange(-1.0f, 1.0f),
				RandomRange(0.25f, 0.62f),
				RandomRange(-1.0f, 1.0f));
			if (_lightningBoltBaseDirection.LengthSquared() > 0.0001f)
				_lightningBoltBaseDirection.Normalize();
			_lightningBoltDirection = _lightningBoltBaseDirection;

			_lightningPulseCount = static_cast<uint32_t>(std::clamp(static_cast<int32_t>(RandomRange(2.0f, 4.99f)), 2, 4));
			float pulseCursor = 0.0f;
			for (uint32_t i = 0; i < _lightningPulseCount; ++i)
			{
				if (i > 0)
					pulseCursor += RandomRange(0.018f, 0.085f);

				_lightningPulseOffsets[i] = pulseCursor;
				_lightningPulseDurations[i] = std::max(0.012f, lightningState.lightningDuration * RandomRange(0.30f, 0.80f));
				_lightningPulseIntensities[i] = lightningState.lightningIntensity * (i == 0 ? RandomRange(0.82f, 1.0f) : RandomRange(0.28f, 0.92f));
				_lightningPulseSeedOffsets[i] = RandomRange(-24.0f, 24.0f) + static_cast<float>(i) * RandomRange(7.5f, 18.0f);
				_lightningPulseWidthScales[i] = (i == 0) ? 1.0f : RandomRange(0.82f, 1.18f);
				_lightningPulseBranchings[i] = RandomRange(0.34f, 0.78f);
				_lightningPulseDirectionOffsets[i] = math::Vector3(
					RandomRange(-0.14f, 0.14f),
					RandomRange(-0.014f, 0.014f),
					RandomRange(-0.14f, 0.14f));
				pulseCursor += _lightningPulseDurations[i];
			}
			for (uint32_t i = _lightningPulseCount; i < _lightningPulseOffsets.size(); ++i)
			{
				_lightningPulseOffsets[i] = 0.0f;
				_lightningPulseDurations[i] = 0.0f;
				_lightningPulseIntensities[i] = 0.0f;
				_lightningPulseSeedOffsets[i] = 0.0f;
				_lightningPulseWidthScales[i] = 1.0f;
				_lightningPulseBranchings[i] = 0.55f;
				_lightningPulseDirectionOffsets[i] = math::Vector3::Zero;
			}

			_lightningClusterDuration = std::max(0.01f, pulseCursor);
			_lightningRemaining = _lightningClusterDuration;
			_lightningActivePulseIndex = 0;
			_lightningBoltSeed = _lightningBoltBaseSeed + _lightningPulseSeedOffsets[0];
			_lightningBoltWidth = _lightningBoltBaseWidth * _lightningPulseWidthScales[0];
			_lightningBoltBranching = _lightningPulseBranchings[0];
			_lightningBoltDirection = _lightningBoltBaseDirection + _lightningPulseDirectionOffsets[0];
			if (_lightningBoltDirection.LengthSquared() > 0.0001f)
				_lightningBoltDirection.Normalize();
			else
				_lightningBoltDirection = _lightningBoltBaseDirection;
			_lightningFlash = _lightningPulseIntensities[0];
			_lightningBoltProgress = 0.0f;
			TriggerThunderOneShot();
		}
	}

	void WeatherControllerComponent::UpdateAudio(Scene* scene, Camera* camera, const WeatherState& currentState, const WeatherState& targetState, float frameTime)
	{
		if (g_pEnv == nullptr || g_pEnv->_audioManager == nullptr)
			return;

		const bool targetAllowsThunder = targetState.enableLightning;
		const float remainingTransitionSeconds = std::max(0.0f, _transitionDuration - _transitionElapsed);
		const float thunderExitFade =
			targetAllowsThunder ? 1.0f :
			(_transitionDuration > 0.0f ? std::clamp(remainingTransitionSeconds / std::max(0.01f, _transitionDuration), 0.0f, 1.0f) : 0.0f);

		_activeThunderSounds.erase(
			std::remove_if(_activeThunderSounds.begin(), _activeThunderSounds.end(),
				[&](ActiveThunderSoundRuntime& active)
				{
					if (active.sound == nullptr || !active.sound->IsPlaying())
						return true;

					if (thunderExitFade < 0.999f)
					{
						active.sound->SetPitch(active.basePitch + (_indoorThunderPitchOffset * (1.0f - _outdoorExposure)));
						active.sound->SetVolume(active.baseVolume * thunderExitFade * (_indoorThunderVolumeScale + ((1.0f - _indoorThunderVolumeScale) * _outdoorExposure)));
						if (thunderExitFade <= 0.001f)
						{
							g_pEnv->_audioManager->Stop(active.sound);
							return true;
						}
					}

					return false;
				}),
			_activeThunderSounds.end());

		float targetOutdoorExposure = 1.0f;
		if (scene != nullptr && camera != nullptr && camera->GetEntity() != nullptr)
		{
			const math::Vector3 listenerPosition = camera->GetEntity()->GetPosition();
			RayHit skyHit;
			const bool blocked = PhysUtils::RayCast(
				listenerPosition,
				listenerPosition + math::Vector3::Up * std::max(50.0f, _skyProbeDistance),
				LAYERMASK(Layer::StaticGeometry),
				&skyHit,
				{ camera->GetEntity() });
			targetOutdoorExposure = blocked ? 0.0f : 1.0f;
		}
		const float exposureBlend = std::clamp(frameTime * 2.0f, 0.0f, 1.0f);
		_outdoorExposure = std::clamp(_outdoorExposure + (targetOutdoorExposure - _outdoorExposure) * exposureBlend, 0.0f, 1.0f);

		const WeatherState& audibleState = (targetState.transitionSeconds > 0.0f) ? currentState : targetState;

		for (size_t i = 0; i < _loopAudio.size(); ++i)
		{
			const auto slot = static_cast<WeatherAudioLoopSlot>(i);
			UpdateLoopSound(slot, ComputeDesiredLoopGain(slot, audibleState), frameTime);
		}

		if (!targetAllowsThunder && thunderExitFade <= 0.001f)
		{
			_pendingThunderDelay = -1.0f;
		}

		if (_pendingThunderDelay >= 0.0f)
		{
			_pendingThunderDelay -= frameTime;
			if (_pendingThunderDelay <= 0.0f)
			{
				if (_thunderSound != nullptr)
				{
					const float thunderExposure = _indoorThunderVolumeScale + ((1.0f - _indoorThunderVolumeScale) * _outdoorExposure);
					const float thunderVolume = _pendingThunderVolume * thunderExposure * thunderExitFade;
					const float thunderPitch = _pendingThunderPitch + (_indoorThunderPitchOffset * (1.0f - _outdoorExposure));
					if (thunderVolume > 0.001f)
					{
						if (auto thunderInstance = _thunderSound->CreatePlaybackClone(); thunderInstance != nullptr)
						{
							thunderInstance->SetRadius(_thunderSound->GetRadius());
							thunderInstance->SetPitch(thunderPitch);
							thunderInstance->SetVolume(thunderVolume);
							g_pEnv->_audioManager->Play(thunderInstance, _pendingThunderPosition);
							_activeThunderSounds.push_back({ thunderInstance, thunderVolume, thunderPitch });
						}
					}
				}

				_pendingThunderDelay = -1.0f;
			}
		}
	}

	float WeatherControllerComponent::ComputeDesiredLoopGain(WeatherAudioLoopSlot slot, const WeatherState& state) const
	{
		const float rainHeavyBlend = std::clamp((state.precipitationIntensity - 0.55f) / 0.40f, 0.0f, 1.0f);
		const float windBed = std::clamp((state.windSpeed - 10.0f) / 32.0f, 0.0f, 1.0f);
		const bool isBlizzard = state.presetId == WeatherPresetId::Blizzard || (state.precipitationType == WeatherPrecipitationType::Snow && state.windSpeed >= 30.0f);
		const bool isStorm = state.presetId == WeatherPresetId::Storm || state.presetId == WeatherPresetId::Thunderstorm;
		const float rainWindBlend = (state.precipitationType == WeatherPrecipitationType::Rain) ? std::clamp((state.precipitationIntensity - 0.20f) / 0.80f, 0.0f, 1.0f) : 0.0f;
		const float stormWindLift = isStorm ? 0.35f : 0.0f;

		switch (slot)
		{
		case WeatherAudioLoopSlot::Rain:
			return state.precipitationType == WeatherPrecipitationType::Rain ? state.precipitationIntensity * (1.0f - rainHeavyBlend) : 0.0f;
		case WeatherAudioLoopSlot::HeavyRain:
			return state.precipitationType == WeatherPrecipitationType::Rain ? state.precipitationIntensity * rainHeavyBlend : 0.0f;
		case WeatherAudioLoopSlot::Snow:
			return state.precipitationType == WeatherPrecipitationType::Snow ? state.precipitationIntensity * (isBlizzard ? 0.35f : 0.85f) : 0.0f;
		case WeatherAudioLoopSlot::Wind:
			if (state.precipitationType == WeatherPrecipitationType::Sand)
				return windBed * 0.35f;
			return std::clamp(windBed * (0.28f + rainWindBlend * 0.42f) + stormWindLift, 0.0f, 1.0f);
		case WeatherAudioLoopSlot::BlizzardWind:
			return isBlizzard ? std::max(state.precipitationIntensity, windBed) : 0.0f;
		case WeatherAudioLoopSlot::Sandstorm:
			return state.precipitationType == WeatherPrecipitationType::Sand ? std::max(state.precipitationIntensity, windBed) : 0.0f;
		default:
			return 0.0f;
		}
	}

	void WeatherControllerComponent::EnsureLoopSoundLoaded(WeatherAudioLoopSlot slot)
	{
		auto* audioManager = (g_pEnv != nullptr) ? g_pEnv->_audioManager : nullptr;
		const size_t idx = static_cast<size_t>(slot);
		auto& runtime = _loopAudioRuntime[idx];
		const auto& config = _loopAudio[idx];
		if (config.assetPath.empty())
		{
			if (audioManager != nullptr && runtime.sound != nullptr && runtime.looping)
				audioManager->Stop(runtime.sound);
			runtime.sound.reset();
			runtime.loadedAssetPath.clear();
			runtime.currentGain = 0.0f;
			runtime.looping = false;
			return;
		}

		if (runtime.sound == nullptr || runtime.loadedAssetPath != config.assetPath)
		{
			if (audioManager != nullptr && runtime.sound != nullptr && runtime.looping)
				audioManager->Stop(runtime.sound);
			runtime.sound = SoundEffect::Create(config.assetPath);
			runtime.loadedAssetPath = config.assetPath;
			runtime.currentGain = 0.0f;
			runtime.looping = false;
		}
	}

	void WeatherControllerComponent::UpdateLoopSound(WeatherAudioLoopSlot slot, float desiredGain, float frameTime)
	{
		if (g_pEnv == nullptr || g_pEnv->_audioManager == nullptr)
			return;

		const size_t idx = static_cast<size_t>(slot);
		auto& runtime = _loopAudioRuntime[idx];
		const auto& config = _loopAudio[idx];
		EnsureLoopSoundLoaded(slot);
		if (runtime.sound == nullptr)
			return;

		const float remainingTransitionSeconds = std::max(0.0f, _transitionDuration - _transitionElapsed);
		const float targetVolume = std::max(0.0f, desiredGain) * std::max(0.0f, config.volume);
		const bool fadingIn = targetVolume > runtime.currentGain;
		float fadeSeconds = fadingIn ? config.fadeInSeconds : config.fadeOutSeconds;
		if (!fadingIn && remainingTransitionSeconds > 0.0f)
			fadeSeconds = remainingTransitionSeconds;
		fadeSeconds = std::max(0.01f, fadeSeconds);
		const float fadeStep = frameTime / fadeSeconds;
		runtime.currentGain = std::clamp(runtime.currentGain + (targetVolume - runtime.currentGain) * std::min(1.0f, fadeStep), 0.0f, std::max(0.0f, config.volume));

		const float exposedPitch = config.pitch;
		const float exposedVolume = runtime.currentGain * (_indoorWeatherVolumeScale + ((1.0f - _indoorWeatherVolumeScale) * _outdoorExposure));
		runtime.sound->SetPitch(exposedPitch);
		runtime.sound->SetVolume(exposedVolume);

		if (targetVolume > 0.001f)
		{
			if (!runtime.looping || !runtime.sound->IsPlaying())
			{
				g_pEnv->_audioManager->Loop(runtime.sound);
				runtime.looping = true;
			}
		}
		else if (runtime.looping && runtime.currentGain <= 0.001f)
		{
			g_pEnv->_audioManager->Stop(runtime.sound);
			runtime.looping = false;
		}
	}

	void WeatherControllerComponent::TriggerThunderOneShot()
	{
		if (g_pEnv == nullptr || g_pEnv->_audioManager == nullptr)
			return;

		std::vector<size_t> availableThunder;
		for (size_t i = 0; i < _thunderAudio.size(); ++i)
		{
			if (!_thunderAudio[i].assetPath.empty())
				availableThunder.push_back(i);
		}
		if (availableThunder.empty())
			return;

		const size_t chosenIndex = availableThunder[static_cast<size_t>(RandomRange(0.0f, static_cast<float>(availableThunder.size()) - 0.001f))];
		const auto& thunder = _thunderAudio[chosenIndex];
		const float horizonFactor = 1.0f - std::clamp(_lightningBoltBaseDirection.y, 0.0f, 1.0f);
		_pendingThunderDelay = 0.18f + horizonFactor * RandomRange(0.35f, 1.10f);
		_pendingThunderVolume = std::max(0.0f, thunder.volume) * RandomRange(0.82f, 1.0f);
		_pendingThunderPitch = RandomRange(
			std::min(thunder.pitchMin, thunder.pitchMax),
			std::max(thunder.pitchMin, thunder.pitchMax));
		const float thunderDistance = RandomRange(std::min(thunder.minDistance, thunder.maxDistance), std::max(thunder.minDistance, thunder.maxDistance));
		const math::Vector3 anchorPosition = (g_pEnv->_sceneManager != nullptr && g_pEnv->_sceneManager->GetCurrentScene() != nullptr && g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera() != nullptr && g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera()->GetEntity() != nullptr)
			? g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera()->GetEntity()->GetPosition()
			: (GetEntity() != nullptr ? GetEntity()->GetPosition() : math::Vector3::Zero);
		_pendingThunderPosition = anchorPosition + (_lightningBoltBaseDirection * thunderDistance);

		if (_thunderLoadedAssetPath != thunder.assetPath || _thunderSound == nullptr)
		{
			_thunderSound = SoundEffect::Create(thunder.assetPath);
			_thunderLoadedAssetPath = thunder.assetPath;
		}
		if (_thunderSound != nullptr)
			_thunderSound->SetRadius(thunder.radius);
	}

	void WeatherControllerComponent::CleanupHelperEntity(Entity*& entity)
	{
		if (entity == nullptr)
			return;

		entity->DeleteMe();
		entity = nullptr;
	}

	Entity* WeatherControllerComponent::EnsurePrecipitationEntity(Scene* scene, size_t index)
	{
		if (index >= _precipitationEntities.size())
			return nullptr;

		if (_precipitationEntities[index] != nullptr && !_precipitationEntities[index]->IsPendingDeletion())
			return _precipitationEntities[index];

		_precipitationEntities[index] = scene->CreateEntity(std::format("WeatherPrecipitation{}", index));
		if (_precipitationEntities[index] == nullptr)
			return nullptr;

		_precipitationEntities[index]->SetLayer(Layer::Particle);
		_precipitationEntities[index]->SetFlag(EntityFlags::DoNotSave);
		auto* particleComponent = _precipitationEntities[index]->AddComponent<ParticleSystemComponent>();
		if (particleComponent != nullptr)
			particleComponent->SetReceiveLighting(true);
		return _precipitationEntities[index];
	}

	std::shared_ptr<ParticleEffect> WeatherControllerComponent::BuildParticleEffect(const WeatherState& state) const
	{
		auto effect = std::make_shared<ParticleEffect>();
		effect->name = "RuntimeWeatherEffect";
		effect->emitters.clear();

		// Intensity is bucketed by BuildPrecipitationSignature, so the rates
		// baked here stay valid until the next bucket boundary.
		const float intensity = std::max(0.05f, state.precipitationIntensity);
		const math::Vector3 planarWind = math::Vector3(-state.windDirection.x, 0.0f, -state.windDirection.z) * state.windSpeed;

		// Spawn volume: a box CENTRED on the camera (the controller parks the
		// helper at eye + height*0.35 and the Y half-extent is height*0.65,
		// so the volume runs from below eye level to the authored ceiling).
		// XZ is intentionally smaller than the authored area radius: a 1-2cm
		// particle is sub-pixel beyond ~25m anyway, so spending budget there
		// thins the close-range look for zero visual return. Distant weather
		// reads through the fog medium, not through particles.
		const float xzRadius = std::min(state.precipitationAreaRadius * 0.6f, 26.0f);
		const float yHalf = std::max(10.0f, state.precipitationHeight * 0.65f);

		auto pushEmitter = [&effect](ParticleEmitterDesc emitter)
		{
			effect->emitters.push_back(std::move(emitter));
		};

		ParticleEmitterDesc primary;
		primary.name = "WeatherPrimary";
		primary.simulateInLocalSpace = false;
		// Receive lighting: with the froxel scatter volume bound, lit
		// particles sample it as a 3D light probe (shadow-tested sun +
		// shadow-tested local lights + emissive glow + ambient in a single
		// tap - see ParticleBillboardLit.shader). Weather was previously
		// UNLIT, which made every flake/drop render at its authored colour
		// regardless of the light environment: full-brightness white snow
		// at midnight, invisible streetlamp interaction.
		primary.overrideReceiveLightingEnabled = true;
		primary.overrideReceiveLighting = true;
		primary.blendMode = ParticleBlendMode::AlphaBlended;
		primary.renderMode = ParticleRenderMode::Billboard;
		primary.facingMode = ParticleFacingMode::CameraFacing;
		primary.shape.type = ParticleShapeType::Box;
		primary.shape.boxExtents = math::Vector3(xzRadius, yHalf, xzRadius);
		primary.softParticles = true;
		// Prewarm fills the entire fall column at effect (re)build. Without
		// it every preset change spawned one synchronised generation that
		// fell, died together (visible gap), then slowly decohered - the
		// "initial flurry that settles" artefact.
		primary.prewarm = true;
		primary.sizeOverLifetime = math::Vector2(1.0f, 1.0f);
		// Fade IN then OUT over life via the three-point alpha curve (0 -> 1 ->
		// 0) so particles don't POP into existence at spawn or vanish at death
		// - worst on the big soft mist puffs, which the user singled out. Each
		// emitter below keeps a CONSTANT colour alpha (start.w == end.w = its
		// peak) and lets this curve own the entire fade shape. The midpoint is
		// where the peak sits in normalised life: 0.25 = quick fade-in, long
		// fade-out (natural for falling precip that's already moving when it
		// enters view). Inherited by every weather emitter via the copies.
		primary.useThreePointAlphaCurve = true;
		primary.alphaOverLifetimeCurve = math::Vector3(0.0f, 1.0f, 0.0f);
		primary.alphaOverLifetimeCurveMidpoint = 0.25f;
		primary.materialPath = "EngineData.Materials/Billboard.hmat";
		primary.texturePath = "EngineData.Textures/white.png";

		switch (state.precipitationType)
		{
		case WeatherPrecipitationType::Rain:
		{
			// Rain falls at TERMINAL VELOCITY (~11 m/s), not under runaway
			// gravity. The old gravity of -150 accelerated drops past
			// 150 m/s, which made density proportional to 1/speed: fresh
			// slow drops bunched up in the spawn box ("dense layer above")
			// while the drops at eye level streaked past sparsely. With
			// gravity balanced against linear drag (v_t = g/drag = 22/2.0 =
			// 11 m/s, reached in ~0.5s), the fall column has uniform density
			// top to bottom. Rendered as velocity-stretched streaks ~2cm
			// wide instead of 4-6cm camera-facing blobs.
			primary.renderMode = ParticleRenderMode::StretchedBillboard;
			primary.facingMode = ParticleFacingMode::VelocityAligned;
			primary.gravity = math::Vector3(0.0f, -22.0f, 0.0f);
			primary.drag = 2.0f;
			primary.speedRange = math::Vector2(0.0f, 0.3f);
			primary.sizeRange = math::Vector2(0.014f, 0.024f);
			primary.lifetimeRange = math::Vector2(2.6f, 3.2f);
			// Lateral wind at terminal slant = force/drag: 0.6x wind over
			// drag 2.0 gives ~4-6 m/s sideways drift in storm winds.
			primary.constantForce = math::Vector3(planarWind.x * 0.6f, 0.0f, planarWind.z * 0.6f);
			primary.noiseAmplitude = 0.06f;
			// Constant alpha across life; the inherited three-point curve does
			// the fade in/out (see base primary).
			primary.startColor = math::Vector4(0.78f, 0.86f, 0.96f, 0.55f);
			primary.endColor = math::Vector4(0.78f, 0.86f, 0.96f, 0.55f);
			primary.alphaOverLifetime = math::Vector2(1.0f, 0.85f);
			// Steady-state count = rate * lifetime; cap with headroom.
			primary.emission.rate = std::max(600.0f, intensity * 11000.0f);
			primary.emission.prewarmTime = 3.2f;
			primary.maxParticles = static_cast<uint32_t>(primary.emission.rate * 3.4f) + 1024u;
			primary.texturePath = "EngineData.Textures/Particles/WeatherRainDrop.png";
			pushEmitter(primary);

			// Mist is drifting VAPOUR - it must read as CONTINUOUS churning
			// haze, never as individual sprites. The previous tuning (0.6-1.3m
			// puffs drifting on the wind vector) failed that test: the puffs
			// were small enough to pick out individually and they all marched
			// along the same near-linear wind direction, so the eye tracked a
			// moving grid of dots sweeping across the scene - the "disco ball"
			// read. Three things fix it together:
			//   - BIG puffs (2-4.5m) so 3-4 always overlap at any screen point
			//     and no single one is resolvable
			//   - LOW per-puff alpha (0.035) so the overlap integrates into
			//     smooth haze instead of stacking into hot blobs
			//   - STRONG coherent-gust swirl + low net wind so they churn and
			//     tumble in place rather than streaming in formation
			ParticleEmitterDesc mist = primary;
			mist.name = "RainMist";
			mist.renderMode = ParticleRenderMode::Billboard;
			mist.facingMode = ParticleFacingMode::CameraFacing;
			mist.emission.rate = std::max(90.0f, intensity * 650.0f);
			mist.emission.prewarmTime = 5.5f;
			mist.maxParticles = static_cast<uint32_t>(mist.emission.rate * 6.0f) + 256u;
			mist.shape.boxExtents = math::Vector3(xzRadius * 0.85f, yHalf, xzRadius * 0.85f);
			mist.speedRange = math::Vector2(0.1f, 0.5f);
			mist.sizeRange = math::Vector2(2.0f, 4.5f);
			mist.lifetimeRange = math::Vector2(4.5f, 6.0f);
			// Constant alpha + a GENTLER, more symmetric fade than the falling
			// layers: mist hangs and churns, so a slow breathe in AND out
			// (midpoint 0.4) reads far better than a pop. This is the layer the
			// user singled out as jarring.
			mist.startColor = math::Vector4(0.72f, 0.80f, 0.88f, 0.035f);
			mist.endColor = math::Vector4(0.72f, 0.80f, 0.88f, 0.035f);
			mist.alphaOverLifetimeCurveMidpoint = 0.4f;
			mist.gravity = math::Vector3(0.0f, -0.4f, 0.0f);
			mist.drag = 4.0f;
			// Net wind kept LOW (0.35x) so the haze hangs and churns rather
			// than streaming past; the swirl comes from the coherent gust
			// field instead. Amplitude 5.0 is an accel fighting drag 4.0 =
			// ~1.25 m/s of shared eddy motion, with big lazy cells (freq
			// 0.18 -> ~10-25m) so neighbouring puffs move together as a
			// churning mass, not as independent flecks.
			mist.constantForce = math::Vector3(planarWind.x * 0.35f, 0.0f, planarWind.z * 0.35f);
			mist.noiseAmplitude = 5.0f;
			mist.noiseFrequency = 0.18f;
			mist.rotationSpeedRange = math::Vector2(0.0f, 0.0f);
			mist.texturePath = "EngineData.Textures/Particles/WeatherSoftPuff.png";
			pushEmitter(mist);
			break;
		}
		case WeatherPrecipitationType::Snow:
		{
			// Snow drifts at ~1.6 m/s terminal (g/drag = 9.81/6.0); WIND does
			// the blizzard work - at the blizzard preset's 24 m/s wind the
			// lateral terminal speed is ~12 m/s, near-horizontal driving snow.
			//
			// Velocity-aligned + motion-stretched: at blizzard speeds each
			// flake draws as a ~15cm streak (the universal visual shorthand
			// for driving snow), while calm-snowfall speeds (~2 m/s total)
			// stretch barely past the quad size and stay flake-like. One
			// mode covers both ends of the preset range.
			primary.renderMode = ParticleRenderMode::StretchedBillboard;
			primary.facingMode = ParticleFacingMode::VelocityAligned;
			primary.gravity = math::Vector3(0.0f, -9.81f, 0.0f);
			primary.drag = 6.0f;
			primary.speedRange = math::Vector2(0.0f, 0.25f);
			// Soft white SPECKS, not snowflake glyphs: the dendrite sprite
			// reads as cartoon clip-art whenever a flake passes near the
			// camera (giant ❄ icons filling the screen). Real snow beyond
			// arm's length is a soft dot - the smoke puff at small sizes
			// gives exactly that, and the velocity stretch turns it into a
			// clean streak under blizzard wind.
			primary.sizeRange = math::Vector2(0.025f, 0.045f);
			primary.lifetimeRange = math::Vector2(8.0f, 11.0f);
			primary.constantForce = math::Vector3(planarWind.x * 3.0f, 0.0f, planarWind.z * 3.0f);
			// Coherent gust field (see ParticleSimulate.shader): amplitude
			// is an acceleration fighting drag 6, so 7.0 gives ~1.2 m/s of
			// SHARED swirl on top of the mean wind - flakes veer and eddy
			// together in travelling gust cells instead of streaming in
			// dead-straight lines. Frequency 0.35 = cells roughly 5-15m.
			primary.noiseAmplitude = 7.0f;
			primary.noiseFrequency = 0.35f;
			// Smoke sprite carries its own alpha falloff, so drive start
			// alpha to full - the texture does the softening.
			// Constant alpha; inherited three-point curve fades in/out.
			primary.startColor = math::Vector4(0.96f, 0.98f, 1.0f, 0.9f);
			primary.endColor = math::Vector4(0.96f, 0.98f, 1.0f, 0.9f);
			primary.alphaOverLifetime = math::Vector2(1.0f, 0.6f);
			// Concentrate the spawn volume: flakes past ~18m are 1-3px and
			// effectively invisible, so budget spent there only THINS the
			// close-range look. A tighter box at double the rate is what
			// makes a blizzard read as a wall of snow (~3 flakes/m3) rather
			// than scattered dots (~0.5/m3 over the full radius).
			const float snowXz = std::min(xzRadius, 18.0f);
			primary.shape.boxExtents = math::Vector3(snowXz, yHalf, snowXz);
			primary.emission.rate = std::max(400.0f, intensity * 7200.0f);
			primary.emission.prewarmTime = 9.0f;
			primary.maxParticles = static_cast<uint32_t>(primary.emission.rate * 11.5f) + 1024u;
			primary.texturePath = "EngineData.Textures/Particles/WeatherSoftPuff.png";
			pushEmitter(primary);

			// Soft out-of-focus foreground flakes - bigger, dimmer, camera-
			// facing (these sell the "snow right in your face" layer).
			ParticleEmitterDesc drift = primary;
			drift.name = "SnowDrift";
			drift.renderMode = ParticleRenderMode::Billboard;
			drift.facingMode = ParticleFacingMode::CameraFacing;
			drift.emission.rate = std::max(150.0f, intensity * 1100.0f);
			drift.emission.prewarmTime = 9.0f;
			drift.maxParticles = static_cast<uint32_t>(drift.emission.rate * 11.5f) + 256u;
			drift.shape.boxExtents = math::Vector3(snowXz * 0.6f, yHalf, snowXz * 0.6f);
			drift.sizeRange = math::Vector2(0.06f, 0.10f);
			// Constant alpha; gentler fade like the mist (slow soft layer).
			drift.startColor = math::Vector4(0.90f, 0.94f, 1.0f, 0.30f);
			drift.endColor = math::Vector4(0.90f, 0.94f, 1.0f, 0.30f);
			drift.alphaOverLifetimeCurveMidpoint = 0.4f;
			drift.drag = 5.0f;
			// Foreground flakes get a stronger swirl - the close layer is
			// where chaotic motion is most visible.
			drift.noiseAmplitude = 9.0f;
			drift.noiseFrequency = 0.5f;
			drift.texturePath = "EngineData.Textures/Particles/WeatherSoftPuff.png";
			pushEmitter(drift);
			break;
		}
		case WeatherPrecipitationType::Sand:
		{
			// Sand is wind-borne, not falling: weak gravity, low drag and a
			// strong lateral force give ~12 m/s horizontal streaming at the
			// preset's 29 m/s wind (force/drag = 29*0.5/1.2). Grain quads
			// roughly halved from before - the thick-air feel comes from the
			// orange fog medium; particles just add motion.
			primary.gravity = math::Vector3(0.0f, -1.5f, 0.0f);
			primary.drag = 1.2f;
			primary.speedRange = math::Vector2(1.0f, 3.5f);
			primary.sizeRange = math::Vector2(0.22f, 0.50f);
			primary.lifetimeRange = math::Vector2(2.0f, 3.5f);
			primary.constantForce = math::Vector3(planarWind.x * 0.5f, 0.0f, planarWind.z * 0.5f);
			primary.noiseAmplitude = 0.9f;
			// Constant alpha; inherited three-point curve fades in/out.
			primary.startColor = math::Vector4(0.82f, 0.66f, 0.44f, 0.40f);
			primary.endColor = math::Vector4(0.82f, 0.66f, 0.44f, 0.40f);
			primary.emission.rate = std::max(120.0f, intensity * 1300.0f);
			primary.emission.prewarmTime = 3.5f;
			primary.maxParticles = static_cast<uint32_t>(primary.emission.rate * 3.8f) + 256u;
			// Dust clumps, not untextured quads - the base desc's white.png
			// renders as hard-edged squares. Smoke puffs tinted by the
			// start/end colour read as wind-borne dust.
			primary.texturePath = "EngineData.Textures/Particles/WeatherSoftPuff.png";
			primary.rotationSpeedRange = math::Vector2(-25.0f, 25.0f);
			pushEmitter(primary);

			ParticleEmitterDesc haze = primary;
			haze.name = "SandHaze";
			haze.emission.rate = std::max(20.0f, intensity * 180.0f);
			haze.emission.prewarmTime = 3.0f;
			haze.maxParticles = static_cast<uint32_t>(haze.emission.rate * 3.2f) + 128u;
			haze.shape.boxExtents = math::Vector3(xzRadius * 0.75f, yHalf * 0.6f, xzRadius * 0.75f);
			haze.speedRange = math::Vector2(0.5f, 1.8f);
			haze.sizeRange = math::Vector2(0.7f, 1.4f);
			haze.lifetimeRange = math::Vector2(1.8f, 3.0f);
			// Constant alpha; gentler fade like the mist (slow soft layer).
			haze.startColor = math::Vector4(0.74f, 0.57f, 0.35f, 0.20f);
			haze.endColor = math::Vector4(0.74f, 0.57f, 0.35f, 0.20f);
			haze.alphaOverLifetimeCurveMidpoint = 0.4f;
			haze.noiseAmplitude = 0.65f;
			haze.texturePath = "EngineData.Textures/Particles/WeatherSoftPuff.png";
			haze.rotationSpeedRange = math::Vector2(-10.0f, 10.0f);
			pushEmitter(haze);
			break;
		}
		case WeatherPrecipitationType::None:
		default:
			break;
		}

		return effect;
	}

}
