
#include "Settings.hpp"
#include "../../Editor.hpp"
#include "../EditorUI.hpp"
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
		uint32_t width, height;
		HexEngine::g_pEnv->GetScreenSize(width, height);

		int32_t centrex = width >> 1;
		int32_t centrey = height >> 1;

		const int32_t sizex = 800;
		const int32_t sizey = 480;

		Settings* pm = new Settings(parent, HexEngine::Point(centrex - sizex / 2, centrey - sizey / 2), HexEngine::Point(sizex, sizey));

		auto layout = new HexEngine::ComponentWidget(pm, HexEngine::Point(10, 10), HexEngine::Point(pm->_size.x - 20, pm->_size.y - 20), L"");

		pm->_widgetBase = new HexEngine::ComponentWidget(layout, layout->GetNextPos(), HexEngine::Point(pm->_size.x - 20, 10), L"Environment");

#define ADD_CONTROL(widget, name,label,p, dp) { auto var = HexEngine::g_pEnv->_commandManager->FindHVar(name);\
HexEngine::DragFloat* df = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(sizex - 40, 18), label, &var->_val.f32, var->_min.f32, var->_max.f32, p, dp); }

#define ADD_CONTROL_TOGGLE(widget, name, label) { auto var = HexEngine::g_pEnv->_commandManager->FindHVar(name);\
HexEngine::Checkbox* df = new HexEngine::Checkbox(widget, widget->GetNextPos(), HexEngine::Point(sizex - 40, 18), label, &var->_max.b); }

		ADD_CONTROL(pm->_widgetBase, "env_zenithExponent", L"Zenith Exponent", 0.01f, 3);
		ADD_CONTROL(pm->_widgetBase, "env_anisotropicIntensity", L"Anisotropic Intensity", 0.01f, 3);
		ADD_CONTROL(pm->_widgetBase, "env_density", L"Density", 0.01f, 3);
		ADD_CONTROL(pm->_widgetBase, "env_volumetricScattering", L"Volumetric Scattering", 0.01f, 3);
		ADD_CONTROL(pm->_widgetBase, "env_volumetricSteps", L"Volumetric Steps", 1.0f, 3);
		ADD_CONTROL(pm->_widgetBase, "env_volumetricStepIncrement", L"Volumetric Step Distance", 0.1f, 3);

		pm->_shadowSettings = new HexEngine::ComponentWidget(layout, layout->GetNextPos(), HexEngine::Point(pm->_size.x - 20, 10), L"Shadow");

		ADD_CONTROL(pm->_shadowSettings, "r_penumbraFilterMaxSize", L"Penumbra Filter Max Size", 0.0001f, 6);
		ADD_CONTROL(pm->_shadowSettings, "r_shadowFilterMaxSize", L"Shadow Filter Max Size", 0.0001f, 6);
		ADD_CONTROL(pm->_shadowSettings, "r_shadowBiasMultiplier", L"Shadow Bias Multiplier", 0.0001f, 6);

		pm->_colouring = new HexEngine::ComponentWidget(layout, layout->GetNextPos(), HexEngine::Point(pm->_size.x - 20, 10), L"Colouring");

		ADD_CONTROL(pm->_colouring, "r_contrast", L"Contrast", 0.01f, 3);
		ADD_CONTROL(pm->_colouring, "r_exposure", L"Exposure", 0.01f, 3);
		ADD_CONTROL(pm->_colouring, "r_hueShift", L"Hue Shift", 0.01f, 3);
		ADD_CONTROL(pm->_colouring, "r_saturation", L"Saturatuin", 0.01f, 3);

		pm->_fog = new HexEngine::ComponentWidget(layout, layout->GetNextPos(), HexEngine::Point(pm->_size.x - 20, 10), L"Fog");

		ADD_CONTROL_TOGGLE(pm->_fog, "r_fog", L"Fog on/off");
		ADD_CONTROL(pm->_fog, "r_fogDensity", L"Fog Density", 0.0001f, 5);
		//ADD_CONTROL(pm->_fog, "r_exposure", L"Exposure", 0.01f, 3);
		//ADD_CONTROL(pm->_fog, "r_hueShift", L"Hue Shift", 0.01f, 3);
		//ADD_CONTROL(pm->_fog, "r_saturation", L"Saturatuin", 0.01f, 3);
		
		pm->_ocean = new HexEngine::ComponentWidget(layout, layout->GetNextPos(), HexEngine::Point(pm->_size.x - 20, 10), L"Ocean");

		auto& ocean = HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->GetOcean();

		HexEngine::DragFloat* fp = new HexEngine::DragFloat(pm->_ocean, pm->_ocean->GetNextPos(), HexEngine::Point(sizex - 40, 18), L"Fresnel Power", &ocean.fresnelPow, 0.1f, 10.0f, 0.1f);
		HexEngine::DragFloat* sfs = new HexEngine::DragFloat(pm->_ocean, pm->_ocean->GetNextPos(), HexEngine::Point(sizex - 40, 18), L"Shore Fade Strength", &ocean.shoreFadeStrength, 0.1f, 50.0f, 0.1f);
		HexEngine::DragFloat* ff = new HexEngine::DragFloat(pm->_ocean, pm->_ocean->GetNextPos(), HexEngine::Point(sizex - 40, 18), L"Fade Factor", &ocean.fadeFactor, 0.1f, 50.0f, 0.1f);
		HexEngine::DragFloat* rs = new HexEngine::DragFloat(pm->_ocean, pm->_ocean->GetNextPos(), HexEngine::Point(sizex - 40, 18), L"Reflection Strength", &ocean.reflectionStrength, 0.1f, 1.0f, 0.01f);
		HexEngine::DragFloat* rnd = new HexEngine::DragFloat(pm->_ocean, pm->_ocean->GetNextPos(), HexEngine::Point(sizex - 40, 18), L"Reflection Near Distance", &ocean.reflectionNearDistance, 1.0f, 2000.0f, 1.0f);
		HexEngine::DragFloat* rfd = new HexEngine::DragFloat(pm->_ocean, pm->_ocean->GetNextPos(), HexEngine::Point(sizex - 40, 18), L"Reflection Far Distance", &ocean.reflectionFarDistance, 1.0f, 5000.0f, 1.0f);

		HexEngine::ColourPicker* deepCol = new HexEngine::ColourPicker(pm->_ocean, pm->_ocean->GetNextPos(), HexEngine::Point(sizex - 40, 18), L"Deep Colour", (math::Color*)&ocean.deepColour.x);
		HexEngine::ColourPicker* shallowCol = new HexEngine::ColourPicker(pm->_ocean, pm->_ocean->GetNextPos(), HexEngine::Point(sizex - 40, 18), L"Shallow Colour", (math::Color*)&ocean.shallowColour.x);

		pm->_editor = new HexEngine::ComponentWidget(layout, layout->GetNextPos(), HexEngine::Point(pm->_size.x - 20, 10), L"Editor");
		if (auto snapEnabledVar = HexEngine::g_pEnv->_commandManager->FindHVar("ed_translateSnap"); snapEnabledVar != nullptr)
		{
			new HexEngine::Checkbox(
				pm->_editor,
				pm->_editor->GetNextPos(),
				HexEngine::Point(sizex - 40, 18),
				L"Grid Snap (Translate Gizmo)",
				&snapEnabledVar->_val.b);
		}

		if (auto snapSizeVar = HexEngine::g_pEnv->_commandManager->FindHVar("ed_translateSnapSize"); snapSizeVar != nullptr)
		{
			new HexEngine::DragFloat(
				pm->_editor,
				pm->_editor->GetNextPos(),
				HexEngine::Point(sizex - 40, 18),
				L"Grid Snap Size",
				&snapSizeVar->_val.f32,
				snapSizeVar->_min.f32,
				snapSizeVar->_max.f32,
				0.1f,
				3);
		}

		pm->BringToFront();
		//pm->_onCompleted = onCompletedAction;

		return pm;
	}
}
