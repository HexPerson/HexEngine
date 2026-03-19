
#pragma once

#include <HexEngine.Core\HexEngine.hpp>
#include "AssetExplorer.hpp"
#include "FolderExplorer.hpp"

namespace HexEditor
{
	class Explorer : public HexEngine::Dock
	{
	public:
		using AssetDesc = AssetExplorer::AssetDesc;

		Explorer(Element* parent, const HexEngine::Point& position, const HexEngine::Point& size);

		virtual bool OnInputEvent(HexEngine::InputEvent event, HexEngine::InputData* data) override;

		void UpdateFolderView();

		void SetProjectPath(const fs::path& path);

		virtual void Render(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h) override;

		AssetDesc* GetCurrentlyDraggedAsset() const;

	private:
		void OnFolderSelected(const fs::path& relativePath, HexEngine::FileSystem* fs);
		void OnEnterSearchText(const std::wstring& text);

	private:
		fs::path _projectPath;
		FolderExplorer* _folderExplorer = nullptr;
		AssetExplorer* _assetExplorer = nullptr;
		HexEngine::TabView* _tab;
		HexEngine::LineEdit* _fileSearchBar = nullptr;
	};
}
