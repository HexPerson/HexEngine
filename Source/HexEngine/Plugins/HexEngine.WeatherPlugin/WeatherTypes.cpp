#include "WeatherTypes.hpp"

#include <algorithm>

namespace HexEngine::Weather
{
	namespace
	{
		template <typename T>
		T LerpValue(const T& a, const T& b, float alpha)
		{
			return static_cast<T>(a + ((b - a) * alpha));
		}
	}

	const wchar_t* GetPresetDisplayName(WeatherPresetId presetId)
	{
		switch (presetId)
		{
		case WeatherPresetId::Clear: return L"Clear";
		case WeatherPresetId::Overcast: return L"Overcast";
		case WeatherPresetId::Rain: return L"Rain";
		case WeatherPresetId::HeavyRain: return L"Heavy Rain";
		case WeatherPresetId::Storm: return L"Storm";
		case WeatherPresetId::Thunderstorm: return L"Thunderstorm";
		case WeatherPresetId::Snow: return L"Snow";
		case WeatherPresetId::Blizzard: return L"Blizzard";
		case WeatherPresetId::Hot: return L"Hot";
		case WeatherPresetId::Sandstorm: return L"Sandstorm";
		case WeatherPresetId::Custom:
		default: return L"Custom";
		}
	}

	const wchar_t* GetPrecipitationDisplayName(WeatherPrecipitationType type)
	{
		switch (type)
		{
		case WeatherPrecipitationType::Rain: return L"Rain";
		case WeatherPrecipitationType::Snow: return L"Snow";
		case WeatherPrecipitationType::Sand: return L"Sand";
		case WeatherPrecipitationType::None:
		default: return L"None";
		}
	}

	const wchar_t* GetZoneShapeDisplayName(WeatherZoneShape shape)
	{
		switch (shape)
		{
		case WeatherZoneShape::Box: return L"Box";
		case WeatherZoneShape::Sphere:
		default: return L"Sphere";
		}
	}

