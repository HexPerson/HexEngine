
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
	Settings::Settings(Element* parent, const Point& position, const Point& size) :
		Dialog(parent, position, size, L"Scene Settings")
	{
		
	}

	Settings::~Settings()
	{
	}

	Settings* Settings::CreateSettingsDialog(Element* parent, OnCompleted onCompletedAction)
	{
		uint32_t width, height;
		g_pEnv->GetScreenSize(width, height);

		int32_t centrex = width >> 1;
		int32_t centrey = height >> 1;

		const int32_t sizex = 800;
		const int32_t sizey = 480;

		Settings* pm = new Settings(parent, Point(centrex - sizex / 2, centrey - sizey / 2), Point(sizex, sizey));

		auto layout = new ComponentWidget(pm, Point(10, 10), Point(pm->_size.x - 20, pm->_size.y - 20), L"");

		pm->_widgetBase = new ComponentWidget(layout, layout->GetNextPos(), Point(pm->_size.x - 20, 10), L"Environment");

#define ADD_CONTROL(widget, name,label,p, dp) { auto var = g_pEnv->_commandManager->FindHVar(name);\
DragFloat* df = new DragFloat(widget, widget->GetNextPos(), Point(sizex - 40, 18), label, &var->_val.f32, var->_min.f32, var->_max.f32, p, dp); }

#define ADD_CONTROL_TOGGLE(widget, name, label) { auto var = g_pEnv->_commandManager->FindHVar(name);\
Checkbox* df = new Checkbox(widget, widget->GetNextPos(), Point(sizex - 40, 18), label, &var->_max.b); }

		ADD_CONTROL(pm->_widgetBase, "env_zenithExponent", L"Zenith Exponent", 0.01f, 3);
		ADD_CONTROL(pm->_widgetBase, "env_anisotropicIntensity", L"Anisotropic Intensity", 0.01f, 3);
		ADD_CONTROL(pm->_widgetBase, "env_density", L"Density", 0.01f, 3);
		ADD_CONTROL(pm->_widgetBase, "env_volumetricScattering", L"Volumetric Scattering", 0.01f, 3);
		ADD_CONTROL(pm->_widgetBase, "env_volumetricSteps", L"Volumetric Steps", 1.0f, 3);
		ADD_CONTROL(pm->_widgetBase, "env_volumetricStepIncrement", L"Volumetric Step Distance", 0.1f, 3);

		pm->_shadowSettings = new ComponentWidget(layout, layout->GetNextPos(), Point(pm->_size.x - 20, 10), L"Shadow");

		ADD_CONTROL(pm->_shadowSettings, "r_penumbraFilterMaxSize", L"Penumbra Filter Max Size", 0.0001f, 6);
		ADD_CONTROL(pm->_shadowSettings, "r_shadowFilterMaxSize", L"Shadow Filter Max Size", 0.0001f, 6);
		ADD_CONTROL(pm->_shadowSettings, "r_shadowBiasMultiplier", L"Shadow Bias Multiplier", 0.0001f, 6);

		pm->_colouring = new ComponentWidget(layout, layout->GetNextPos(), Point(pm->_size.x - 20, 10), L"Colouring");

		ADD_CONTROL(pm->_colouring, "r_contrast", L"Contrast", 0.01f, 3);
		ADD_CONTROL(pm->_colouring, "r_exposure", L"Exposure", 0.01f, 3);
		ADD_CONTROL(pm->_colouring, "r_hueShift", L"Hue Shift", 0.01f, 3);
		ADD_CONTROL(pm->_colouring, "r_saturation", L"Saturatuin", 0.01f, 3);

		pm->_fog = new ComponentWidget(layout, layout->GetNextPos(), Point(pm->_size.x - 20, 10), L"Fog");

		ADD_CONTROL_TOGGLE(pm->_fog, "r_fog", L"Fog on/off");
		ADD_CONTROL(pm->_fog, "r_fogDensity", L"Fog Density", 0.0001f, 5);
		//ADD_CONTROL(pm->_fog, "r_exposure", L"Exposure", 0.01f, 3);
		//ADD_CONTROL(pm->_fog, "r_hueShift", L"Hue Shift", 0.01f, 3);
		//ADD_CONTROL(pm->_fog, "r_saturation", L"Saturatuin", 0.01f, 3);
		
		pm->_ocean = new ComponentWidget(layout, layout->GetNextPos(), Point(pm->_size.x - 20, 10), L"Ocean");

		auto& ocean = g_pEnv->_sceneManager->GetCurrentScene()->GetOcean();

		DragFloat* fp = new DragFloat(pm->_ocean, pm->_ocean->GetNextPos(), Point(sizex - 40, 18), L"Fresnel Power", &ocean.fresnelPow, 0.1f, 10.0f, 0.1f);
		DragFloat* sfs = new DragFloat(pm->_ocean, pm->_ocean->GetNextPos(), Point(sizex - 40, 18), L"Shore Fade Strength", &ocean.shoreFadeStrength, 0.1f, 50.0f, 0.1f);
		DragFloat* ff = new DragFloat(pm->_ocean, pm->_ocean->GetNextPos(), Point(sizex - 40, 18), L"Fade Factor", &ocean.fadeFactor, 0.1f, 50.0f, 0.1f);
		DragFloat* rs = new DragFloat(pm->_ocean, pm->_ocean->GetNextPos(), Point(sizex - 40, 18), L"Reflection Strength", &ocean.reflectionStrength, 0.1f, 1.0f, 0.01f);

		pm->BringToFront();
		//pm->_onCompleted = onCompletedAction;

		return pm;
	}
}