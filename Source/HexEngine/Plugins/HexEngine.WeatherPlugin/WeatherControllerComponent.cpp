#include "WeatherControllerComponent.hpp"

#include "WeatherZoneComponent.hpp"

#include <algorithm>
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
			return std::to_string(static_cast<int32_t>(state.precipitationType));
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

	void WeatherControllerComponent::Update(float frameTime)
	{
		UpdateComponent::Update(frameTime);

		Scene* scene = GetEntity()->GetScene();
		if (scene == nullptr || !_previewEnabled)
			return;

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
		_currentState = _globalState;
		_transitionSourceState = _globalState;
		_transitionTargetState = _globalState;
		_transitionElapsed = 0.0f;
		_transitionDuration = 0.0f;
	}

	bool WeatherControllerComponent::CreateWidget(ComponentWidget* widget)
	{
		auto* preview = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Preview Enabled", &_previewEnabled);
		preview->SetPrefabOverrideBinding(GetComponentName(), "/_previewEnabled");

		auto* preset = new DropDown(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Global Preset");
		PopulatePresetDropDown(preset, _globalPresetId, [this](WeatherPresetId presetId)
		{
			ApplyPreset(presetId);
		});

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
		const WeatherState& visibleState = currentActive ? currentState : targetState;

		const math::Vector3 anchorPosition =
			(camera != nullptr && camera->GetEntity() != nullptr) ? camera->GetEntity()->GetPosition() : GetEntity()->GetPosition();
		size_t helperCount = 1;
		if (effectState.precipitationIntensity >= 0.95f)
			helperCount = 7;
		else if (effectState.precipitationIntensity >= 0.8f)
			helperCount = 5;

		const std::string signature = BuildPrecipitationSignature(effectState) + ":" + std::to_string(helperCount);
		const bool effectChanged = (_precipitationEffect == nullptr || _precipitationEffectSignature != signature);
		if (effectChanged)
		{
			_precipitationEffect = BuildParticleEffect(effectState);
			_precipitationEffectSignature = signature;
		}

		const float offsetRadius = std::max(6.0f, effectState.precipitationAreaRadius * 0.35f);
		for (size_t i = 0; i < helperCount; ++i)
		{
			Entity* helper = EnsurePrecipitationEntity(scene, i);
			if (helper == nullptr)
				continue;

			math::Vector3 offset = math::Vector3::Zero;
			if (i == 1) offset = math::Vector3(offsetRadius, 0.0f, 0.0f);
			else if (i == 2) offset = math::Vector3(-offsetRadius, 0.0f, 0.0f);
			else if (i == 3) offset = math::Vector3(0.0f, 0.0f, offsetRadius);
			else if (i == 4) offset = math::Vector3(0.0f, 0.0f, -offsetRadius);
			else if (i == 5) offset = math::Vector3(offsetRadius * 0.72f, 0.0f, offsetRadius * 0.72f);
			else if (i == 6) offset = math::Vector3(-offsetRadius * 0.72f, 0.0f, -offsetRadius * 0.72f);

			helper->ForcePosition(anchorPosition + offset + math::Vector3(0.0f, effectState.precipitationHeight, 0.0f));
			auto* particleComponent = helper->GetComponent<ParticleSystemComponent>();
			if (particleComponent == nullptr)
				continue;

			if (effectChanged)
			{
				particleComponent->SetEffect(_precipitationEffect);
				particleComponent->Reset();
			}

			if (frameTime > 0.0f)
			{
				float sustainedSpawnRate = 0.0f;
				switch (effectState.precipitationType)
				{
				case WeatherPrecipitationType::Rain:
					sustainedSpawnRate = 26000.0f;
					break;
				case WeatherPrecipitationType::Snow:
					sustainedSpawnRate = 18000.0f;
					break;
				case WeatherPrecipitationType::Sand:
					sustainedSpawnRate = 7000.0f;
					break;
				case WeatherPrecipitationType::None:
				default:
					break;
				}

				if (sustainedSpawnRate > 0.0f)
				{
					const float sustainedIntensity = std::max(visibleState.precipitationIntensity, effectState.precipitationIntensity * 0.35f);
					const uint32_t extraTriggerCount = static_cast<uint32_t>(std::max(0.0f, (sustainedSpawnRate * sustainedIntensity * frameTime) / (float)helperCount));
					if (extraTriggerCount > 0)
						particleComponent->Trigger(extraTriggerCount);
				}
			}
			particleComponent->Play();
		}

		for (size_t i = helperCount; i < _precipitationEntities.size(); ++i)
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

		const float intensity = std::max(0.05f, state.precipitationIntensity);
		const math::Vector3 planarWind = math::Vector3(-state.windDirection.x, 0.0f, -state.windDirection.z) * state.windSpeed;

		auto pushEmitter = [&effect](ParticleEmitterDesc emitter)
		{
			effect->emitters.push_back(std::move(emitter));
		};

		ParticleEmitterDesc primary;
		primary.name = "WeatherPrimary";
		primary.simulateInLocalSpace = false;
		primary.overrideReceiveLightingEnabled = true;
		primary.overrideReceiveLighting = false;
		primary.blendMode = ParticleBlendMode::AlphaBlended;
		primary.renderMode = ParticleRenderMode::Billboard;
		primary.facingMode = ParticleFacingMode::VelocityAligned;
		primary.maxParticles = std::max(2400u, static_cast<uint32_t>(50000.0f * intensity));
		primary.emission.rate = std::max(120.0f, intensity * 35000.0f);
		primary.shape.type = ParticleShapeType::Box;
		primary.shape.boxExtents = math::Vector3(
			state.precipitationAreaRadius,
			std::max(12.0f, state.precipitationHeight * 0.85f),
			state.precipitationAreaRadius);
		primary.gravity = math::Vector3::Zero;
		primary.constantForce = planarWind * 0.45f;
		primary.noiseAmplitude = 0.15f * intensity;
		primary.noiseFrequency = 0.8f;
		primary.softParticles = true;
		primary.prewarm = false;
		primary.emission.prewarmTime = 0.08f;
		primary.lifetimeRange = math::Vector2(0.8f, 1.4f);
		primary.alphaOverLifetime = math::Vector2(1.0f, 0.25f);
		primary.useThreePointAlphaCurve = true;
		primary.alphaOverLifetimeCurve = math::Vector3(0.1f, 1.0f, 0.1f);
		primary.sizeOverLifetime = math::Vector2(1.0f, 0.85f);
		primary.materialPath = "EngineData.Materials/Billboard.hmat";
		primary.texturePath = "EngineData.Textures/white.png";

		switch (state.precipitationType)
		{
		case WeatherPrecipitationType::Rain:
		{
			primary.facingMode = ParticleFacingMode::CameraFacing;
			primary.maxParticles = std::max(12000u, static_cast<uint32_t>(52000.0f * intensity));
			primary.emission.rate = std::max(900.0f, intensity * 28000.0f);
			primary.speedRange = math::Vector2(0.0f, 0.45f);
			primary.sizeRange = math::Vector2(0.040f, 0.065f);
			primary.startColor = math::Vector4(0.78f, 0.86f, 0.96f, 0.72f);
			primary.endColor = math::Vector4(0.78f, 0.86f, 0.96f, 0.0f);
			primary.gravity = math::Vector3(0.0f, -150.0f, 0.0f);
			primary.constantForce = math::Vector3(planarWind.x * 1.35f, 0.0f, planarWind.z * 1.35f);
			primary.lifetimeRange = math::Vector2(1.8f, 2.7f);
			primary.alphaOverLifetime = math::Vector2(1.0f, 0.45f);
			primary.sizeOverLifetime = math::Vector2(1.0f, 0.75f);
			primary.texturePath = "EngineData.Textures/Particles/WeatherRainDrop.png";
			pushEmitter(primary);

			ParticleEmitterDesc mist = primary;
			mist.name = "RainMist";
			mist.facingMode = ParticleFacingMode::CameraFacing;
			mist.maxParticles = std::max(3600u, static_cast<uint32_t>(12000.0f * intensity));
			mist.emission.rate = std::max(260.0f, intensity * 3600.0f);
			mist.shape.boxExtents = math::Vector3(state.precipitationAreaRadius * 0.8f, 4.0f, state.precipitationAreaRadius * 0.8f);
			mist.speedRange = math::Vector2(1.0f, 4.0f);
			mist.sizeRange = math::Vector2(0.10f, 0.22f);
			mist.lifetimeRange = math::Vector2(1.0f, 1.8f);
			mist.startColor = math::Vector4(0.72f, 0.80f, 0.88f, 0.18f);
			mist.endColor = math::Vector4(0.72f, 0.80f, 0.88f, 0.0f);
			mist.gravity = math::Vector3(0.0f, -10.0f, 0.0f);
			mist.constantForce = math::Vector3(planarWind.x * 0.9f, 0.0f, planarWind.z * 0.9f);
			mist.noiseAmplitude = 0.25f;
			mist.texturePath = "EngineData.Textures/Particles/WeatherRainDrop.png";
			pushEmitter(mist);
			break;
		}
		case WeatherPrecipitationType::Snow:
		{
			primary.facingMode = ParticleFacingMode::CameraFacing;
			primary.maxParticles = std::max(10000u, static_cast<uint32_t>(30000.0f * intensity));
			primary.emission.rate = std::max(420.0f, intensity * 12500.0f);
			primary.speedRange = math::Vector2(0.0f, 0.25f);
			primary.sizeRange = math::Vector2(0.052f, 0.092f);
			primary.lifetimeRange = math::Vector2(5.8f, 8.8f);
			primary.startColor = math::Vector4(0.96f, 0.98f, 1.0f, 0.94f);
			primary.endColor = math::Vector4(0.96f, 0.98f, 1.0f, 0.0f);
			primary.gravity = math::Vector3(0.0f, -7.0f, 0.0f);
			primary.constantForce = math::Vector3(planarWind.x * 0.65f, 0.0f, planarWind.z * 0.65f);
			primary.alphaOverLifetime = math::Vector2(1.0f, 0.55f);
			primary.noiseAmplitude = 0.35f;
			primary.texturePath = "EngineData.Textures/Particles/WeatherSnowflake.png";
			pushEmitter(primary);

			ParticleEmitterDesc drift = primary;
			drift.name = "SnowDrift";
			drift.maxParticles = std::max(5200u, static_cast<uint32_t>(16000.0f * intensity));
			drift.emission.rate = std::max(180.0f, intensity * 4600.0f);
			drift.shape.boxExtents = math::Vector3(state.precipitationAreaRadius * 0.9f, 8.0f, state.precipitationAreaRadius * 0.9f);
			drift.speedRange = math::Vector2(0.2f, 0.9f);
			drift.sizeRange = math::Vector2(0.05f, 0.11f);
			drift.lifetimeRange = math::Vector2(6.2f, 9.2f);
			drift.startColor = math::Vector4(0.90f, 0.94f, 1.0f, 0.28f);
			drift.endColor = math::Vector4(0.90f, 0.94f, 1.0f, 0.0f);
			drift.constantForce = math::Vector3(planarWind.x * 0.95f, 0.0f, planarWind.z * 0.95f);
			drift.noiseAmplitude = 0.55f;
			drift.texturePath = "EngineData.Textures/Particles/WeatherSnowflake.png";
			pushEmitter(drift);
			break;
		}
		case WeatherPrecipitationType::Sand:
		{
			primary.facingMode = ParticleFacingMode::CameraFacing;
			primary.maxParticles = std::max(384u, static_cast<uint32_t>(2600.0f * intensity));
			primary.emission.rate = std::max(18.0f, intensity * 760.0f);
			primary.speedRange = math::Vector2(1.2f, 4.5f);
			primary.sizeRange = math::Vector2(0.45f, 1.0f);
			primary.lifetimeRange = math::Vector2(1.2f, 2.4f);
			primary.startColor = math::Vector4(0.82f, 0.66f, 0.44f, 0.46f);
			primary.endColor = math::Vector4(0.82f, 0.66f, 0.44f, 0.0f);
			primary.gravity = math::Vector3(0.0f, -2.5f, 0.0f);
			primary.constantForce = math::Vector3(planarWind.x * 1.25f, 0.0f, planarWind.z * 1.25f);
			primary.noiseAmplitude = 1.0f;
			pushEmitter(primary);

			ParticleEmitterDesc haze = primary;
			haze.name = "SandHaze";
			haze.maxParticles = std::max(256u, static_cast<uint32_t>(900.0f * intensity));
			haze.emission.rate = std::max(8.0f, intensity * 120.0f);
			haze.shape.boxExtents = math::Vector3(state.precipitationAreaRadius * 0.75f, 3.0f, state.precipitationAreaRadius * 0.75f);
			haze.speedRange = math::Vector2(0.5f, 1.8f);
			haze.sizeRange = math::Vector2(0.8f, 1.6f);
			haze.lifetimeRange = math::Vector2(1.4f, 3.0f);
			haze.startColor = math::Vector4(0.74f, 0.57f, 0.35f, 0.26f);
			haze.endColor = math::Vector4(0.74f, 0.57f, 0.35f, 0.0f);
			haze.noiseAmplitude = 0.65f;
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