	WeatherState MakePresetState(WeatherPresetId presetId)
	{
		WeatherState state;
		state.presetId = presetId;

		switch (presetId)
		{
		case WeatherPresetId::Clear:
			state.cloudCoverage = 0.18f;
			state.cloudDensity = 0.55f;
			state.cloudErosion = 0.45f;
			state.windSpeed = 18.0f;
			state.fogDensity = 0.0012f;
			state.fogHeightDensity = 0.0010f;
			state.surface.temperatureBias = 0.2f;
			break;
		case WeatherPresetId::Overcast:
			state.zenithExponent = 2.2f;
			state.anisotropicIntensity = 0.16f;
			state.atmosphereDensity = 0.16f;
			state.cloudCoverage = 0.82f;
			state.cloudDensity = 1.45f;
			state.cloudAmbientStrength = 0.22f;
			state.cloudViewAbsorption = 0.70f;
			state.cloudShadowStrength = 0.82f;
			state.sunIntensity = 0.22f;
			state.sunColour = math::Color(0.68f, 0.71f, 0.76f, 1.0f);
			state.fogColour = math::Color(0.50f, 0.53f, 0.58f, 1.0f);
			state.ambientLight = math::Vector4(0.16f, 0.17f, 0.19f, 1.0f);
			state.ambientSkyStrength = 0.24f;
			state.rayleighStrength = 0.34f;
			state.mieStrength = 1.65f;
			state.sunHazeStrength = 1.22f;
			state.sunsetWarmStrength = 0.08f;
			state.sunsetCoolStrength = 0.20f;
			state.sunsetGlowStrength = 0.08f;
			state.fogDensity = 0.0042f;
			state.fogHeightDensity = 0.0046f;
			state.fogSkyTintInfluence = 0.08f;
			break;
		case WeatherPresetId::Rain:
			state.zenithExponent = 1.9f;
			state.anisotropicIntensity = 0.14f;
			state.atmosphereDensity = 0.18f;
			state.precipitationType = WeatherPrecipitationType::Rain;
			state.precipitationIntensity = 0.45f;
			state.surface.wetness = 0.55f;
			state.surface.puddleAmount = 0.25f;
			state.cloudCoverage = 0.94f;
			state.cloudDensity = 1.55f;
			state.cloudViewAbsorption = 0.82f;
			state.cloudShadowStrength = 0.92f;
			state.sunIntensity = 0.16f;
			state.sunColour = math::Color(0.58f, 0.62f, 0.68f, 1.0f);
			state.fogColour = math::Color(0.40f, 0.44f, 0.50f, 1.0f);
			state.ambientLight = math::Vector4(0.12f, 0.13f, 0.16f, 1.0f);
			state.ambientSkyStrength = 0.16f;
			state.rayleighStrength = 0.26f;
			state.mieStrength = 1.95f;
			state.cloudAmbientStrength = 0.14f;
			state.sunHazeStrength = 1.35f;
			state.sunsetWarmStrength = 0.04f;
			state.sunsetCoolStrength = 0.14f;
			state.sunsetGlowStrength = 0.04f;
			state.fogDensity = 0.0052f;
			state.fogHeightDensity = 0.0056f;
			state.fogSkyTintInfluence = 0.04f;
			state.windSpeed = 26.0f;
			break;
		case WeatherPresetId::HeavyRain:
			state = MakePresetState(WeatherPresetId::Rain);
			state.presetId = WeatherPresetId::HeavyRain;
			state.precipitationIntensity = 1.0f;
			state.precipitationAreaRadius = 38.0f;
			state.precipitationHeight = 22.0f;
			state.surface.wetness = 0.82f;
			state.surface.puddleAmount = 0.60f;
			state.zenithExponent = 1.55f;
			state.anisotropicIntensity = 0.10f;
			state.cloudCoverage = 0.98f;
			state.cloudDensity = 1.95f;
			state.cloudViewAbsorption = 0.95f;
			state.cloudShadowStrength = 1.0f;
			state.sunIntensity = 0.08f;
			state.sunColour = math::Color(0.48f, 0.52f, 0.58f, 1.0f);
			state.fogColour = math::Color(0.34f, 0.37f, 0.43f, 1.0f);
			state.ambientLight = math::Vector4(0.08f, 0.09f, 0.11f, 1.0f);
			state.ambientSkyStrength = 0.10f;
			state.rayleighStrength = 0.18f;
			state.mieStrength = 2.15f;
			state.cloudAmbientStrength = 0.08f;
			state.sunHazeStrength = 1.55f;
			state.sunsetWarmStrength = 0.02f;
			state.sunsetCoolStrength = 0.10f;
			state.sunsetGlowStrength = 0.02f;
			state.fogDensity = 0.0068f;
			state.fogHeightDensity = 0.0072f;
			state.fogSkyTintInfluence = 0.02f;
			state.windSpeed = 36.0f;
			break;
		case WeatherPresetId::Storm:
			state = MakePresetState(WeatherPresetId::HeavyRain);
			state.presetId = WeatherPresetId::Storm;
			state.enableLightning = true;
			state.lightningIntensity = 5.5f;
			state.lightningIntervalMin = 1.2f;
			state.lightningIntervalMax = 3.0f;
			state.lightningDuration = 0.35f;
			state.volumetricStrength = 1.35f;
			state.zenithExponent = 1.35f;
			state.anisotropicIntensity = 0.06f;
			state.atmosphereDensity = 0.24f;
			state.cloudCoverage = 1.0f;
			state.cloudDensity = 2.25f;
			state.sunIntensity = 0.03f;
			state.sunColour = math::Color(0.42f, 0.46f, 0.52f, 1.0f);
			state.fogColour = math::Color(0.24f, 0.27f, 0.32f, 1.0f);
			state.ambientLight = math::Vector4(0.05f, 0.06f, 0.08f, 1.0f);
			state.ambientSkyStrength = 0.05f;
			state.rayleighStrength = 0.10f;
			state.mieStrength = 2.45f;
			state.cloudAmbientStrength = 0.04f;
			state.cloudViewAbsorption = 1.0f;
			state.cloudShadowStrength = 1.0f;
			state.sunHazeStrength = 1.85f;
			state.sunsetWarmStrength = 0.0f;
			state.sunsetCoolStrength = 0.06f;
			state.sunsetGlowStrength = 0.0f;
			state.fogDensity = 0.0085f;
			state.fogHeightDensity = 0.0090f;
			state.fogSkyTintInfluence = 0.0f;
			state.windSpeed = 42.0f;
			break;
		case WeatherPresetId::Thunderstorm:
			state = MakePresetState(WeatherPresetId::Storm);
			state.presetId = WeatherPresetId::Thunderstorm;
			state.lightningIntensity = 7.5f;
			state.lightningIntervalMin = 1.8f;
			state.lightningIntervalMax = 4.0f;
			state.lightningDuration = 0.45f;
			break;
		case WeatherPresetId::Snow:
			state.zenithExponent = 2.0f;
			state.anisotropicIntensity = 0.12f;
			state.precipitationType = WeatherPrecipitationType::Snow;
			state.precipitationIntensity = 0.36f;
			state.surface.snowCoverage = 0.45f;
			state.surface.snowMelt = 0.15f;
			state.surface.temperatureBias = -0.8f;
			state.cloudCoverage = 0.78f;
			state.cloudDensity = 1.05f;
			state.atmosphereDensity = 0.13f;
			state.sunIntensity = 0.18f;
			state.sunColour = math::Color(0.72f, 0.76f, 0.82f, 1.0f);
			state.fogColour = math::Color(0.58f, 0.62f, 0.68f, 1.0f);
			state.ambientLight = math::Vector4(0.18f, 0.20f, 0.23f, 1.0f);
			state.ambientSkyStrength = 0.18f;
			state.rayleighStrength = 0.20f;
			state.mieStrength = 1.58f;
			state.sunHazeStrength = 1.18f;
			state.sunsetWarmStrength = 0.06f;
			state.sunsetCoolStrength = 0.18f;
			state.sunsetGlowStrength = 0.06f;
			state.cloudAmbientStrength = 0.12f;
			state.cloudViewAbsorption = 0.78f;
			state.cloudShadowStrength = 0.82f;
			state.fogDensity = 0.0048f;
			state.fogHeightDensity = 0.0052f;
			state.fogSkyTintInfluence = 0.04f;
			state.windSpeed = 16.0f;
			state.auroraIntensity = 0.0f;
			break;
		case WeatherPresetId::Blizzard:
			state = MakePresetState(WeatherPresetId::Snow);
			state.presetId = WeatherPresetId::Blizzard;
			state.precipitationIntensity = 1.0f;
			state.precipitationAreaRadius = 42.0f;
			state.precipitationHeight = 20.0f;
			state.surface.snowCoverage = 0.9f;
			state.cloudCoverage = 0.95f;
			state.cloudDensity = 1.65f;
			state.atmosphereDensity = 0.17f;
			state.zenithExponent = 1.45f;
			state.anisotropicIntensity = 0.07f;
			state.sunIntensity = 0.04f;
			state.sunColour = math::Color(0.60f, 0.64f, 0.70f, 1.0f);
			state.fogColour = math::Color(0.48f, 0.52f, 0.58f, 1.0f);
			state.ambientLight = math::Vector4(0.12f, 0.14f, 0.16f, 1.0f);
			state.ambientSkyStrength = 0.08f;
			state.rayleighStrength = 0.10f;
			state.mieStrength = 1.95f;
			state.sunHazeStrength = 1.34f;
			state.sunsetWarmStrength = 0.02f;
			state.sunsetCoolStrength = 0.10f;
			state.sunsetGlowStrength = 0.02f;
			state.cloudAmbientStrength = 0.06f;
			state.cloudViewAbsorption = 0.95f;
			state.cloudShadowStrength = 0.95f;
			state.fogDensity = 0.0080f;
			state.fogHeightDensity = 0.0086f;
			state.fogSkyTintInfluence = 0.02f;
			state.windSpeed = 48.0f;
			state.auroraIntensity = 0.0f;
			break;
		case WeatherPresetId::Hot:
			state.surface.temperatureBias = 1.0f;
			state.cloudCoverage = 0.12f;
			state.cloudDensity = 0.35f;
			state.sunIntensity = 1.25f;
			state.atmosphereDensity = 0.14f;
			state.mieStrength = 1.45f;
			state.sunHazeStrength = 1.35f;
			state.fogDensity = 0.0018f;
			state.windSpeed = 10.0f;
			break;
		case WeatherPresetId::Sandstorm:
			state.precipitationType = WeatherPrecipitationType::Sand;
			state.precipitationIntensity = 0.78f;
			state.surface.dirtAmount = 0.7f;
			state.surface.temperatureBias = 0.7f;
			state.cloudCoverage = 0.48f;
			state.cloudDensity = 0.82f;
			state.atmosphereDensity = 0.19f;
			state.mieStrength = 1.75f;
			state.sunHazeStrength = 1.6f;
			state.fogDensity = 0.0060f;
			state.fogSkyTintInfluence = 0.12f;
			state.fogColour = math::Color(0.72f, 0.58f, 0.39f, 1.0f);
			state.ambientLight = math::Vector4(0.42f, 0.34f, 0.24f, 1.0f);
			state.windDirection = math::Vector3(0.75f, 0.0f, 0.35f);
			state.windSpeed = 58.0f;
			break;
		case WeatherPresetId::Custom:
		default:
			break;
		}

		return state;
	}

