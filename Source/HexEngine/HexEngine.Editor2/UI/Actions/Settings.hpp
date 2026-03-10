
#pragma once

#include <HexEngine.Core\HexEngine.hpp>

namespace HexEditor
{
	class Settings : public HexEngine::Dialog
	{
	public:
		using OnCompleted = std::function<void(const fs::path&, const std::string&, bool)>;

		Settings(Element* parent, const HexEngine::Point& position, const HexEngine::Point& size);
		~Settings();

		static Settings* CreateSettingsDialog(Element* parent, OnCompleted onCompletedAction);

	private:
		HexEngine::ComponentWidget* _widgetBase = nullptr;
		HexEngine::ComponentWidget* _shadowSettings = nullptr;
		HexEngine::ComponentWidget* _colouring = nullptr;
		HexEngine::ComponentWidget* _fog = nullptr;
		HexEngine::ComponentWidget* _ocean = nullptr;
	};
}
