#include "Explorer.hpp"

namespace HexEditor
{
	Explorer::Explorer(Element* parent, const HexEngine::Point& position, const HexEngine::Point& size) :
		Dock(parent, position, size, Dock::Anchor::Bottom)
	{
		_folderExplorer = new FolderExplorer(this, HexEngine::Point(10, 10), HexEngine::Point(size.y - 20, size.y - 20));
		_folderExplorer->SetOnFolderSelected(std::bind(&Explorer::OnFolderSelected, this, std::placeholders::_1, std::placeholders::_2));

		_tab = new HexEngine::TabView(this, HexEngine::Point(size.y, 10), HexEngine::Point(HexEngine::g_pEnv->GetUIManager().GetWidth() - (size.y + 10), size.y - 20));
		_tab->AddTab(L"Assets");
		_tab->AddTab(L"Log");

		_fileSearchBar = new HexEngine::LineEdit(_tab, HexEngine::Point(10, 20), HexEngine::Point(_tab->GetSize().x - 20, 20), L"");
		_fileSearchBar->SetIcon(HexEngine::ITexture2D::Create(L"EngineData.Textures/UI/magnifying_glass.png"), math::Color(1, 1, 1, 1));
		_fileSearchBar->SetOnInputFn(std::bind(&Explorer::OnEnterSearchText, this, std::placeholders::_2));
		_fileSearchBar->SetDoesCallbackWaitForReturn(false);

		_assetExplorer = new AssetExplorer(_tab, HexEngine::Point(10, 50), HexEngine::Point(_tab->GetSize().x - 20, _tab->GetSize().y - 60));
	}

	bool Explorer::OnInputEvent(HexEngine::InputEvent event, HexEngine::InputData* data)
	{
		(void)event;
		(void)data;
		return false;
	}

	void Explorer::OnEnterSearchText(const std::wstring& text)
	{
		if (_assetExplorer != nullptr)
		{
			_assetExplorer->SetSearchFilter(text);
		}
	}

	void Explorer::OnFolderSelected(const fs::path& relativePath, HexEngine::FileSystem* fs)
	{
		if (_assetExplorer != nullptr)
		{
			_assetExplorer->UpdateAssets(relativePath, fs);
		}
	}

	Explorer::AssetDesc* Explorer::GetCurrentlyDraggedAsset() const
	{
		return _assetExplorer ? _assetExplorer->GetCurrentlyDraggedAsset() : nullptr;
	}

	bool Explorer::ConsumeRecentlyDroppedAssetPath(fs::path& outPath)
	{
		return _assetExplorer ? _assetExplorer->ConsumeRecentlyDroppedAssetPath(outPath) : false;
	}

	void Explorer::SetProjectPath(const fs::path& path)
	{
		_projectPath = path;
		UpdateFolderView();
	}

	void Explorer::UpdateFolderView()
	{
		if (_folderExplorer != nullptr)
		{
			_folderExplorer->UpdateFolderView();
		}
	}

	void Explorer::Render(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		const bool showAssetsTab = _tab->GetCurrentTabIndex() == 0;

		if (showAssetsTab)
		{
			_fileSearchBar->EnableRecursive();
			_assetExplorer->EnableRecursive();
		}
		else
		{
			_fileSearchBar->DisableRecursive();
			_assetExplorer->DisableRecursive();
		}

		Dock::Render(renderer, w, h);
	}
}