	WeatherState LerpWeatherState(const WeatherState& a, const WeatherState& b, float alpha)
	{
		alpha = std::clamp(alpha, 0.0f, 1.0f);

		WeatherState out = a;
		out.presetId = alpha >= 0.5f ? b.presetId : a.presetId;
		out.precipitationType = alpha >= 0.5f ? b.precipitationType : a.precipitationType;
		out.surface.wetness = LerpValue(a.surface.wetness, b.surface.wetness, alpha);
		out.surface.puddleAmount = LerpValue(a.surface.puddleAmount, b.surface.puddleAmount, alpha);
		out.surface.snowCoverage = LerpValue(a.surface.snowCoverage, b.surface.snowCoverage, alpha);
		out.surface.snowMelt = LerpValue(a.surface.snowMelt, b.surface.snowMelt, alpha);
		out.surface.dirtAmount = LerpValue(a.surface.dirtAmount, b.surface.dirtAmount, alpha);
		out.surface.temperatureBias = LerpValue(a.surface.temperatureBias, b.surface.temperatureBias, alpha);
		out.ambientLight = LerpValue(a.ambientLight, b.ambientLight, alpha);
		out.fogColour = LerpValue(a.fogColour, b.fogColour, alpha);
		out.sunColour = LerpValue(a.sunColour, b.sunColour, alpha);
		out.sunIntensity = LerpValue(a.sunIntensity, b.sunIntensity, alpha);
		out.zenithExponent = LerpValue(a.zenithExponent, b.zenithExponent, alpha);
		out.anisotropicIntensity = LerpValue(a.anisotropicIntensity, b.anisotropicIntensity, alpha);
		out.atmosphereDensity = LerpValue(a.atmosphereDensity, b.atmosphereDensity, alpha);
		out.rayleighStrength = LerpValue(a.rayleighStrength, b.rayleighStrength, alpha);
		out.mieStrength = LerpValue(a.mieStrength, b.mieStrength, alpha);
		out.ambientSkyStrength = LerpValue(a.ambientSkyStrength, b.ambientSkyStrength, alpha);
		out.sunHazeStrength = LerpValue(a.sunHazeStrength, b.sunHazeStrength, alpha);
		out.sunsetWarmStrength = LerpValue(a.sunsetWarmStrength, b.sunsetWarmStrength, alpha);
		out.sunsetCoolStrength = LerpValue(a.sunsetCoolStrength, b.sunsetCoolStrength, alpha);
		out.sunsetGlowStrength = LerpValue(a.sunsetGlowStrength, b.sunsetGlowStrength, alpha);
		out.volumetricScattering = LerpValue(a.volumetricScattering, b.volumetricScattering, alpha);
		out.volumetricStrength = LerpValue(a.volumetricStrength, b.volumetricStrength, alpha);
		out.fogDensity = LerpValue(a.fogDensity, b.fogDensity, alpha);
		out.fogStartDistance = LerpValue(a.fogStartDistance, b.fogStartDistance, alpha);
		out.fogHeightDensity = LerpValue(a.fogHeightDensity, b.fogHeightDensity, alpha);
		out.fogHeightFalloff = LerpValue(a.fogHeightFalloff, b.fogHeightFalloff, alpha);
		out.fogHeightPivot = LerpValue(a.fogHeightPivot, b.fogHeightPivot, alpha);
		out.fogSkyTintInfluence = LerpValue(a.fogSkyTintInfluence, b.fogSkyTintInfluence, alpha);
		out.cloudDensity = LerpValue(a.cloudDensity, b.cloudDensity, alpha);
		out.cloudCoverage = LerpValue(a.cloudCoverage, b.cloudCoverage, alpha);
		out.cloudErosion = LerpValue(a.cloudErosion, b.cloudErosion, alpha);
		out.cloudAmbientStrength = LerpValue(a.cloudAmbientStrength, b.cloudAmbientStrength, alpha);
		out.cloudViewAbsorption = LerpValue(a.cloudViewAbsorption, b.cloudViewAbsorption, alpha);
		out.cloudShadowStrength = LerpValue(a.cloudShadowStrength, b.cloudShadowStrength, alpha);
		out.cloudAnimationSpeed = LerpValue(a.cloudAnimationSpeed, b.cloudAnimationSpeed, alpha);
		out.windDirection = LerpValue(a.windDirection, b.windDirection, alpha);
		out.windSpeed = LerpValue(a.windSpeed, b.windSpeed, alpha);
		out.precipitationIntensity = LerpValue(a.precipitationIntensity, b.precipitationIntensity, alpha);
		out.precipitationAreaRadius = LerpValue(a.precipitationAreaRadius, b.precipitationAreaRadius, alpha);
		out.precipitationHeight = LerpValue(a.precipitationHeight, b.precipitationHeight, alpha);
		out.enableLightning = alpha >= 0.5f ? b.enableLightning : a.enableLightning;
		out.lightningIntervalMin = LerpValue(a.lightningIntervalMin, b.lightningIntervalMin, alpha);
		out.lightningIntervalMax = LerpValue(a.lightningIntervalMax, b.lightningIntervalMax, alpha);
		out.lightningDuration = LerpValue(a.lightningDuration, b.lightningDuration, alpha);
		out.lightningIntensity = LerpValue(a.lightningIntensity, b.lightningIntensity, alpha);
		out.enableAurora = alpha >= 0.5f ? b.enableAurora : a.enableAurora;
		out.auroraIntensity = LerpValue(a.auroraIntensity, b.auroraIntensity, alpha);
		out.auroraSpeed = LerpValue(a.auroraSpeed, b.auroraSpeed, alpha);
		out.auroraBanding = LerpValue(a.auroraBanding, b.auroraBanding, alpha);
		out.auroraHeight = LerpValue(a.auroraHeight, b.auroraHeight, alpha);
		out.auroraColorA = LerpValue(a.auroraColorA, b.auroraColorA, alpha);
		out.auroraColorB = LerpValue(a.auroraColorB, b.auroraColorB, alpha);
		out.transitionSeconds = LerpValue(a.transitionSeconds, b.transitionSeconds, alpha);
		return out;
	}

