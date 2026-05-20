
#include "Settings.hpp"
#include "../../Editor.hpp"
#include "../EditorUI.hpp"
#include <algorithm>
#include <shlobj.h>

namespace HexEngine
{
	//extern HVar env_zenithExponent;
	extern HVar env_volumetricScattering;
	extern HVar env_volumetricStrength;
	extern HVar env_waterNormalInfluence;
	extern HVar env_volumetricSteps;
	extern HVar r_volumetricQuality;
	extern HVar r_fog;
	extern HVar r_fogDensity;
	extern HVar r_fogStartDistance;
	extern HVar r_fogHeightDensity;
	extern HVar r_fogHeightFalloff;
	extern HVar r_fogHeightPivot;
	extern HVar r_fogSkyTintInfluence;
	extern HVar r_fogFarDesaturate;
	extern HVar r_fogAtmosphereBlendStart;
	extern HVar r_fogAtmosphereBlendRange;
	extern HVar r_fogSunsetRange;
	extern HVar r_fogSunsetWarmthStrength;
	extern HVar r_fogFarAtmosphereMatchStrength;
}

namespace HexEditor
{
	namespace
	{
		struct AtmospherePresetValues
		{
			float anisotropicIntensity;
			float density;
			float rayleighStrength;
			float mieStrength;
			float ambientSkyStrength;
			float sunHazeStrength;
			float sunsetWarmStrength;
			float sunsetCoolStrength;
			float sunsetGlowStrength;
		};

		static HexEngine::HVar* FindNamedHVar(const char* name)
		{
			return HexEngine::g_pEnv ? HexEngine::g_pEnv->_commandManager->FindHVar(name) : nullptr;
		}

		static void SetNamedHVarFloat(const char* name, float value)
		{
			if (HexEngine::HVar* var = FindNamedHVar(name))
			{
				var->_val.f32 = value;
				var->Clamp();
			}
		}

		static int32_t GetNamedHVarInt(const char* name, int32_t fallback = 0)
		{
			if (HexEngine::HVar* var = FindNamedHVar(name))
				return var->_val.i32;
			return fallback;
		}

		static void SetNamedHVarInt(const char* name, int32_t value)
		{
			if (HexEngine::HVar* var = FindNamedHVar(name))
			{
				var->_val.i32 = value;
				var->Clamp();
			}
		}

		static void ApplyAtmospherePreset(int32_t presetIndex)
		{
			static const AtmospherePresetValues presets[] =
			{
				{ 0.38f, 0.11f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f }, // Custom / current defaults
				{ 0.30f, 0.085f, 0.78f, 0.50f, 0.72f, 0.62f, 1.18f, 1.10f, 1.08f }, // Crisp Alpine
				{ 0.34f, 0.105f, 0.70f, 0.72f, 0.88f, 0.78f, 1.35f, 1.22f, 1.18f }, // Warm Plains
				{ 0.26f, 0.070f, 0.62f, 0.38f, 0.60f, 0.48f, 1.10f, 1.30f, 1.12f }, // High Altitude Clear
				{ 0.33f, 0.095f, 0.74f, 0.64f, 0.78f, 0.86f, 1.55f, 1.38f, 1.35f }, // Golden Hour
			};

			const int32_t clampedPreset = std::clamp(presetIndex, 0, 4);
			SetNamedHVarInt("env_atmospherePreset", clampedPreset);

			const AtmospherePresetValues& preset = presets[clampedPreset];
			SetNamedHVarFloat("env_anisotropicIntensity", preset.anisotropicIntensity);
			SetNamedHVarFloat("env_density", preset.density);
			SetNamedHVarFloat("env_rayleighStrength", preset.rayleighStrength);
			SetNamedHVarFloat("env_mieStrength", preset.mieStrength);
			SetNamedHVarFloat("env_ambientSkyStrength", preset.ambientSkyStrength);
			SetNamedHVarFloat("env_sunHazeStrength", preset.sunHazeStrength);
			SetNamedHVarFloat("env_sunsetWarmStrength", preset.sunsetWarmStrength);
			SetNamedHVarFloat("env_sunsetCoolStrength", preset.sunsetCoolStrength);
			SetNamedHVarFloat("env_sunsetGlowStrength", preset.sunsetGlowStrength);
		}
	}

