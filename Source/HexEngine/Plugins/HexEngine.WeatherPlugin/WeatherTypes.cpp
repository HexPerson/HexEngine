#include "WeatherTypes.hpp"

#include <algorithm>
#include <cmath>

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
		// fogDensity is the uniform extinction (per metre) of the froxel fog
		// medium - it is now a REAL participating-media coefficient, lit and
		// shadowed by the volumetric system, with analytic continuation past
		// the froxel range. Values derive from meteorological visibility via
		// Koschmieder: extinction = 3.912 / visibility_metres.
		//   0.0002 = ~20 km (clear)    0.0085 = ~450 m (heavy rain)
		//   0.0013 = ~3 km (overcast)  0.015  = ~260 m (storm)
		//   0.0035 = ~1.1 km (rain)    0.046  = ~85 m  (blizzard)
		//   0.006  = ~650 m (snow)     0.060  = ~65 m  (sandstorm)
		// The previous values (0.0012..0.009) compressed every preset into a
		// 2x band that the froxel system never even consumed (baseExt was
		// hardcoded 0) - all weathers rendered with identical visibility.
		case WeatherPresetId::Clear:
			state.cloudCoverage = 0.08f;
			state.cloudType = 0.85f;      // fair-weather cumulus
			state.cirrusAmount = 0.35f;
			state.cirrusType = 0.30f;
			state.cloudDensity = 0.50f;
			state.cloudErosion = 0.45f;
			state.windSpeed = 9.0f;
			// 2026-11-24: a little haze - the ~20 km visibility (0.0002) read
			// flat and Lambertian with nothing receding. ~3 km plus a low
			// height band gives distant mountains and the far town aerial
			// depth. The fog's ambient inscatter colour is the scene ambient,
			// and the old dark grey (0.10) turned that haze into a dull brown
			// veil; a sky-tinted ambient makes it read as airy daylight haze
			// (warm at sunset, faint blue at night via the per-frame tint).
			// With DDGI on, surfaces hand this flat ambient off entirely, so it
			// only colours the air; with r_ddgi 0 it also lifts shadows.
			state.fogDensity = 0.0012f;
			state.fogHeightDensity = 0.0030f;
			state.mieStrength = 1.4f;
			state.sunHazeStrength = 1.2f;
			state.fogSkyTintInfluence = 0.5f;
			state.ambientLight = math::Vector4(0.34f, 0.36f, 0.40f, 1.0f);
			// Warm-midday pass (same day): the haze ambient was too blue and the
			// near-white sun let the sky fill wash surfaces blue; a slightly warm
			// sun + near-neutral haze reads as a sunny day (user ref: vibrant, warm).
			state.sunColour = math::Color(1.0f, 0.94f, 0.84f, 1.0f);
			state.surface.temperatureBias = 0.2f;
			state.sunIntensity = 1.92f;
			// 2026-11-23 look pass: the sunny-day sun (was carried by a 3x day/night boost).
			break;
		case WeatherPresetId::Overcast:
			state.zenithExponent = 2.2f;
			// Deck values bridge-tuned live 2026-08-29 against the HDR sky:
			// a heavy broken deck (0.88) of thick (1.7) low stratus reads as
			// overcast; the old 0.60/1.05 rendered sparse thin scraps.
			state.cloudType = 0.10f;      // flat stratus deck
			state.cirrusAmount = 0.0f;
			state.cirrusType = 0.50f;
			state.anisotropicIntensity = 0.16f;
			state.atmosphereDensity = 0.16f;
			state.cloudCoverage = 0.97f;
			state.cloudDensity = 1.70f;
			state.cloudAmbientStrength = 0.75f;
			state.cloudViewAbsorption = 0.90f;
			state.cloudShadowStrength = 0.82f;
			state.sunIntensity = 0.05f;
			state.sunColour = math::Color(0.68f, 0.71f, 0.76f, 1.0f);
			state.fogColour = math::Color(0.50f, 0.53f, 0.58f, 1.0f);
			state.ambientLight = math::Vector4(0.34f, 0.35f, 0.37f, 1.0f);
			state.ambientSkyStrength = 0.24f;
			state.rayleighStrength = 0.34f;
			state.mieStrength = 1.65f;
			state.sunHazeStrength = 1.22f;
			state.sunsetWarmStrength = 0.08f;
			state.sunsetCoolStrength = 0.20f;
			state.sunsetGlowStrength = 0.08f;
			// Overcast is grey skies, not fog - keep visibility long (~3km)
			// and let the cloud deck + dimmed sun carry the mood.
			state.fogDensity = 0.0013f;
			state.fogHeightDensity = 0.0030f;
			state.fogSkyTintInfluence = 0.08f;
			// 2026-11-23 look pass: values tuned with r_iblSkyDiffuse 0.25 and the scene's
			// _sunLightBoost 1.56 - cloud cover reads as soft diffuse light, not sun + gloom.
			break;
		case WeatherPresetId::Rain:
			state.zenithExponent = 1.9f;
			state.cloudType = 0.15f;
			state.cirrusAmount = 0.0f;
			state.anisotropicIntensity = 0.14f;
			state.atmosphereDensity = 0.18f;
			state.precipitationType = WeatherPrecipitationType::Rain;
			state.precipitationIntensity = 0.45f;
			state.surface.wetness = 0.55f;
			state.surface.puddleAmount = 0.25f;
			// Bridge-tuned 2026-08-29 against the HDR sky.
			state.cloudCoverage = 0.95f;
			state.cloudDensity = 1.90f;
			state.cloudViewAbsorption = 0.90f;
			state.cloudShadowStrength = 0.92f;
			state.sunIntensity = 0.08f;
			state.sunColour = math::Color(0.58f, 0.62f, 0.68f, 1.0f);
			state.fogColour = math::Color(0.40f, 0.44f, 0.50f, 1.0f);
			state.ambientLight = math::Vector4(0.24f, 0.25f, 0.28f, 1.0f);
			state.ambientSkyStrength = 0.16f;
			state.rayleighStrength = 0.26f;
			state.mieStrength = 1.95f;
			state.cloudAmbientStrength = 0.32f;
			state.sunHazeStrength = 1.35f;
			state.sunsetWarmStrength = 0.04f;
			state.sunsetCoolStrength = 0.14f;
			state.sunsetGlowStrength = 0.04f;
			// Rain: ~1.1km visibility - the air visibly thickens but the
			// street still reads clearly.
			state.fogDensity = 0.004f;
			state.fogHeightDensity = 0.0045f;
			state.fogSkyTintInfluence = 0.04f;
			state.windSpeed = 13.0f;
			break;
		case WeatherPresetId::HeavyRain:
			state = MakePresetState(WeatherPresetId::Rain);
			state.presetId = WeatherPresetId::HeavyRain;
			state.cloudType = 0.25f;
			state.precipitationIntensity = 1.0f;
			state.precipitationAreaRadius = 38.0f;
			state.precipitationHeight = 22.0f;
			state.surface.wetness = 0.82f;
			state.surface.puddleAmount = 0.60f;
			state.zenithExponent = 1.55f;
			state.anisotropicIntensity = 0.10f;
			state.cloudCoverage = 1.0f;
			state.cloudDensity = 2.10f;
			state.cloudViewAbsorption = 0.95f;
			state.cloudShadowStrength = 1.0f;
			state.sunIntensity = 0.04f;
			state.sunColour = math::Color(0.48f, 0.52f, 0.58f, 1.0f);
			state.fogColour = math::Color(0.34f, 0.37f, 0.43f, 1.0f);
			state.ambientLight = math::Vector4(0.18f, 0.19f, 0.22f, 1.0f);
			state.ambientSkyStrength = 0.10f;
			state.rayleighStrength = 0.18f;
			state.mieStrength = 2.15f;
			state.cloudAmbientStrength = 0.26f;
			state.sunHazeStrength = 1.55f;
			state.sunsetWarmStrength = 0.02f;
			state.sunsetCoolStrength = 0.10f;
			state.sunsetGlowStrength = 0.02f;
			// Heavy rain: ~450m visibility - distant blocks dissolve into
			// the downpour.
			state.fogDensity = 0.008f;
			state.fogHeightDensity = 0.0060f;
			state.fogSkyTintInfluence = 0.02f;
			state.windSpeed = 18.0f;
			break;
		case WeatherPresetId::Storm:
			state = MakePresetState(WeatherPresetId::HeavyRain);
			state.presetId = WeatherPresetId::Storm;
			// 2026-11-26 cloud pass (user reference: stormy sunset, sculpted
			// cumulonimbus with gaps toward a low sun). The old closed 1.0 deck
			// at type 0.55 + 260 m fog rendered one flat grey ceiling with no
			// edge to catch light. Now: towering cells (type 0.95 + coverage
			// >= 0.55 arms the cumulonimbus/anvil profile in CloudCommon),
			// broken (0.58) so the sun breaks through, clearer air (~1.5 km)
			// so the cells read, and a real sun (0.45) for the shafts and the
			// lit patches between cloud shadows. HeavyRain keeps the closed deck.
			state.cloudType = 0.95f;      // towering storm cells
			state.cloudErosion = 0.50f;
			state.enableLightning = true;
			state.lightningIntensity = 3.0f;
			state.lightningIntervalMin = 1.2f;
			state.lightningIntervalMax = 3.0f;
			state.lightningDuration = 0.35f;
			state.volumetricStrength = 1.35f;
			state.zenithExponent = 1.35f;
			state.anisotropicIntensity = 0.06f;
			state.atmosphereDensity = 0.24f;
			state.cloudCoverage = 0.58f;  // 0.66 closed into a ceiling once storm towers got their 8 km slab
			state.cloudDensity = 2.40f;
			state.sunIntensity = 0.45f;
			state.sunColour = math::Color(0.42f, 0.46f, 0.52f, 1.0f);
			state.fogColour = math::Color(0.24f, 0.27f, 0.32f, 1.0f);
			state.ambientLight = math::Vector4(0.11f, 0.12f, 0.15f, 1.0f);
			state.ambientSkyStrength = 0.05f;
			state.rayleighStrength = 0.10f;
			state.mieStrength = 2.45f;
			// 0.18 -> 0.30: the cloud shader now darkens bases by sky
			// visibility itself; 0.18 on top crushed them to black.
			state.cloudAmbientStrength = 0.30f;
			state.cloudViewAbsorption = 1.0f;
			state.cloudShadowStrength = 1.0f;
			state.sunHazeStrength = 1.85f;
			// A little warm glow at the sun through the gaps (was 0 =
			// colourless dusk). Kept low: 0.6 washed the whole frame orange
			// (fog + sky), burying the cool storm bases.
			state.sunsetWarmStrength = 0.15f;
			state.sunsetCoolStrength = 0.3f;
			state.sunsetGlowStrength = 0.35f;
			// Storm: ~1.5 km visibility (was 0.013 = 260 m, which fogged the
			// sky until no cloud structure survived). Rain shafts under the
			// cells (VolumetricClouds) now carry the "closing in" mood.
			state.fogDensity = 0.0025f;
			state.fogHeightDensity = 0.0030f;
			state.fogSkyTintInfluence = 0.0f;
			state.windSpeed = 21.0f;
			break;
		case WeatherPresetId::Thunderstorm:
			state = MakePresetState(WeatherPresetId::Storm);
			state.presetId = WeatherPresetId::Thunderstorm;
			state.lightningIntensity = 3.0f;
			state.lightningIntervalMin = 1.8f;
			state.lightningIntervalMax = 4.0f;
			state.lightningDuration = 0.45f;
			// Slightly denser than the base storm (~1.2 km vs ~1.5 km) so the
			// two presets read differently even between lightning strikes.
			state.fogDensity = 0.0032f;
			break;
		case WeatherPresetId::Snow:
			state.zenithExponent = 2.0f;
			state.cloudType = 0.20f;
			state.cirrusAmount = 0.15f;
			state.cirrusType = 0.60f;
			state.anisotropicIntensity = 0.12f;
			state.precipitationType = WeatherPrecipitationType::Snow;
			state.precipitationIntensity = 0.36f;
			state.surface.snowCoverage = 0.45f;
			state.surface.snowMelt = 0.15f;
			state.surface.temperatureBias = -0.8f;
			state.cloudCoverage = 0.96f;
			state.cloudDensity = 1.50f;
			state.atmosphereDensity = 0.13f;
			state.sunIntensity = 0.10f;
			state.sunColour = math::Color(0.72f, 0.76f, 0.82f, 1.0f);
			state.fogColour = math::Color(0.58f, 0.62f, 0.68f, 1.0f);
			state.ambientLight = math::Vector4(0.36f, 0.38f, 0.42f, 1.0f);
			state.ambientSkyStrength = 0.18f;
			state.rayleighStrength = 0.20f;
			state.mieStrength = 1.58f;
			state.sunHazeStrength = 1.18f;
			state.sunsetWarmStrength = 0.06f;
			state.sunsetCoolStrength = 0.18f;
			state.sunsetGlowStrength = 0.06f;
			state.cloudAmbientStrength = 0.55f;
			state.cloudViewAbsorption = 0.80f;
			state.cloudShadowStrength = 0.82f;
			// Snow: ~650m visibility - softly muffled air, still navigable.
			state.fogDensity = 0.006f;
			state.fogHeightDensity = 0.0055f;
			state.fogSkyTintInfluence = 0.04f;
			state.windSpeed = 8.0f;
			state.auroraIntensity = 0.0f;
			break;
		case WeatherPresetId::Blizzard:
			state = MakePresetState(WeatherPresetId::Snow);
			state.presetId = WeatherPresetId::Blizzard;
			state.cloudType = 0.25f;
			state.cirrusAmount = 0.0f;
			state.precipitationIntensity = 1.0f;
			state.precipitationAreaRadius = 42.0f;
			state.precipitationHeight = 20.0f;
			state.surface.snowCoverage = 0.9f;
			state.cloudCoverage = 0.98f;
			state.cloudDensity = 1.90f;
			state.atmosphereDensity = 0.17f;
			state.zenithExponent = 1.45f;
			state.anisotropicIntensity = 0.07f;
			state.sunIntensity = 0.04f;
			state.sunColour = math::Color(0.60f, 0.64f, 0.70f, 1.0f);
			state.fogColour = math::Color(0.48f, 0.52f, 0.58f, 1.0f);
			state.ambientLight = math::Vector4(0.52f, 0.55f, 0.60f, 1.0f);
			state.ambientSkyStrength = 0.08f;
			state.rayleighStrength = 0.10f;
			state.mieStrength = 1.95f;
			state.sunHazeStrength = 1.34f;
			state.sunsetWarmStrength = 0.02f;
			state.sunsetCoolStrength = 0.10f;
			state.sunsetGlowStrength = 0.02f;
			state.cloudAmbientStrength = 0.35f;
			state.cloudViewAbsorption = 0.95f;
			state.cloudShadowStrength = 0.95f;
			// Blizzard: ~85m visibility - genuine whiteout. The froxel medium
			// saturates toward the cool-grey ambient within a city block;
			// the analytic continuation closes off everything beyond.
			state.fogDensity = 0.045f;
			state.fogHeightDensity = 0.010f;
			state.fogSkyTintInfluence = 0.02f;
			state.windSpeed = 24.0f;
			state.auroraIntensity = 0.0f;
			// Bright ambient = the fog lights up into a white-out (it was a dark murk).
			break;
		case WeatherPresetId::Hot:
			state.surface.temperatureBias = 1.0f;
			state.cloudType = 0.90f;
			state.cirrusAmount = 0.45f;
			state.cirrusType = 0.25f;
			state.cloudCoverage = 0.14f;
			state.cloudDensity = 0.55f;
			state.cloudAmbientStrength = 0.60f;
			state.cloudViewAbsorption = 0.50f;
			state.sunIntensity = 2.4f;
			state.atmosphereDensity = 0.14f;
			state.mieStrength = 1.45f;
			state.sunHazeStrength = 1.35f;
			// Hot: light heat haze (~3km visibility), shimmering rather
			// than thick.
			state.fogDensity = 0.0012f;
			state.windSpeed = 5.0f;
			break;
		case WeatherPresetId::Sandstorm:
			state.precipitationType = WeatherPrecipitationType::Sand;
			state.cloudType = 0.30f;
			state.cirrusAmount = 0.05f;
			state.precipitationIntensity = 0.78f;
			state.surface.dirtAmount = 0.7f;
			state.surface.temperatureBias = 0.7f;
			state.cloudCoverage = 0.35f;
			state.cloudDensity = 0.70f;
			state.atmosphereDensity = 0.19f;
			state.mieStrength = 1.75f;
			state.sunHazeStrength = 1.6f;
			// Sandstorm: ~65m visibility, the densest preset. The froxel
			// medium's ambient inscatter picks up the orange ambientLight
			// below, so the whole air mass glows dusty orange.
			state.fogDensity = 0.035f;
			state.fogHeightDensity = 0.008f;
			state.fogSkyTintInfluence = 0.12f;
			state.fogColour = math::Color(0.72f, 0.58f, 0.39f, 1.0f);
			state.ambientLight = math::Vector4(0.50f, 0.40f, 0.28f, 1.0f);
			state.windDirection = math::Vector3(0.75f, 0.0f, 0.35f);
			state.windSpeed = 29.0f;
			break;
		case WeatherPresetId::Custom:
		default:
			break;
		}

		// scale up the sun if the PBR fix is in
		/*static const HVar* physUnits = g_pEnv->_commandManager->FindHVar("r_pbrEnergyFix");
		if (physUnits && physUnits->_val.b)
		{
			state.sunIntensity *= 2.0f;
		}*/

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
		out.cloudType = LerpValue(a.cloudType, b.cloudType, alpha);
		out.cirrusAmount = LerpValue(a.cirrusAmount, b.cirrusAmount, alpha);
		out.cirrusType = LerpValue(a.cirrusType, b.cirrusType, alpha);
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

	// ---- Sky moods -----------------------------------------------------------
	namespace
	{
		// Every field is relative to the preset: multipliers (1 = unchanged),
		// RGB tints (1,1,1 = unchanged) or additive offsets (0 = unchanged).
		// A preset's reach k blends each term toward identity:
		// mul -> 1 + (m - 1) * k, add -> a * k.
		struct MoodDef
		{
			float warm, cool, glow;                        // env_sunset* trims
			float density, rayleigh, mie, haze;            // atmosphere (LUTs)
			math::Vector3 sunTint; float sunIntensity;     // directional light
			math::Vector3 ambientTint;                     // scene ambient (also fog inscatter)
			float fogDensity; math::Vector3 fogTint;       // froxel fog
			float coverageAdd, cirrusAdd, cirrusTypeAdd, cloudTypeAdd;
		};

		const MoodDef& GetMoodDef(WeatherMoodId mood)
		{
			static const MoodDef kMoods[] = {
				// Standard
				{ 1.00f, 1.00f, 1.00f,  1.00f, 1.00f, 1.00f, 1.00f,  { 1.00f, 1.00f, 1.00f }, 1.00f,  { 1.00f, 1.00f, 1.00f },  1.00f, { 1.00f, 1.00f, 1.00f },  0.00f,  0.00f,  0.00f,  0.00f },
				// Golden: richer warm horizon + halo, warm sun and fill.
				{ 1.70f, 0.70f, 1.60f,  1.08f, 1.00f, 1.15f, 1.20f,  { 1.05f, 0.97f, 0.88f }, 1.00f,  { 1.05f, 1.00f, 0.93f },  1.00f, { 1.05f, 1.00f, 0.92f },  0.00f, 0.10f, 0.00f, 0.00f },
				// Ember: thick aerosol reddens the low sun (Hillaire density/Mie),
				// hot tinted light all day, warmer denser haze.
				{ 2.10f, 0.45f, 1.90f,  1.55f, 0.95f, 1.90f, 1.40f,  { 1.09f, 0.90f, 0.74f }, 0.90f,  { 1.08f, 0.97f, 0.86f },  1.45f, { 1.10f, 0.96f, 0.82f },  0.00f, 0.05f, 0.00f, 0.00f },
				// Crisp: thin clean air (deep blue, sharp distance), cooler light,
				// pink-blue dusk instead of orange.
				{ 0.65f, 1.70f, 0.75f,  0.85f, 1.15f, 0.50f, 0.70f,  { 0.95f, 1.00f, 1.08f }, 1.05f,  { 0.93f, 0.99f, 1.09f },  0.50f, { 0.92f, 0.99f, 1.08f },  0.00f,-0.10f, 0.00f, 0.00f },
				// Violet: strong anti-sun purple, extra streaky cirrus to hold it.
				{ 0.80f, 2.60f, 1.40f,  1.00f, 1.10f, 0.85f, 1.00f,  { 1.03f, 0.94f, 1.02f }, 1.00f,  { 1.03f, 0.94f, 1.10f },  0.85f, { 1.02f, 0.94f, 1.08f },  0.00f, 0.25f,-0.10f, 0.00f },
				// Hazy: milky aerosol, soft sun, lower contrast.
				{ 0.90f, 0.65f, 1.30f,  1.20f, 0.95f, 2.00f, 1.70f,  { 1.02f, 1.00f, 0.95f }, 0.85f,  { 1.05f, 1.04f, 1.00f },  2.00f, { 1.04f, 1.03f, 1.00f },  0.00f, 0.05f, 0.00f, 0.00f },
				// Streaked: more cirrus + a few more low clouds to catch the light.
				{ 1.20f, 1.10f, 1.25f,  1.00f, 1.00f, 1.00f, 1.00f,  { 1.00f, 1.00f, 1.00f }, 1.00f,  { 1.00f, 1.00f, 1.00f },  1.00f, { 1.00f, 1.00f, 1.00f },  0.06f,  0.35f, -0.15f, -0.05f },
			};
			static_assert(sizeof(kMoods) / sizeof(kMoods[0]) == static_cast<size_t>(WeatherMoodId::Count), "one MoodDef per WeatherMoodId");
			const int32_t index = std::clamp(static_cast<int32_t>(mood), 0, static_cast<int32_t>(WeatherMoodId::Count) - 1);
			return kMoods[index];
		}

		// How far moods reach for each preset, and how much the per-roll layout
		// jitter (coverage / cirrus / wind heading) may move it. Storm-class
		// presets get NO coverage jitter: coverage 0.50-0.65 is the storm-tower
		// gate in CloudCommon (CloudStormSky), so +-0.03 would visibly grow or
		// collapse the towers.
		struct PresetMoodProfile
		{
			float reach = 0.0f;
			float coverageJitter = 0.0f; // +- cloud coverage
			float cirrusJitter = 0.0f;   // +- cirrus amount
			float windJitterDeg = 0.0f;  // +- wind heading
		};

		PresetMoodProfile GetPresetMoodProfile(WeatherPresetId presetId)
		{
			switch (presetId)
			{
			case WeatherPresetId::Clear:        return { 1.00f, 0.030f, 0.12f, 25.0f };
			case WeatherPresetId::Hot:          return { 1.00f, 0.030f, 0.12f, 25.0f };
			case WeatherPresetId::Overcast:     return { 0.60f, 0.020f, 0.00f, 25.0f };
			case WeatherPresetId::Rain:
			case WeatherPresetId::HeavyRain:    return { 0.50f, 0.015f, 0.00f, 20.0f };
			case WeatherPresetId::Storm:
			case WeatherPresetId::Thunderstorm: return { 0.70f, 0.000f, 0.00f, 20.0f };
			case WeatherPresetId::Snow:
			case WeatherPresetId::Blizzard:     return { 0.60f, 0.015f, 0.05f, 20.0f };
			case WeatherPresetId::Sandstorm:    return { 0.50f, 0.000f, 0.00f, 10.0f };
			case WeatherPresetId::Custom:
			default:                            return {};
			}
		}

		float MoodMul(float m, float k) { return 1.0f + (m - 1.0f) * k; }
		math::Vector3 MoodTint(const math::Vector3& t, float k)
		{
			return math::Vector3(MoodMul(t.x, k), MoodMul(t.y, k), MoodMul(t.z, k));
		}
	}

	const wchar_t* GetMoodDisplayName(WeatherMoodId mood)
	{
		switch (mood)
		{
		case WeatherMoodId::Golden: return L"Golden";
		case WeatherMoodId::Ember: return L"Ember";
		case WeatherMoodId::Crisp: return L"Crisp";
		case WeatherMoodId::Violet: return L"Violet";
		case WeatherMoodId::Hazy: return L"Hazy";
		case WeatherMoodId::Streaked: return L"Streaked";
		case WeatherMoodId::Standard:
		default: return L"Standard";
		}
	}

	const std::vector<WeatherMoodId>& GetPresetMoods(WeatherPresetId presetId)
	{
		using M = WeatherMoodId;
		static const std::vector<M> kClear    = { M::Standard, M::Golden, M::Ember, M::Crisp, M::Violet, M::Hazy, M::Streaked };
		static const std::vector<M> kHot      = { M::Standard, M::Golden, M::Ember, M::Hazy, M::Streaked };
		static const std::vector<M> kOvercast = { M::Standard, M::Golden, M::Crisp, M::Violet, M::Hazy };
		static const std::vector<M> kRain     = { M::Standard, M::Golden, M::Crisp, M::Hazy };
		// Crisp = the cold blue-grey storm, Golden = a copper break under the deck
		// (Ember pushed Storm's already-high Mie to ~3.9 and washed the deck out).
		static const std::vector<M> kStorm    = { M::Standard, M::Golden, M::Crisp, M::Violet };
		static const std::vector<M> kSnow     = { M::Standard, M::Golden, M::Crisp, M::Violet };
		static const std::vector<M> kSand     = { M::Standard, M::Ember, M::Hazy };
		static const std::vector<M> kNone;

		switch (presetId)
		{
		case WeatherPresetId::Clear:        return kClear;
		case WeatherPresetId::Hot:          return kHot;
		case WeatherPresetId::Overcast:     return kOvercast;
		case WeatherPresetId::Rain:
		case WeatherPresetId::HeavyRain:    return kRain;
		case WeatherPresetId::Storm:
		case WeatherPresetId::Thunderstorm: return kStorm;
		case WeatherPresetId::Snow:
		case WeatherPresetId::Blizzard:     return kSnow;
		case WeatherPresetId::Sandstorm:    return kSand;
		case WeatherPresetId::Custom:
		default:                            return kNone;
		}
	}

	void ApplyWeatherMood(WeatherState& state, WeatherPresetId presetId, const WeatherMoodRoll& roll, float strength)
	{
		const PresetMoodProfile profile = GetPresetMoodProfile(presetId);
		const float k = profile.reach * std::clamp(strength, 0.0f, 2.0f);
		if (k <= 0.0f)
			return;

		const MoodDef& m = GetMoodDef(roll.mood);
		const float j0 = std::clamp(roll.jitter[0], -1.0f, 1.0f);
		const float j1 = std::clamp(roll.jitter[1], -1.0f, 1.0f);
		const float j2 = std::clamp(roll.jitter[2], -1.0f, 1.0f);
		const float j3 = std::clamp(roll.jitter[3], -1.0f, 1.0f);

		// Sunset trims (env_sunset* range 0..4).
		state.sunsetWarmStrength = std::clamp(state.sunsetWarmStrength * MoodMul(m.warm, k) * (1.0f + 0.12f * j0 * k), 0.0f, 4.0f);
		state.sunsetCoolStrength = std::clamp(state.sunsetCoolStrength * MoodMul(m.cool, k), 0.0f, 4.0f);
		state.sunsetGlowStrength = std::clamp(state.sunsetGlowStrength * MoodMul(m.glow, k), 0.0f, 4.0f);

		// Atmosphere: feeds the Hillaire LUTs, so these change the sky all day
		// (a hazy day is hazy at noon too) and the sun's colour at the horizon.
		state.atmosphereDensity = std::clamp(state.atmosphereDensity * MoodMul(m.density, k), 0.02f, 4.0f);
		state.rayleighStrength = std::clamp(state.rayleighStrength * MoodMul(m.rayleigh, k), 0.0f, 4.0f);
		state.mieStrength = std::clamp(state.mieStrength * MoodMul(m.mie, k), 0.0f, 4.0f);
		state.sunHazeStrength = std::clamp(state.sunHazeStrength * MoodMul(m.haze, k), 0.0f, 4.0f);

		const math::Vector3 sunTint = MoodTint(m.sunTint, k);
		state.sunColour = math::Color(
			std::clamp(state.sunColour.x * sunTint.x, 0.0f, 2.0f),
			std::clamp(state.sunColour.y * sunTint.y, 0.0f, 2.0f),
			std::clamp(state.sunColour.z * sunTint.z, 0.0f, 2.0f),
			state.sunColour.w);
		state.sunIntensity = std::max(0.0f, state.sunIntensity * MoodMul(m.sunIntensity, k));

		const math::Vector3 ambientTint = MoodTint(m.ambientTint, k);
		state.ambientLight = math::Vector4(
			std::max(0.0f, state.ambientLight.x * ambientTint.x),
			std::max(0.0f, state.ambientLight.y * ambientTint.y),
			std::max(0.0f, state.ambientLight.z * ambientTint.z),
			state.ambientLight.w);

		const float fogMul = MoodMul(m.fogDensity, k);
		state.fogDensity = std::max(0.0f, state.fogDensity * fogMul);
		state.fogHeightDensity = std::max(0.0f, state.fogHeightDensity * fogMul);
		const math::Vector3 fogTint = MoodTint(m.fogTint, k);
		state.fogColour = math::Color(
			std::clamp(state.fogColour.x * fogTint.x, 0.0f, 1.0f),
			std::clamp(state.fogColour.y * fogTint.y, 0.0f, 1.0f),
			std::clamp(state.fogColour.z * fogTint.z, 0.0f, 1.0f),
			state.fogColour.w);

		// Cloud layout. Coverage offsets use the profile's jitter scale as a
		// gate too, so Storm (jitter 0) keeps its authored tower gate.
		if (profile.coverageJitter > 0.0f)
			state.cloudCoverage = std::clamp(state.cloudCoverage + m.coverageAdd * k + j1 * profile.coverageJitter * k, 0.01f, 1.0f);
		state.cirrusAmount = std::clamp(state.cirrusAmount + m.cirrusAdd * k + j2 * profile.cirrusJitter * k, 0.0f, 1.0f);
		state.cirrusType = std::clamp(state.cirrusType + m.cirrusTypeAdd * k, 0.0f, 1.0f);
		if (profile.coverageJitter > 0.0f)
			state.cloudType = std::clamp(state.cloudType + m.cloudTypeAdd * k, 0.0f, 1.0f);

		// Wind heading: clouds drift (and rain slants) a different way each day.
		if (profile.windJitterDeg > 0.0f && std::fabs(j3) > 0.0001f)
		{
			const float angle = j3 * profile.windJitterDeg * std::min(k / std::max(profile.reach, 0.0001f), 1.0f) * 0.0174532925f;
			const float c = std::cos(angle);
			const float s = std::sin(angle);
			const math::Vector3 w = state.windDirection;
			state.windDirection = math::Vector3(w.x * c - w.z * s, w.y, w.x * s + w.z * c);
		}
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
		file->Serialize(data, "cloudType", state.cloudType);
		file->Serialize(data, "cirrusAmount", state.cirrusAmount);
		file->Serialize(data, "cirrusType", state.cirrusType);
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
		file->Deserialize(data, "cloudType", state.cloudType);
		file->Deserialize(data, "cirrusAmount", state.cirrusAmount);
		file->Deserialize(data, "cirrusType", state.cirrusType);
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
