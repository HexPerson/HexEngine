#include "BuildDialog.hpp"
#include "../../Editor.hpp"
#include "../../GameIntegrator.hpp"
#include "../EditorUI.hpp"

namespace HexEditor
{
	BuildDialog::BuildDialog(HexEngine::Element* parent, const HexEngine::Point& position, const HexEngine::Point& size, GameIntegrator* integrator) :
		HexEngine::Dialog(parent, position, size, L"Build Project"),
		_integrator(integrator)
	{
		_widgetBase = new HexEngine::ComponentWidget(this, HexEngine::Point(10, 10), HexEngine::Point(size.x - 20, -1), L"Build Options");

		_buildCodeToggle = new HexEngine::Checkbox(
			_widgetBase,
			_widgetBase->GetNextPos(),
			HexEngine::Point(_widgetBase->GetSize().x - 20, 20),
			L"Build game code (MSBuild on Code/)",
			&_buildCode);

		_packageAssetsToggle = new HexEngine::Checkbox(
			_widgetBase,
			_widgetBase->GetNextPos(),
			HexEngine::Point(_widgetBase->GetSize().x - 20, 20),
			L"Package assets (AssetPacker on Data/ -> GameData.pkg)",
			&_packageAssets);

		_compressAssetsToggle = new HexEngine::Checkbox(
			_widgetBase,
			_widgetBase->GetNextPos(),
			HexEngine::Point(_widgetBase->GetSize().x - 20, 20),
			L"Compress assets (Brotli)",
			&_compressAssets);

		_status = new HexEngine::LineEdit(
			_widgetBase,
			_widgetBase->GetNextPos(),
			HexEngine::Point(_widgetBase->GetSize().x - 20, 22),
			L"Status");
		_status->SetValue(L"Ready");
		_status->DisableRecursive();

		_buildButton = new HexEngine::Button(
			_widgetBase,
			_widgetBase->GetNextPos(),
			HexEngine::Point(120, 24),
			L"Build",
			[this](HexEngine::Button*) -> bool
			{
				RunBuild();
				return true;
			});
	}

	BuildDialog::~BuildDialog()
	{
	}

	BuildDialog* BuildDialog::CreateBuildDialog(HexEngine::Element* parent, GameIntegrator* integrator)
	{
		// Centre the dialog over the editor UI root. Sized to comfortably hold
		// the toggles + status line + button without scrolling.
		constexpr int32_t kWidth = 520;
		constexpr int32_t kHeight = 280;

		const auto centre = HexEngine::Point::GetScreenCenterWithOffset(-kWidth / 2, -kHeight / 2);
		return new BuildDialog(parent, centre, HexEngine::Point(kWidth, kHeight), integrator);
	}

	void BuildDialog::SetStatus(const std::wstring& text, bool failure)
	{
		if (_status == nullptr)
			return;
		(void)failure;
		_status->SetValue(text);
	}

	void BuildDialog::RunBuild()
	{
		if (_integrator == nullptr)
		{
			SetStatus(L"No GameIntegrator available", true);
			return;
		}

		auto* integrator = _integrator;

		if (_buildCode)
		{
			SetStatus(L"Building game code...");
			if (!integrator->BuildGame())
			{
				SetStatus(L"Game code build failed - see editor console / Build/GameBuild.log", true);
				return;
			}
		}

		if (_packageAssets)
		{
			SetStatus(L"Packaging assets...");
			if (!integrator->PackageAssets(_compressAssets))
			{
				SetStatus(L"Asset packaging failed - see editor console / Build/AssetPack.log", true);
				return;
			}
		}

		if (!_buildCode && !_packageAssets)
		{
			SetStatus(L"Nothing selected. Tick at least one option.", true);
			return;
		}

		SetStatus(L"Build complete. Output under <project>/Build/");
	}
}
