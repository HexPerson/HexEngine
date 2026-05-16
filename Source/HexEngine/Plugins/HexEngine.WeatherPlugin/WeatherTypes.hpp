#pragma once

#include <HexEngine.Core/HexEngine.hpp>

namespace HexEngine::Weather
{
	enum class WeatherPresetId : int32_t
	{
		Custom = 0,
		Clear,
		Overcast,
		Rain,
		HeavyRain,
		Storm,
		Thunderstorm,
		Snow,
		Blizzard,
		Hot,
		Sandstorm
	};

	enum class WeatherPrecipitationType : int32_t
	{
		None = 0,
		Rain,
		Snow,
		Sand
	};

	enum class WeatherZoneShape : int32_t
	{
		Sphere = 0,
		Box
	};

	struct WeatherSurfaceResponse
	{
		float wetness = 0.0f;
		float puddleAmount = 0.0f;
		float snowCoverage = 0.0f;
		float snowMelt = 0.0f;
		float dirtAmount = 0.0f;
		float temperatureBias = 0.0f;
	};

	struct WeatherState
	{
		WeatherPresetId presetId = WeatherPresetId::Custom;
		WeatherPrecipitationType precipitationType = WeatherPrecipitationType::None;
		WeatherSurfaceResponse surface;

		math::Vector4 ambientLight = math::Vector4(0.14f, 0.14f, 0.145f, 1.0f);
		math::Color fogColour = math::Color(HEX_RGB_TO_FLOAT3(95, 95, 95));
		math::Color sunColour = math::Color(1.0f, 0.98f, 0.94f, 1.0f);
		float sunIntensity = 1.0f;

		float zenithExponent = 4.12f;
		float anisotropicIntensity = 0.38f;
		float atmosphereDensity = 0.11f;
		float rayleighStrength = 1.0f;
		float mieStrength = 1.0f;
		float ambientSkyStrength = 1.0f;
		float sunHazeStrength = 1.0f;
		float sunsetWarmStrength = 1.0f;
		float sunsetCoolStrength = 1.0f;
		float sunsetGlowStrength = 1.0f;
		float volumetricScattering = -0.43f;
		float volumetricStrength = 1.0f;

		float fogDensity = 0.0030f;
		float fogStartDistance = 45.0f;
		float fogHeightDensity = 0.0031f;
		float fogHeightFalloff = 0.0150f;
		float fogHeightPivot = 18.0f;
		float fogSkyTintInfluence = 0.26f;

		float cloudDensity = 1.0f;
		float cloudCoverage = 0.56f;
		float cloudErosion = 0.34f;
		float cloudAmbientStrength = 0.52f;
		float cloudViewAbsorption = 0.42f;
		float cloudShadowStrength = 0.6f;
		float cloudAnimationSpeed = 1.0f;

		math::Vector3 windDirection = math::Vector3(1.0f, 0.0f, 0.15f);
		float windSpeed = 34.0f;

		float precipitationIntensity = 0.0f;
		float precipitationAreaRadius = 30.0f;
		float precipitationHeight = 50.0f;

		bool enableLightning = false;
		float lightningIntervalMin = 4.0f;
		float lightningIntervalMax = 10.0f;
		float lightningDuration = 0.18f;
		float lightningIntensity = 3.0f;
		bool enableAurora = false;
		float auroraIntensity = 0.0f;
		float auroraSpeed = 0.12f;
		float auroraBanding = 1.0f;
		float auroraHeight = 0.42f;
		math::Color auroraColorA = math::Color(0.10f, 0.90f, 0.65f, 1.0f);
		math::Color auroraColorB = math::Color(0.32f, 0.38f, 1.00f, 1.0f);

		float transitionSeconds = 2.0f;
	};

	const wchar_t* GetPresetDisplayName(WeatherPresetId presetId);
	const wchar_t* GetPrecipitationDisplayName(WeatherPrecipitationType type);
	const wchar_t* GetZoneShapeDisplayName(WeatherZoneShape shape);

	WeatherState MakePresetState(WeatherPresetId presetId);
	WeatherState LerpWeatherState(const WeatherState& a, const WeatherState& b, float alpha);

	void SerializeWeatherSurface(const WeatherSurfaceResponse& surface, json& data, JsonFile* file);
	void DeserializeWeatherSurface(WeatherSurfaceResponse& surface, json& data, JsonFile* file);
	void SerializeWeatherState(const WeatherState& state, json& data, JsonFile* file);
	void DeserializeWeatherState(WeatherState& state, json& data, JsonFile* file);
}
