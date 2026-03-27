
#include "Settings.hpp"
#include "../../Editor.hpp"
#include "../EditorUI.hpp"
#include <algorithm>
#include <shlobj.h>

namespace HexEngine
{
	//extern HVar env_zenithExponent;
	extern HVar env_anisotropicIntensity;
	extern HVar env_density;
	extern HVar env_volumetricScattering;
	extern HVar env_volumetricStrength;
	extern HVar env_waterNormalInfluence;
	extern HVar env_volumetricSteps;
	extern HVar r_volumetricQuality;
	extern HVar r_fog;
	extern HVar r_fogDensity;
}

namespace HexEditor
{
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
			addFloatControl(pm->_widgetBase, "env_zenithExponent", L"Zenith Exponent", 0.01f, 3);
			addFloatControl(pm->_widgetBase, "env_anisotropicIntensity", L"Anisotropic Intensity", 0.01f, 3);
			addFloatControl(pm->_widgetBase, "env_density", L"Density", 0.01f, 3);
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

		pm->_colouring = makeSectionTab(L"Colouring", L"Colouring");
		addFloatControl(pm->_colouring, "r_contrast", L"Contrast", 0.01f, 3);
		addFloatControl(pm->_colouring, "r_exposure", L"Exposure", 0.01f, 3);
		addFloatControl(pm->_colouring, "r_hueShift", L"Hue Shift", 0.01f, 3);
		addFloatControl(pm->_colouring, "r_saturation", L"Saturatuin", 0.01f, 3);

		pm->_fog = makeSectionTab(L"Fog", L"Fog");
		addToggleControl(pm->_fog, "r_fog", L"Fog on/off");
		addFloatControl(pm->_fog, "r_fogDensity", L"Fog Density", 0.0001f, 5);

		pm->_clouds = makeSectionTab(L"Clouds", L"Volumetric Clouds");
		addToggleControl(pm->_clouds, "r_cloudEnable", L"Clouds on/off");
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
		addFloatControl(gi, "r_giEmissiveInjection", L"Emissive Injection", 0.01f, 3);
		addFloatControl(gi, "r_giLocalLightInjection", L"Local Light Injection", 0.01f, 3);
		addIntControl(gi, "r_giLocalLightMaxPerMesh", L"Local Light Max Per Mesh", 1);
		addFloatControl(gi, "r_giLocalLightBaseSuppression", L"Local Light Base Suppression", 0.01f, 3);
		addFloatControl(gi, "r_giLocalLightSunSuppression", L"Local Light Sun Suppression", 0.01f, 3);
		addFloatControl(gi, "r_giLocalLightAlbedoWeight", L"Local Light Albedo Weight", 0.01f, 3);
		addToggleControl(gi, "r_giLocalLightsOnlyDebug", L"Local Lights Only Debug");
		addFloatControl(gi, "r_giProbeGatherBoost", L"Probe Gather Boost", 0.01f, 3);
		addFloatControl(gi, "r_giScreenBounce", L"Screen Bounce", 0.01f, 3);
		addFloatControl(gi, "r_giVoxelDecay", L"Voxel Decay", 0.001f, 3);
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
		addIntControl(gi, "r_giDebugView", L"Debug View", 1);

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