	void SerializeWeatherSurface(const WeatherSurfaceResponse& surface, json& data, JsonFile* file)
	{
		file->Serialize(data, "wetness", surface.wetness);
		file->Serialize(data, "puddleAmount", surface.puddleAmount);
		file->Serialize(data, "snowCoverage", surface.snowCoverage);
		file->Serialize(data, "snowMelt", surface.snowMelt);
		file->Serialize(data, "dirtAmount", surface.dirtAmount);
		file->Serialize(data, "temperatureBias", surface.temperatureBias);
	}

	void DeserializeWeatherSurface(WeatherSurfaceResponse& surface, json& data, JsonFile* file)
	{
		file->Deserialize(data, "wetness", surface.wetness);
		file->Deserialize(data, "puddleAmount", surface.puddleAmount);
		file->Deserialize(data, "snowCoverage", surface.snowCoverage);
		file->Deserialize(data, "snowMelt", surface.snowMelt);
		file->Deserialize(data, "dirtAmount", surface.dirtAmount);
		file->Deserialize(data, "temperatureBias", surface.temperatureBias);
	}

	void SerializeWeatherState(const WeatherState& state, json& data, JsonFile* file)
	{
		data["presetId"] = static_cast<int32_t>(state.presetId);
		data["precipitationType"] = static_cast<int32_t>(state.precipitationType);
		SerializeWeatherSurface(state.surface, data["surface"], file);
		file->Serialize(data, "ambientLight", state.ambientLight);
		file->Serialize(data, "fogColour", state.fogColour);
		file->Serialize(data, "sunColour", state.sunColour);
		file->Serialize(data, "sunIntensity", state.sunIntensity);
		file->Serialize(data, "zenithExponent", state.zenithExponent);
		file->Serialize(data, "anisotropicIntensity", state.anisotropicIntensity);
		file->Serialize(data, "atmosphereDensity", state.atmosphereDensity);
		file->Serialize(data, "rayleighStrength", state.rayleighStrength);
		file->Serialize(data, "mieStrength", state.mieStrength);
		file->Serialize(data, "ambientSkyStrength", state.ambientSkyStrength);
		file->Serialize(data, "sunHazeStrength", state.sunHazeStrength);
		file->Serialize(data, "sunsetWarmStrength", state.sunsetWarmStrength);
		file->Serialize(data, "sunsetCoolStrength", state.sunsetCoolStrength);
		file->Serialize(data, "sunsetGlowStrength", state.sunsetGlowStrength);
		file->Serialize(data, "volumetricScattering", state.volumetricScattering);
		file->Serialize(data, "volumetricStrength", state.volumetricStrength);
		file->Serialize(data, "fogDensity", state.fogDensity);
		file->Serialize(data, "fogStartDistance", state.fogStartDistance);
		file->Serialize(data, "fogHeightDensity", state.fogHeightDensity);
		file->Serialize(data, "fogHeightFalloff", state.fogHeightFalloff);
		file->Serialize(data, "fogHeightPivot", state.fogHeightPivot);
		file->Serialize(data, "fogSkyTintInfluence", state.fogSkyTintInfluence);
		file->Serialize(data, "cloudDensity", state.cloudDensity);
		file->Serialize(data, "cloudCoverage", state.cloudCoverage);
		file->Serialize(data, "cloudErosion", state.cloudErosion);
		file->Serialize(data, "cloudAmbientStrength", state.cloudAmbientStrength);
		file->Serialize(data, "cloudViewAbsorption", state.cloudViewAbsorption);
		file->Serialize(data, "cloudShadowStrength", state.cloudShadowStrength);
		file->Serialize(data, "cloudAnimationSpeed", state.cloudAnimationSpeed);
		file->Serialize(data, "windDirection", state.windDirection);
		file->Serialize(data, "windSpeed", state.windSpeed);
		file->Serialize(data, "precipitationIntensity", state.precipitationIntensity);
		file->Serialize(data, "precipitationAreaRadius", state.precipitationAreaRadius);
		file->Serialize(data, "precipitationHeight", state.precipitationHeight);
		file->Serialize(data, "enableLightning", state.enableLightning);
		file->Serialize(data, "lightningIntervalMin", state.lightningIntervalMin);
		file->Serialize(data, "lightningIntervalMax", state.lightningIntervalMax);
		file->Serialize(data, "lightningDuration", state.lightningDuration);
		file->Serialize(data, "lightningIntensity", state.lightningIntensity);
		file->Serialize(data, "enableAurora", state.enableAurora);
		file->Serialize(data, "auroraIntensity", state.auroraIntensity);
		file->Serialize(data, "auroraSpeed", state.auroraSpeed);
		file->Serialize(data, "auroraBanding", state.auroraBanding);
		file->Serialize(data, "auroraHeight", state.auroraHeight);
		file->Serialize(data, "auroraColorA", state.auroraColorA);
		file->Serialize(data, "auroraColorB", state.auroraColorB);
		file->Serialize(data, "transitionSeconds", state.transitionSeconds);
	}

