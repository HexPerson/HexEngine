
#pragma once

#include <HexEngine.Core\HexEngine.hpp>

namespace HexEditor
{
	// Engine settings dialog. Data-driven: enumerates the global HVar registry
	// (g_hvars), buckets every cvar into a tab by name prefix, and emits a
	// typed control per row (checkbox / drag-float / drag-int / vector3) with
	// the HVar's own description in smaller text beneath it. New cvars appear
	// automatically - the only maintenance surface is the prefix->tab table in
	// Settings.cpp, and anything unmatched lands on the Misc tab rather than
	// silently vanishing.
	class Settings : public HexEngine::Dialog
	{
	public:
		using OnCompleted = std::function<void(const fs::path&, const std::string&, bool)>;

		Settings(Element* parent, const HexEngine::Point& position, const HexEngine::Point& size);
		~Settings();

		static Settings* CreateSettingsDialog(Element* parent, OnCompleted onCompletedAction);
	};
}
