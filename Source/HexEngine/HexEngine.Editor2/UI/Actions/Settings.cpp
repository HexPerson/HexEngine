
#include "Settings.hpp"
#include "../../Editor.hpp"
#include "../EditorUI.hpp"
#include <algorithm>
#include <shlobj.h>

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

		// ASCII widen - cvar names/descriptions are plain ASCII by convention.
		static std::wstring Widen(const std::string& text)
		{
			return std::wstring(text.begin(), text.end());
		}

		// ------------------------------------------------------------------
		// Small dimmed multi-line text element for the HVar description that
		// sits under each control row. Word-wraps to its width at construction
		// (the wrapped height must be known before Element's ctor runs, since
		// ComponentWidget snapshots the child height in OnAddChild).
		// ------------------------------------------------------------------
		class HVarDescription : public HexEngine::Element
		{
		public:
			static constexpr int32_t kFontSize = (int32_t)HexEngine::Style::FontSize::Titchy;
			static constexpr int32_t kLineHeight = 14;

			static std::vector<std::wstring> WrapLines(int32_t width, const std::wstring& text)
			{
				std::vector<std::wstring> lines;
				auto* renderer = HexEngine::g_pEnv->GetUIManager().GetRenderer();
				auto* font = renderer != nullptr ? renderer->_style.font.get() : nullptr;
				if (font == nullptr || text.empty())
					return lines;

				std::wstring current;
				std::wstring word;
				const auto flushWord = [&]()
				{
					if (word.empty())
						return;
					std::wstring candidate = current.empty() ? word : current + L" " + word;
					int32_t w = 0, h = 0;
					font->MeasureText(kFontSize, candidate, w, h);
					if (w > width && !current.empty())
					{
						lines.push_back(current);
						current = word;
					}
					else
					{
						current = candidate;
					}
					word.clear();
				};

				for (wchar_t c : text)
				{
					if (c == L' ' || c == L'\n')
					{
						flushWord();
						if (c == L'\n' && !current.empty())
						{
							lines.push_back(current);
							current.clear();
						}
					}
					else
					{
						word += c;
					}
				}
				flushWord();
				if (!current.empty())
					lines.push_back(current);
				return lines;
			}

			static int32_t MeasureHeight(int32_t width, const std::wstring& text)
			{
				const size_t lineCount = WrapLines(width, text).size();
				return (int32_t)lineCount * kLineHeight;
			}

			HVarDescription(Element* parent, const HexEngine::Point& position, int32_t width, const std::wstring& text) :
				Element(parent, position, HexEngine::Point(width, std::max(kLineHeight, MeasureHeight(width, text)))),
				_lines(WrapLines(width, text))
			{
				// Descriptions are informational - never eat clicks meant for
				// the controls around them.
				EnableInput(false);
			}

			virtual void Render(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h) override
			{
				const auto pos = GetAbsolutePosition();
				const math::Color& base = renderer->_style.text_regular;
				const math::Color dim(base.x, base.y, base.z, base.w * 0.55f);
				for (size_t i = 0; i < _lines.size(); ++i)
				{
					renderer->PrintText(
						renderer->_style.font.get(),
						(uint8_t)kFontSize,
						pos.x, pos.y + (int32_t)i * kLineHeight,
						dim,
						HexEngine::FontAlign::None,
						_lines[i]);
				}
			}

		private:
			std::vector<std::wstring> _lines;
		};

		// ------------------------------------------------------------------
		// Prefix -> tab classification. Checked IN ORDER, first match wins, so
		// specific prefixes (env_volumetric) must precede general ones (env_).
		// Anything unmatched lands on the Misc tab.
		// ------------------------------------------------------------------
		struct TabDef
		{
			const wchar_t* label;
		};

		enum TabIndex : size_t
		{
			TabDisplay = 0, TabPostFx, TabReflections, TabLighting, TabShadows,
			TabGi, TabFogVol, TabAtmosphere, TabClouds, TabWeather, TabWater,
			TabWorld, TabPerf, TabEditor, TabMisc,
			TabCount
		};

		static const TabDef kTabs[TabCount] =
		{
			{ L"Display" }, { L"Post FX" }, { L"Reflections" }, { L"Lighting" }, { L"Shadows" },
			{ L"GI" }, { L"Fog/Vol" }, { L"Atmos" }, { L"Clouds" }, { L"Weather" }, { L"Water" },
			{ L"World" }, { L"Perf" }, { L"Editor" }, { L"Misc" },
		};

		struct PrefixRule
		{
			const char* prefix;
			size_t tab;
		};

		static const PrefixRule kRules[] =
		{
			// Specific rules first - these would otherwise be swallowed by a
			// broader prefix further down.
			{ "env_volumetric",       TabFogVol },
			{ "env_water",            TabWater },
			{ "r_volumetric",         TabFogVol },
			{ "r_fog",                TabFogVol },
			{ "r_froxel",             TabFogVol },

			{ "r_cloud",              TabClouds },
			{ "r_gi",                 TabGi },
			{ "r_useGIAO",            TabGi },

			{ "r_ssr",                TabReflections },
			{ "r_ibl",                TabReflections },
			{ "r_nrd",                TabReflections },
			{ "r_reflection",         TabReflections },

			{ "r_shadow",             TabShadows },
			{ "r_penumbra",           TabShadows },
			{ "r_contactShadow",      TabShadows },
			{ "r_pointShadow",        TabShadows },
			{ "r_spotShadow",         TabShadows },
			{ "r_sunAngularDiameter", TabShadows },

			{ "r_hdr",                TabDisplay },
			{ "r_tonemap",            TabDisplay },
			{ "r_taa",                TabDisplay },
			{ "r_fxaa",               TabDisplay },
			{ "r_dlss",               TabDisplay },
			{ "r_sharpen",            TabDisplay },
			{ "r_vsync",              TabDisplay },
			{ "r_fullscreen",         TabDisplay },
			{ "r_resolution",         TabDisplay },

			{ "r_bloom",              TabPostFx },
			{ "r_motionBlur",         TabPostFx },
			{ "r_dof",                TabPostFx },
			{ "r_bokeh",              TabPostFx },
			{ "r_vignette",           TabPostFx },
			{ "r_chromatic",          TabPostFx },
			{ "r_filmGrain",          TabPostFx },
			{ "r_grain",              TabPostFx },
			{ "r_colourLut",          TabPostFx },
			{ "r_colorLut",           TabPostFx },
			{ "r_lens",               TabPostFx },
			{ "r_cas",                TabPostFx },
			{ "r_contrast",           TabPostFx },
			{ "r_exposure",           TabPostFx },
			{ "r_autoExposure",       TabPostFx },
			{ "r_hue",                TabPostFx },
			{ "r_saturation",         TabPostFx },
			{ "r_whiteBalance",       TabPostFx },
			{ "r_lift",               TabPostFx },
			{ "r_gamma",              TabPostFx },
			{ "r_gain",               TabPostFx },

			{ "r_cluster",            TabLighting },
			{ "r_light",              TabLighting },
			{ "r_physicalLightUnits", TabLighting },
			{ "r_legacyLightScale",   TabLighting },
			{ "r_emissive",           TabLighting },
			{ "r_sss",                TabLighting },
			{ "r_ssao",               TabLighting },
			{ "r_ambient",            TabLighting },
			{ "r_forward",            TabLighting },
			{ "r_deferred",           TabLighting },

			{ "r_weather",            TabWeather },
			{ "r_snow",               TabWeather },
			{ "r_wet",                TabWeather },
			{ "r_puddle",             TabWeather },
			{ "r_autoPuddles",        TabWeather },
			{ "r_rain",               TabWeather },
			{ "r_drip",               TabWeather },
			{ "r_wind",               TabWeather },
			{ "r_dust",               TabWeather },
			{ "r_sand",               TabWeather },
			{ "r_shelter",            TabWeather },
			{ "r_footprint",          TabWeather },

			{ "r_ocean",              TabWater },
			{ "r_water",              TabWater },

			{ "r_terrain",            TabWorld },
			{ "r_grass",              TabWorld },
			{ "r_vegetation",         TabWorld },
			{ "r_hlod",               TabWorld },
			{ "r_lod",                TabWorld },
			{ "r_particle",           TabWorld },
			{ "r_decal",              TabWorld },
			{ "r_mesh",               TabWorld },
			{ "r_anim",               TabWorld },

			{ "r_gpuCull",            TabPerf },
			{ "r_profile",            TabPerf },
			{ "r_hzb",                TabPerf },
			{ "r_instance",           TabPerf },
			{ "r_batch",              TabPerf },
			{ "cl_",                  TabPerf },

			{ "ed_",                  TabEditor },

			// General atmosphere catch-alls LAST among the matchers.
			{ "env_",                 TabAtmosphere },
			{ "r_atmosphere",         TabAtmosphere },
			{ "r_sky",                TabAtmosphere },
			{ "r_sun",                TabAtmosphere },
			{ "r_moon",               TabAtmosphere },
			{ "r_star",               TabAtmosphere },
		};

		static size_t ClassifyHVar(const std::string& name)
		{
			for (const auto& rule : kRules)
			{
				if (name.rfind(rule.prefix, 0) == 0)
					return rule.tab;
			}
			return TabMisc;
		}

		// HVars owned by bespoke controls (dropdowns with named entries) -
		// excluded from the auto rows so they don't appear twice.
		static bool IsExcludedFromAutoRows(const std::string& name)
		{
			return name == "env_atmospherePreset" || name == "r_tonemapOperator";
		}
	}

	Settings::Settings(Element* parent, const HexEngine::Point& position, const HexEngine::Point& size) :
		Dialog(parent, position, size, L"Engine Settings")
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

		// As large as fits comfortably: the dialog carries every cvar in the
		// engine now, and screen real estate is what makes that browsable.
		const int32_t sizex = std::min<int32_t>(1280, (int32_t)width - 60);
		const int32_t sizey = std::min<int32_t>(820, (int32_t)height - 60);

		const int32_t centrex = (int32_t)width >> 1;
		const int32_t centrey = (int32_t)height >> 1;

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

		// --------------------------------------------------------------
		// Automatic typed row for one HVar: control labelled with the cvar
		// name, HVar description in small dimmed text underneath. Drag
		// stepping/precision derive from the registered [min, max] range.
		// --------------------------------------------------------------
		const auto addAutoRow = [&](HexEngine::ComponentWidget* widget, HexEngine::HVar* var)
		{
			const int32_t cw = controlWidthFor(widget);
			const std::wstring label = Widen(var->_name);

			switch (var->GetType())
			{
			case HexEngine::HVar::Type::Bool:
			{
				new HexEngine::Checkbox(widget, widget->GetNextPos(), HexEngine::Point(cw, 18), label, &var->_val.b);
				break;
			}
			case HexEngine::HVar::Type::Float32:
			{
				const float range = var->_max.f32 - var->_min.f32;
				const float step = range > 0.0f ? range / 300.0f : 0.01f;
				uint32_t decimals;
				if      (step >= 1.0f)     decimals = 1;
				else if (step >= 0.1f)     decimals = 2;
				else if (step >= 0.01f)    decimals = 3;
				else if (step >= 0.001f)   decimals = 4;
				else if (step >= 0.0001f)  decimals = 5;
				else                       decimals = 6;

				new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(cw, 18),
					label, &var->_val.f32, var->_min.f32, var->_max.f32, step, decimals);
				break;
			}
			case HexEngine::HVar::Type::Int32:
			{
				new HexEngine::DragInt(widget, widget->GetNextPos(), HexEngine::Point(cw, 18),
					label, &var->_val.i32, var->_min.i32, var->_max.i32, 1);
				break;
			}
			case HexEngine::HVar::Type::UInt32:
			{
				// The value union aliases; ranges registered on uint cvars are
				// small enough that int32 editing is safe.
				new HexEngine::DragInt(widget, widget->GetNextPos(), HexEngine::Point(cw, 18),
					label, &var->_val.i32, (int32_t)var->_min.ui32, (int32_t)var->_max.ui32, 1);
				break;
			}
			case HexEngine::HVar::Type::Vector3:
			{
				new HexEngine::Vector3Edit(widget, widget->GetNextPos(), HexEngine::Point(cw, 18),
					label, &var->_val.v3,
					[var](const math::Vector3& value)
					{
						var->_val.v3 = value;
						var->Clamp();
					});
				break;
			}
			default:
				return; // unsupported type - no row
			}

			if (!var->_description.empty())
			{
				auto rowPos = widget->GetNextPos();
				rowPos.x += 14; // indent under the control it describes
				new HVarDescription(widget, rowPos, cw - 14, Widen(var->_description));
			}
		};

		// --------------------------------------------------------------
		// Create every tab up front (fixed order), then bucket the whole HVar
		// registry into them.
		// --------------------------------------------------------------
		HexEngine::ComponentWidget* tabWidgets[TabCount] = {};
		for (size_t i = 0; i < TabCount; ++i)
			tabWidgets[i] = makeSectionTab(kTabs[i].label, kTabs[i].label);

		// ---- Bespoke controls first, so they sit at the top of their tabs.

		// Atmosphere preset dropdown (writes a family of env_ cvars).
		{
			auto* atmosWidget = tabWidgets[TabAtmosphere];
			auto* atmospherePreset = new HexEngine::DropDown(atmosWidget, atmosWidget->GetNextPos(),
				HexEngine::Point(controlWidthFor(atmosWidget), 18), L"Atmosphere Preset");
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
			const auto addPresetItem = [&](const std::wstring& itemLabel, int32_t preset)
			{
				atmospherePreset->GetContextMenu()->AddItem(new HexEngine::ContextItem(itemLabel,
					[setPresetLabel, preset](const std::wstring&)
					{
						if (preset == 0)
							SetNamedHVarInt("env_atmospherePreset", 0);
						else
							ApplyAtmospherePreset(preset);
						setPresetLabel(preset);
					}));
			};
			addPresetItem(L"Custom", 0);
			addPresetItem(L"Crisp Alpine", 1);
			addPresetItem(L"Warm Plains", 2);
			addPresetItem(L"High Altitude Clear", 3);
			addPresetItem(L"Golden Hour", 4);
		}

		// Tonemap operator dropdown (named entries for an int cvar). Keep the
		// labels in sync with TonemapOperators.shader's switch statement.
		{
			auto* displayWidget = tabWidgets[TabDisplay];
			auto* tonemapDropdown = new HexEngine::DropDown(displayWidget, displayWidget->GetNextPos(),
				HexEngine::Point(controlWidthFor(displayWidget), 18), L"Tonemap Operator");
			const auto setTonemapLabel = [tonemapDropdown](int32_t op)
			{
				switch (op)
				{
				case 0: tonemapDropdown->SetValue(L"Reinhard"); break;
				case 1: tonemapDropdown->SetValue(L"Reinhard Extended"); break;
				case 2: tonemapDropdown->SetValue(L"ACES (Fitted)"); break;
				case 3: tonemapDropdown->SetValue(L"Uncharted 2 / Hable"); break;
				case 4: tonemapDropdown->SetValue(L"Lottes"); break;
				case 5: tonemapDropdown->SetValue(L"Linear (debug)"); break;
				default: tonemapDropdown->SetValue(L"ACES (Fitted)"); break;
				}
			};
			setTonemapLabel(GetNamedHVarInt("r_tonemapOperator", 2));
			const auto addTonemapItem = [&](const std::wstring& itemLabel, int32_t op)
			{
				tonemapDropdown->GetContextMenu()->AddItem(new HexEngine::ContextItem(itemLabel,
					[setTonemapLabel, op](const std::wstring&)
					{
						SetNamedHVarInt("r_tonemapOperator", op);
						setTonemapLabel(op);
					}));
			};
			addTonemapItem(L"Reinhard", 0);
			addTonemapItem(L"Reinhard Extended", 1);
			addTonemapItem(L"ACES (Fitted)", 2);
			addTonemapItem(L"Uncharted 2 / Hable", 3);
			addTonemapItem(L"Lottes", 4);
			addTonemapItem(L"Linear (debug)", 5);
		}

		// Ocean per-scene settings (a struct on the Scene, not HVars).
		{
			auto* waterWidget = tabWidgets[TabWater];
			auto& ocean = HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->GetOcean();
			const int32_t cw = controlWidthFor(waterWidget);
			new HexEngine::DragFloat(waterWidget, waterWidget->GetNextPos(), HexEngine::Point(cw, 18), L"Fresnel Power", &ocean.fresnelPow, 0.1f, 10.0f, 0.1f);
			new HexEngine::DragFloat(waterWidget, waterWidget->GetNextPos(), HexEngine::Point(cw, 18), L"Shore Fade Strength", &ocean.shoreFadeStrength, 0.1f, 50.0f, 0.1f);
			new HexEngine::DragFloat(waterWidget, waterWidget->GetNextPos(), HexEngine::Point(cw, 18), L"Fade Factor", &ocean.fadeFactor, 0.1f, 50.0f, 0.1f);
			new HexEngine::DragFloat(waterWidget, waterWidget->GetNextPos(), HexEngine::Point(cw, 18), L"Reflection Strength", &ocean.reflectionStrength, 0.1f, 1.0f, 0.01f);
			new HexEngine::DragFloat(waterWidget, waterWidget->GetNextPos(), HexEngine::Point(cw, 18), L"Reflection Near Distance", &ocean.reflectionNearDistance, 1.0f, 2000.0f, 1.0f);
			new HexEngine::DragFloat(waterWidget, waterWidget->GetNextPos(), HexEngine::Point(cw, 18), L"Reflection Far Distance", &ocean.reflectionFarDistance, 1.0f, 5000.0f, 1.0f);
			new HexEngine::ColourPicker(waterWidget, waterWidget->GetNextPos(), HexEngine::Point(cw, 18), L"Deep Colour", (math::Color*)&ocean.deepColour.x);
			new HexEngine::ColourPicker(waterWidget, waterWidget->GetNextPos(), HexEngine::Point(cw, 18), L"Shallow Colour", (math::Color*)&ocean.shallowColour.x);
		}

		// ---- Auto rows: the entire HVar registry, bucketed and sorted.
		{
			std::vector<HexEngine::HVar*> buckets[TabCount];
			for (HexEngine::HVar* var = HexEngine::g_hvars; var != nullptr; var = var->_next)
			{
				if (IsExcludedFromAutoRows(var->_name))
					continue;
				buckets[ClassifyHVar(var->_name)].push_back(var);
			}

			for (size_t tabIdx = 0; tabIdx < TabCount; ++tabIdx)
			{
				auto& bucket = buckets[tabIdx];
				std::sort(bucket.begin(), bucket.end(),
					[](const HexEngine::HVar* a, const HexEngine::HVar* b) { return a->_name < b->_name; });

				for (HexEngine::HVar* var : bucket)
					addAutoRow(tabWidgets[tabIdx], var);
			}
		}

		pm->BringToFront();

		return pm;
	}
}
