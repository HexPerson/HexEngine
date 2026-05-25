#pragma once

#include <HexEngine.Core\HexEngine.hpp>

namespace HexEditor
{
	class GameIntegrator;

	/**
	 * @brief Editor dialog for shipping the active project.
	 *
	 * Bundles the two-phase ship flow into one user-facing surface:
	 *   1. Build the game's Code/ via GameIntegrator::BuildGame (MSBuild).
	 *   2. Package the project's Data/ folder into GameData.pkg via
	 *      GameIntegrator::PackageAssets (HexEngine.AssetPacker.exe).
	 *
	 * Both steps are optional via checkboxes - useful when the user has
	 * already built the game DLL and just wants to repack assets, or vice
	 * versa. Output lands under <project>/Build/ by default. Progress and
	 * any error tail get logged through the standard editor console.
	 */
	class BuildDialog : public HexEngine::Dialog
	{
	public:
		BuildDialog(HexEngine::Element* parent, const HexEngine::Point& position, const HexEngine::Point& size, GameIntegrator* integrator);
		~BuildDialog();

		static BuildDialog* CreateBuildDialog(HexEngine::Element* parent, GameIntegrator* integrator);

	private:
		void RunBuild();
		void SetStatus(const std::wstring& text, bool failure = false);

	private:
		HexEngine::ComponentWidget* _widgetBase = nullptr;
		HexEngine::Checkbox* _buildCodeToggle = nullptr;
		HexEngine::Checkbox* _packageAssetsToggle = nullptr;
		HexEngine::Checkbox* _compressAssetsToggle = nullptr;
		HexEngine::Checkbox* _deployEngineToggle = nullptr;
		HexEngine::LineEdit* _status = nullptr;
		HexEngine::Button* _buildButton = nullptr;

		bool _buildCode = true;
		bool _packageAssets = true;
		bool _compressAssets = true;
		// Default true - this is the single biggest source of "I changed
		// engine code but the launcher still runs the stale Core.dll"
		// frustration, and copying a handful of DLLs is cheap.
		bool _deployEngine = true;
		GameIntegrator* _integrator = nullptr;
	};
}
