
#pragma once

#include <HexEngine.Core\HexEngine.hpp>

namespace HexEditor
{
	class Settings : public Dialog
	{
	public:
		using OnCompleted = std::function<void(const fs::path&, const std::string&, bool)>;

		Settings(Element* parent, const Point& position, const Point& size);
		~Settings();

		static Settings* CreateSettingsDialog(Element* parent, OnCompleted onCompletedAction);

	private:
		ComponentWidget* _widgetBase = nullptr;
		ComponentWidget* _shadowSettings = nullptr;
		ComponentWidget* _colouring = nullptr;
		ComponentWidget* _fog = nullptr;
	};
}