	void DeserializeWeatherState(WeatherState& state, json& data, JsonFile* file)
	{
		int32_t presetId = static_cast<int32_t>(state.presetId);
		int32_t precipitationType = static_cast<int32_t>(state.precipitationType);
		file->Deserialize(data, "presetId", presetId);
		file->Deserialize(data, "precipitationType", precipitationType);
		state.presetId = static_cast<WeatherPresetId>(presetId);
		state.precipitationType = static_cast<WeatherPrecipitationType>(precipitationType);
		if (data.contains("surface"))
			DeserializeWeatherSurface(state.surface, data["surface"], file);
		file->Deserialize(data, "ambientLight", state.ambientLight);
		file->Deserialize(data, "fogColour", state.fogColour);
		file->Deserialize(data, "sunColour", state.sunColour);
		file->Deserialize(data, "sunIntensity", state.sunIntensity);
		file->Deserialize(data, "zenithExponent", state.zenithExponent);
		file->Deserialize(data, "anisotropicIntensity", state.anisotropicIntensity);
		file->Deserialize(data, "atmosphereDensity", state.atmosphereDensity);
		file->Deserialize(data, "rayleighStrength", state.rayleighStrength);
		file->Deserialize(data, "mieStrength", state.mieStrength);
		file->Deserialize(data, "ambientSkyStrength", state.ambientSkyStrength);
		file->Deserialize(data, "sunHazeStrength", state.sunHazeStrength);
		file->Deserialize(data, "sunsetWarmStrength", state.sunsetWarmStrength);
		file->Deserialize(data, "sunsetCoolStrength", state.sunsetCoolStrength);
		file->Deserialize(data, "sunsetGlowStrength", state.sunsetGlowStrength);
		file->Deserialize(data, "volumetricScattering", state.volumetricScattering);
		file->Deserialize(data, "volumetricStrength", state.volumetricStrength);
		file->Deserialize(data, "fogDensity", state.fogDensity);
		file->Deserialize(data, "fogStartDistance", state.fogStartDistance);
		file->Deserialize(data, "fogHeightDensity", state.fogHeightDensity);
		file->Deserialize(data, "fogHeightFalloff", state.fogHeightFalloff);
		file->Deserialize(data, "fogHeightPivot", state.fogHeightPivot);
		file->Deserialize(data, "fogSkyTintInfluence", state.fogSkyTintInfluence);
		file->Deserialize(data, "cloudDensity", state.cloudDensity);
		file->Deserialize(data, "cloudCoverage", state.cloudCoverage);
		file->Deserialize(data, "cloudErosion", state.cloudErosion);
		file->Deserialize(data, "cloudAmbientStrength", state.cloudAmbientStrength);
		file->Deserialize(data, "cloudViewAbsorption", state.cloudViewAbsorption);
		file->Deserialize(data, "cloudShadowStrength", state.cloudShadowStrength);
		file->Deserialize(data, "cloudAnimationSpeed", state.cloudAnimationSpeed);
		file->Deserialize(data, "windDirection", state.windDirection);
		file->Deserialize(data, "windSpeed", state.windSpeed);
		file->Deserialize(data, "precipitationIntensity", state.precipitationIntensity);
		file->Deserialize(data, "precipitationAreaRadius", state.precipitationAreaRadius);
		file->Deserialize(data, "precipitationHeight", state.precipitationHeight);
		file->Deserialize(data, "enableLightning", state.enableLightning);
		file->Deserialize(data, "lightningIntervalMin", state.lightningIntervalMin);
		file->Deserialize(data, "lightningIntervalMax", state.lightningIntervalMax);
		file->Deserialize(data, "lightningDuration", state.lightningDuration);
		file->Deserialize(data, "lightningIntensity", state.lightningIntensity);
		file->Deserialize(data, "enableAurora", state.enableAurora);
		file->Deserialize(data, "auroraIntensity", state.auroraIntensity);
		file->Deserialize(data, "auroraSpeed", state.auroraSpeed);
		file->Deserialize(data, "auroraBanding", state.auroraBanding);
		file->Deserialize(data, "auroraHeight", state.auroraHeight);
		file->Deserialize(data, "auroraColorA", state.auroraColorA);
		file->Deserialize(data, "auroraColorB", state.auroraColorB);
		file->Deserialize(data, "transitionSeconds", state.transitionSeconds);
	}
}