	Settings::Settings(Element* parent, const HexEngine::Point& position, const HexEngine::Point& size) :
		Dialog(parent, position, size, L"Scene Settings")
	{
		
	}

	Settings::~Settings()
	{
	}

	Settings* Settings::CreateSettingsDialog(Element* parent, OnCompleted onCompletedAction)
	{
		(void)onCompletedAction;

		uint32_t width, height;
		HexEngine::g_pEnv->GetScreenSize(width, height);

		int32_t centrex = width >> 1;
		int32_t centrey = height >> 1;

		const int32_t sizex = 800;
		const int32_t sizey = 480;

		Settings* pm = new Settings(parent, HexEngine::Point(centrex - sizex / 2, centrey - sizey / 2), HexEngine::Point(sizex, sizey));

		auto* tabs = new HexEngine::TabView(pm, HexEngine::Point(10, 10), HexEngine::Point(pm->_size.x - 20, pm->_size.y - 40));
		const int32_t tabHeaderHeight = HexEngine::g_pEnv->GetUIManager().GetRenderer()->_style.tab_height;

		const auto controlWidthFor = [](HexEngine::ComponentWidget* widget) {
			return std::max(120, widget->GetSize().x - 20);
		};

		const auto makeSectionTab = [&](const std::wstring& tabLabel, const std::wstring& sectionLabel) -> HexEngine::ComponentWidget*
		{
			auto* tab = tabs->AddTab(tabLabel);
			const int32_t tabOffsetX = tab->GetPosition().x;
			auto* scroll = new HexEngine::ScrollView(
				tab,
				HexEngine::Point(-tabOffsetX, tabHeaderHeight),
				HexEngine::Point(tab->GetSize().x, std::max(1, tab->GetSize().y - tabHeaderHeight)));

			auto* contentRoot = scroll->GetContentRoot();
			return new HexEngine::ComponentWidget(
				contentRoot,
				HexEngine::Point(10, 10),
				HexEngine::Point(scroll->GetSize().x - 20, 10),
				sectionLabel);
		};

		const auto addFloatControl = [&](HexEngine::ComponentWidget* widget, const char* name, const wchar_t* label, float precision, uint32_t decimals)
		{
			auto* var = HexEngine::g_pEnv->_commandManager->FindHVar(name);
			if (var == nullptr)
				return;

			new HexEngine::DragFloat(
				widget,
				widget->GetNextPos(),
				HexEngine::Point(controlWidthFor(widget), 18),
				label,
				&var->_val.f32,
				var->_min.f32,
				var->_max.f32,
				precision,
				decimals);
		};

		const auto addIntControl = [&](HexEngine::ComponentWidget* widget, const char* name, const wchar_t* label, int32_t step)
		{
			auto* var = HexEngine::g_pEnv->_commandManager->FindHVar(name);
			if (var == nullptr)
				return;

			new HexEngine::DragInt(
				widget,
				widget->GetNextPos(),
				HexEngine::Point(controlWidthFor(widget), 18),
				label,
				&var->_val.i32,
				var->_min.i32,
				var->_max.i32,
				step);
		};

		const auto addToggleControl = [&](HexEngine::ComponentWidget* widget, const char* name, const wchar_t* label)
		{
			auto* var = HexEngine::g_pEnv->_commandManager->FindHVar(name);
			if (var == nullptr)
				return;

			new HexEngine::Checkbox(
				widget,
				widget->GetNextPos(),
				HexEngine::Point(controlWidthFor(widget), 18),
				label,
				&var->_val.b);
		};

		const auto addVector3Control = [&](HexEngine::ComponentWidget* widget, const char* name, const wchar_t* label)
		{
			auto* var = HexEngine::g_pEnv->_commandManager->FindHVar(name);
			if (var == nullptr || var->GetType() != HexEngine::HVar::Type::Vector3)
				return;

			new HexEngine::Vector3Edit(
				widget,
				widget->GetNextPos(),
				HexEngine::Point(controlWidthFor(widget), 18),
				label,
				&var->_val.v3,
				[var](const math::Vector3& value)
				{
					var->_val.v3 = value;
					var->Clamp();
				});
		};

		pm->_widgetBase = makeSectionTab(L"Environment", L"Environment");
		auto* atmospherePreset = new HexEngine::DropDown(pm->_widgetBase, pm->_widgetBase->GetNextPos(), HexEngine::Point(controlWidthFor(pm->_widgetBase), 18), L"Atmosphere Preset");
		const auto setPresetLabel = [atmospherePreset](int32_t preset)
		{
			switch (preset)
			{
			case 1: atmospherePreset->SetValue(L"Crisp Alpine"); break;
			case 2: atmospherePreset->SetValue(L"Warm Plains"); break;
			case 3: atmospherePreset->SetValue(L"High Altitude Clear"); break;
			case 4: atmospherePreset->SetValue(L"Golden Hour"); break;
			case 0:
			default: atmospherePreset->SetValue(L"Custom"); break;
			}
		};
		setPresetLabel(GetNamedHVarInt("env_atmospherePreset", 0));
		atmospherePreset->GetContextMenu()->AddItem(new HexEngine::ContextItem(L"Custom",
			[setPresetLabel](const std::wstring&)
			{
				SetNamedHVarInt("env_atmospherePreset", 0);
				setPresetLabel(0);
			}));
		atmospherePreset->GetContextMenu()->AddItem(new HexEngine::ContextItem(L"Crisp Alpine",
			[setPresetLabel](const std::wstring&)
			{
				ApplyAtmospherePreset(1);
				setPresetLabel(1);
			}));
		atmospherePreset->GetContextMenu()->AddItem(new HexEngine::ContextItem(L"Warm Plains",
			[setPresetLabel](const std::wstring&)
			{
				ApplyAtmospherePreset(2);
				setPresetLabel(2);
			}));
		atmospherePreset->GetContextMenu()->AddItem(new HexEngine::ContextItem(L"High Altitude Clear",
			[setPresetLabel](const std::wstring&)
			{
				ApplyAtmospherePreset(3);
				setPresetLabel(3);
			}));
		atmospherePreset->GetContextMenu()->AddItem(new HexEngine::ContextItem(L"Golden Hour",
			[setPresetLabel](const std::wstring&)
			{
				ApplyAtmospherePreset(4);
				setPresetLabel(4);
			}));

		addFloatControl(pm->_widgetBase, "env_zenithExponent", L"Zenith Exponent", 0.01f, 3);
		addFloatControl(pm->_widgetBase, "env_anisotropicIntensity", L"Anisotropic Intensity", 0.01f, 3);
		addFloatControl(pm->_widgetBase, "env_density", L"Density", 0.01f, 3);
		addFloatControl(pm->_widgetBase, "env_rayleighStrength", L"Rayleigh Strength", 0.01f, 3);
		addFloatControl(pm->_widgetBase, "env_mieStrength", L"Mie Strength", 0.01f, 3);
		addFloatControl(pm->_widgetBase, "env_ambientSkyStrength", L"Ambient Sky Fill", 0.01f, 3);
		addFloatControl(pm->_widgetBase, "env_sunHazeStrength", L"Sun-side Haze", 0.01f, 3);
		addFloatControl(pm->_widgetBase, "env_sunsetWarmStrength", L"Sunset Warmth", 0.01f, 3);
		addFloatControl(pm->_widgetBase, "env_sunsetCoolStrength", L"Sunset Cool", 0.01f, 3);
		addFloatControl(pm->_widgetBase, "env_sunsetGlowStrength", L"Sunset Glow", 0.01f, 3);
		addFloatControl(pm->_widgetBase, "env_volumetricScattering", L"Volumetric Scattering", 0.01f, 3);
		addFloatControl(pm->_widgetBase, "env_volumetricStrength", L"Volumetric Strength", 0.01f, 3);
		addIntControl(pm->_widgetBase, "r_volumetricQuality", L"Volumetric Quality Preset", 1);
		addFloatControl(pm->_widgetBase, "env_volumetricSteps", L"Volumetric Steps", 1.0f, 3);
		addFloatControl(pm->_widgetBase, "env_volumetricStepIncrement", L"Volumetric Step Scale", 0.1f, 3);
		addFloatControl(pm->_widgetBase, "env_volumetricPointInsideMin", L"Point Inside Gain Min", 0.01f, 3);
		addFloatControl(pm->_widgetBase, "env_volumetricPointInsideMax", L"Point Inside Gain Max", 0.01f, 3);
		addFloatControl(pm->_widgetBase, "env_volumetricSpotInsideMin", L"Spot Inside Gain Min", 0.01f, 3);
		addFloatControl(pm->_widgetBase, "env_volumetricSpotInsideMax", L"Spot Inside Gain Max", 0.01f, 3);

		pm->_shadowSettings = makeSectionTab(L"Shadow", L"Shadow");
		addFloatControl(pm->_shadowSettings, "r_penumbraFilterMaxSize", L"Penumbra Filter Max Size", 0.0001f, 6);
		addFloatControl(pm->_shadowSettings, "r_shadowFilterMaxSize", L"Shadow Filter Max Size", 0.0001f, 6);
		addFloatControl(pm->_shadowSettings, "r_shadowBiasMultiplier", L"Shadow Bias Multiplier", 0.0001f, 6);
		addIntControl(pm->_shadowSettings, "r_shadowSamples", L"Shadow Samples", 1);

		pm->_colouring = makeSectionTab(L"Colouring", L"Colouring");
		addFloatControl(pm->_colouring, "r_contrast", L"Contrast", 0.01f, 3);
		addFloatControl(pm->_colouring, "r_exposure", L"Exposure", 0.01f, 3);
		addFloatControl(pm->_colouring, "r_hueShift", L"Hue Shift", 0.01f, 3);
		addFloatControl(pm->_colouring, "r_saturation", L"Saturatuin", 0.01f, 3);
		// Auto exposure controls. r_exposure above acts as a manual offset multiplied into the
		// auto-exposure result when r_autoExposure is on; when r_autoExposure is off, r_exposure
		// is the only exposure factor and the auto pass is bypassed entirely.
		addToggleControl(pm->_colouring, "r_autoExposure", L"Auto Exposure");
		addFloatControl(pm->_colouring, "r_autoExposureTargetLuma", L"Auto Exposure Target Luma", 0.01f, 3);
		addFloatControl(pm->_colouring, "r_autoExposureMin", L"Auto Exposure Min", 0.01f, 3);
		addFloatControl(pm->_colouring, "r_autoExposureMax", L"Auto Exposure Max", 0.05f, 3);
		addFloatControl(pm->_colouring, "r_autoExposureSpeed", L"Auto Exposure Adapt Speed", 0.05f, 2);
		addIntControl(pm->_colouring, "r_autoExposureSampleStride", L"Auto Exposure Sample Stride", 1);
		addFloatControl(pm->_colouring, "r_autoExposureNightTargetLuma", L"Auto Exposure Night Target Luma", 0.005f, 3);
		addFloatControl(pm->_colouring, "r_autoExposureNightMax", L"Auto Exposure Night Max", 0.05f, 2);
		addToggleControl(pm->_colouring, "r_autoExposureDebug", L"Auto Exposure Debug Log");

		pm->_fog = makeSectionTab(L"Fog", L"Fog");
		addToggleControl(pm->_fog, "r_fog", L"Fog on/off");
		addFloatControl(pm->_fog, "r_fogDensity", L"Fog Density", 0.0001f, 5);
		addFloatControl(pm->_fog, "r_fogStartDistance", L"Fog Start Distance", 0.5f, 2);
		addFloatControl(pm->_fog, "r_fogHeightDensity", L"Height Density", 0.0001f, 5);
		addFloatControl(pm->_fog, "r_fogHeightFalloff", L"Height Falloff", 0.0001f, 5);
		addFloatControl(pm->_fog, "r_fogHeightPivot", L"Height Pivot", 0.5f, 2);
		addFloatControl(pm->_fog, "r_fogSkyTintInfluence", L"Sky Tint Influence", 0.01f, 3);
		addFloatControl(pm->_fog, "r_fogFarDesaturate", L"Far Desaturate", 0.01f, 3);
		addFloatControl(pm->_fog, "r_fogAtmosphereBlendStart", L"Atmosphere Blend Start", 1.0f, 1);
		addFloatControl(pm->_fog, "r_fogAtmosphereBlendRange", L"Atmosphere Blend Range", 1.0f, 1);
		addFloatControl(pm->_fog, "r_fogSunsetRange", L"Sunset Range", 0.01f, 3);
		addFloatControl(pm->_fog, "r_fogSunsetWarmthStrength", L"Sunset Warmth", 0.01f, 3);
		addFloatControl(pm->_fog, "r_fogFarAtmosphereMatchStrength", L"Far Atmosphere Match", 0.01f, 3);

		pm->_clouds = makeSectionTab(L"Clouds", L"Volumetric Clouds");
		addToggleControl(pm->_clouds, "r_cloudEnable", L"Clouds on/off");
		addToggleControl(pm->_clouds, "r_cloudFollowCameraXZ", L"Follow Camera X/Z");
		addToggleControl(pm->_clouds, "r_cloudCastShadows", L"Cast Ground Shadows");
		addFloatControl(pm->_clouds, "r_cloudShadowStrength", L"Ground Shadow Strength", 0.01f, 3);
		addIntControl(pm->_clouds, "r_cloudShadowSteps", L"Ground Shadow Steps", 1);
		addIntControl(pm->_clouds, "r_cloudQuality", L"Quality Preset", 1);
		addVector3Control(pm->_clouds, "r_cloudAabbMin", L"AABB Min");
		addVector3Control(pm->_clouds, "r_cloudAabbMax", L"AABB Max");
		addFloatControl(pm->_clouds, "r_cloudDensity", L"Density", 0.01f, 3);
		addFloatControl(pm->_clouds, "r_cloudCoverage", L"Coverage", 0.01f, 3);
		addFloatControl(pm->_clouds, "r_cloudErosion", L"Erosion", 0.01f, 3);
		addFloatControl(pm->_clouds, "r_cloudShapeScale", L"Shape Scale", 0.00001f, 6);
		addFloatControl(pm->_clouds, "r_cloudDetailScale", L"Detail Scale", 0.00001f, 6);
		addVector3Control(pm->_clouds, "r_cloudWindDirection", L"Wind Direction");
		addFloatControl(pm->_clouds, "r_cloudWindSpeed", L"Wind Speed", 0.1f, 2);
		addFloatControl(pm->_clouds, "r_cloudAnimationSpeed", L"Animation Speed", 0.01f, 3);
		addFloatControl(pm->_clouds, "r_cloudLightAbsorption", L"Shadow Absorption", 0.01f, 3);
		addFloatControl(pm->_clouds, "r_cloudViewAbsorption", L"View Absorption (Opacity)", 0.01f, 3);
		addFloatControl(pm->_clouds, "r_cloudPowderStrength", L"Powder Strength", 0.01f, 3);
		addFloatControl(pm->_clouds, "r_cloudAmbientStrength", L"Ambient Strength", 0.01f, 3);
		addFloatControl(pm->_clouds, "r_cloudShadowFloor", L"Shadow Floor", 0.01f, 3);
		addFloatControl(pm->_clouds, "r_cloudAnisotropy", L"Anisotropy", 0.01f, 3);
		addFloatControl(pm->_clouds, "r_cloudSilverLiningStrength", L"Silver Lining Strength", 0.01f, 3);
		addFloatControl(pm->_clouds, "r_cloudSilverLiningExponent", L"Silver Lining Falloff", 0.05f, 3);
		addFloatControl(pm->_clouds, "r_cloudMultiScatterStrength", L"Multi-Scatter Lift", 0.01f, 3);
		addFloatControl(pm->_clouds, "r_cloudHeightTintStrength", L"Height Tint Strength", 0.01f, 3);
		addFloatControl(pm->_clouds, "r_cloudTintWarmth", L"Tint Warmth", 0.01f, 3);
		addFloatControl(pm->_clouds, "r_cloudSkyTintInfluence", L"Sky Tint Influence", 0.01f, 3);
		addFloatControl(pm->_clouds, "r_cloudDirectionalDiffuse", L"Directional Diffuse", 0.01f, 3);
		addFloatControl(pm->_clouds, "r_cloudAmbientOcclusion", L"Ambient Occlusion", 0.01f, 3);
		addFloatControl(pm->_clouds, "r_cloudViewSteps", L"View Steps", 1.0f, 0);
		addFloatControl(pm->_clouds, "r_cloudLightSteps", L"Light Steps", 1.0f, 0);
		addFloatControl(pm->_clouds, "r_cloudStepScale", L"Step Scale", 0.01f, 3);
		addFloatControl(pm->_clouds, "r_cloudMaxDistance", L"Max Distance", 1.0f, 1);

		auto* gi = makeSectionTab(L"GI", L"Global Illumination");
		addToggleControl(gi, "r_giEnable", L"Enable GI");
		addToggleControl(gi, "r_giGpuVoxelize", L"GPU Voxelization");
		addToggleControl(gi, "r_giUseProbes", L"Blend Probe GI");
		addToggleControl(gi, "r_giHalfRes", L"Half Resolution Trace");
		addIntControl(gi, "r_giQuality", L"Quality Preset", 1);
		addIntControl(gi, "r_giMovementPreset", L"Movement Preset", 1);
		addIntControl(gi, "r_giProbeBudget", L"Probe Budget", 1);
		addIntControl(gi, "r_giRaysPerProbe", L"Rays Per Probe", 1);
		addIntControl(gi, "r_giVoxelResolution", L"Voxel Resolution", 1);
		addFloatControl(gi, "r_giClipmapBaseExtent", L"Clipmap Base Extent", 1.0f, 2);
		addFloatControl(gi, "r_giIntensity", L"GI Intensity", 0.01f, 3);
		addFloatControl(gi, "r_giSunInjection", L"Sun Injection", 0.01f, 3);
		addFloatControl(gi, "r_giSunDirectionalBoost", L"Sun Directional Boost", 0.05f, 3);
		addFloatControl(gi, "r_giSunDirectionality", L"Sun Directionality", 0.01f, 3);
		addFloatControl(gi, "r_giDiffuseInjection", L"Diffuse Injection", 0.01f, 3);
		addFloatControl(gi, "r_giUnlitAlbedoInjection", L"Unlit Albedo Injection", 0.01f, 3);
		addFloatControl(gi, "r_giAlbedoBleedBoost", L"Albedo Bleed Boost", 0.05f, 3);
		addFloatControl(gi, "r_giColourBleedStrength", L"Colour Bleed Strength", 0.05f, 3);
		addFloatControl(gi, "r_giBounceAlbedoMinLuma", L"Bounce Min. Luma", 0.01f, 3);
		addFloatControl(gi, "r_giBounceAlbedoRemapAmount", L"Bounce Remap Amount", 0.01f, 3);
		addFloatControl(gi, "r_giReceiverMinLuma", L"Receiver Min Luma", 0.01f, 3);
		addFloatControl(gi, "r_giReceiverRemapAmount", L"Receiver Remap Amount", 0.01f, 3);
		addFloatControl(gi, "r_giEmissiveInjection", L"Emissive Injection", 0.01f, 3);
		addFloatControl(gi, "r_giLocalLightInjection", L"Local Light Injection", 0.01f, 3);
		addToggleControl(gi, "r_giDebugDisableLocalLightInjection", L"Debug Disable Local Light Injection");
		addToggleControl(gi, "r_giDebugDisableBaseAndSunInjection", L"Debug Disable Base+Sun Injection");
		addToggleControl(gi, "r_giDebugDisableBaseInjection", L"Debug Disable Base Injection");
		addToggleControl(gi, "r_giDebugDisableSunInjection", L"Debug Disable Sun Injection");
		addFloatControl(gi, "r_giMeshBaseInjectionNormalization", L"Mesh Base Injection Normalization", 0.01f, 3);
		addFloatControl(gi, "r_giMeshSunInjectionNormalization", L"Mesh Sun Injection Normalization", 0.01f, 3);
		addFloatControl(gi, "r_giMeshBaseInjectionMinScale", L"Mesh Base Injection Min Scale", 0.01f, 3);
		addFloatControl(gi, "r_giMeshSunInjectionMinScale", L"Mesh Sun Injection Min Scale", 0.01f, 3);
		addIntControl(gi, "r_giLocalLightMaxPerMesh", L"Local Light Max Per Mesh", 1);
		addFloatControl(gi, "r_giLocalLightBaseSuppression", L"Local Light Base Suppression", 0.01f, 3);
		addFloatControl(gi, "r_giLocalLightSunSuppression", L"Local Light Sun Suppression", 0.01f, 3);
		addFloatControl(gi, "r_giLocalLightAlbedoWeight", L"Local Light Albedo Weight", 0.01f, 3);
		addToggleControl(gi, "r_giLocalLightsOnlyDebug", L"Local Lights Only Debug");
		addFloatControl(gi, "r_giBaseSunSmallTriangleDamp", L"Base+Sun Small Tri Damp", 0.01f, 3);
		addFloatControl(gi, "r_giProbeGatherBoost", L"Probe Gather Boost", 0.01f, 3);
		addFloatControl(gi, "r_giScreenBounce", L"Screen Bounce", 0.01f, 3);
		addFloatControl(gi, "r_giVoxelDecay", L"Voxel Decay", 0.001f, 3);
		addFloatControl(gi, "r_giVoxelNeighbourBlend", L"Voxel Neighbour Blend", 0.01f, 3);
		addIntControl(gi, "r_giVoxelTriangleBudget", L"Voxel Triangle Budget", 1);
		addIntControl(gi, "r_giTriangleCacheFrames", L"Triangle Cache Frames", 1);
		addToggleControl(gi, "r_giUseTextureTint", L"Use Texture Tint (Slow)");
		addFloatControl(gi, "r_giEnergyClamp", L"Energy Clamp", 0.05f, 3);
		addFloatControl(gi, "r_giHysteresis", L"History Hysteresis", 0.005f, 3);
		addFloatControl(gi, "r_giHistoryReject", L"History Reject", 0.001f, 4);
		addFloatControl(gi, "r_giJitterScale", L"Jitter Scale", 0.01f, 3);
		addFloatControl(gi, "r_giClipBlendWidth", L"Clip Blend Width", 0.01f, 3);
		addFloatControl(gi, "r_giResolvePixelMotionStart", L"Resolve Motion Start", 0.05f, 3);
		addFloatControl(gi, "r_giResolvePixelMotionStrength", L"Resolve Motion Strength", 0.01f, 3);
		addFloatControl(gi, "r_giResolveLumaReject", L"Resolve Luma Reject", 0.01f, 3);
		addFloatControl(gi, "r_giResolveDitherDark", L"Resolve Dither Dark", 0.0001f, 4);
		addFloatControl(gi, "r_giResolveDitherBright", L"Resolve Dither Bright", 0.0001f, 4);
		addFloatControl(gi, "r_giGpuEdgeSmoothThreshold", L"Edge Smoothing Threshold", 0.001f, 4);
		addFloatControl(gi, "r_giGpuEdgeSmoothBlendStrength", L"Edge Smoothing Blend Strength", 0.001f, 4);
		addIntControl(gi, "r_giDebugView", L"Debug View", 1);

		auto* gpuCulling = makeSectionTab(L"GPU Culling", L"GPU Visibility Culling");
		addToggleControl(gpuCulling, "r_gpuCullEnable", L"Enable GPU Culling");
		addToggleControl(gpuCulling, "r_gpuCullFrustum", L"Enable Frustum Stage");
		addToggleControl(gpuCulling, "r_gpuCullOcclusion", L"Enable Occlusion Stage");
		addToggleControl(gpuCulling, "r_gpuCullOcclusionAggressive", L"Aggressive Occlusion");
		addToggleControl(gpuCulling, "r_gpuCullUseIndirectDraw", L"Use Indirect Draw");
		addToggleControl(gpuCulling, "r_gpuCullDepthPrepassFallback", L"Allow Depth Prepass Fallback");
		addToggleControl(gpuCulling, "r_gpuCullFreeze", L"Freeze Culling Results");
		addIntControl(gpuCulling, "r_gpuCullGraceFrames", L"Occlusion Grace Frames", 1);
		addIntControl(gpuCulling, "r_gpuCullOcclusionRejectFrames", L"Occlusion Reject Frames", 1);
		addIntControl(gpuCulling, "r_gpuCullOcclusionStableFrames", L"Occlusion Stable Frames", 1);
		addIntControl(gpuCulling, "r_gpuCullMinCandidates", L"Min Candidates", 1);
		addFloatControl(gpuCulling, "r_gpuCullFastCameraDistance", L"Fast Camera Distance", 0.05f, 3);
		addFloatControl(gpuCulling, "r_gpuCullFastCameraAngleDeg", L"Fast Camera Angle (Deg)", 0.1f, 2);
		addFloatControl(gpuCulling, "r_gpuCullNearBypassDistance", L"Near Bypass Distance", 0.05f, 3);
		addFloatControl(gpuCulling, "r_gpuCullLargeSphereBypass", L"Large Sphere Bypass Radius", 0.1f, 3);
		addFloatControl(gpuCulling, "r_gpuCullFrustumRadiusScale", L"Frustum Radius Scale", 0.01f, 3);
		addFloatControl(gpuCulling, "r_gpuCullOcclusionDepthBias", L"Occlusion Depth Bias", 0.0001f, 5);
		addToggleControl(gpuCulling, "r_gpuCullDebugBounds", L"Debug Draw Bounds");
		addToggleControl(gpuCulling, "r_gpuCullDebugFrustumRejected", L"Debug Frustum Rejected");
		addToggleControl(gpuCulling, "r_gpuCullDebugOcclusionRejected", L"Debug Occlusion Rejected");
		addToggleControl(gpuCulling, "r_gpuCullStatsLog", L"Log Culling Stats");

		pm->_ocean = makeSectionTab(L"Ocean", L"Ocean");
		auto& ocean = HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->GetOcean();
		new HexEngine::DragFloat(pm->_ocean, pm->_ocean->GetNextPos(), HexEngine::Point(controlWidthFor(pm->_ocean), 18), L"Fresnel Power", &ocean.fresnelPow, 0.1f, 10.0f, 0.1f);
		new HexEngine::DragFloat(pm->_ocean, pm->_ocean->GetNextPos(), HexEngine::Point(controlWidthFor(pm->_ocean), 18), L"Shore Fade Strength", &ocean.shoreFadeStrength, 0.1f, 50.0f, 0.1f);
		new HexEngine::DragFloat(pm->_ocean, pm->_ocean->GetNextPos(), HexEngine::Point(controlWidthFor(pm->_ocean), 18), L"Fade Factor", &ocean.fadeFactor, 0.1f, 50.0f, 0.1f);
		new HexEngine::DragFloat(pm->_ocean, pm->_ocean->GetNextPos(), HexEngine::Point(controlWidthFor(pm->_ocean), 18), L"Reflection Strength", &ocean.reflectionStrength, 0.1f, 1.0f, 0.01f);
		new HexEngine::DragFloat(pm->_ocean, pm->_ocean->GetNextPos(), HexEngine::Point(controlWidthFor(pm->_ocean), 18), L"Reflection Near Distance", &ocean.reflectionNearDistance, 1.0f, 2000.0f, 1.0f);
		new HexEngine::DragFloat(pm->_ocean, pm->_ocean->GetNextPos(), HexEngine::Point(controlWidthFor(pm->_ocean), 18), L"Reflection Far Distance", &ocean.reflectionFarDistance, 1.0f, 5000.0f, 1.0f);
		new HexEngine::ColourPicker(pm->_ocean, pm->_ocean->GetNextPos(), HexEngine::Point(controlWidthFor(pm->_ocean), 18), L"Deep Colour", (math::Color*)&ocean.deepColour.x);
		new HexEngine::ColourPicker(pm->_ocean, pm->_ocean->GetNextPos(), HexEngine::Point(controlWidthFor(pm->_ocean), 18), L"Shallow Colour", (math::Color*)&ocean.shallowColour.x);

		pm->_editor = makeSectionTab(L"Editor", L"Editor");
		if (auto snapEnabledVar = HexEngine::g_pEnv->_commandManager->FindHVar("ed_translateSnap"); snapEnabledVar != nullptr)
		{
			new HexEngine::Checkbox(
				pm->_editor,
				pm->_editor->GetNextPos(),
				HexEngine::Point(controlWidthFor(pm->_editor), 18),
				L"Grid Snap (Translate Gizmo)",
				&snapEnabledVar->_val.b);
		}

		if (auto snapSizeVar = HexEngine::g_pEnv->_commandManager->FindHVar("ed_translateSnapSize"); snapSizeVar != nullptr)
		{
			new HexEngine::DragFloat(
				pm->_editor,
				pm->_editor->GetNextPos(),
				HexEngine::Point(controlWidthFor(pm->_editor), 18),
				L"Grid Snap Size",
				&snapSizeVar->_val.f32,
				snapSizeVar->_min.f32,
				snapSizeVar->_max.f32,
				0.1f,
				3);
		}

		pm->BringToFront();

		return pm;
	}
}
