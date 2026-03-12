
#pragma once

#include <HexEngine.Core\HexEngine.hpp>

namespace HexEditor
{
	class Explorer : public HexEngine::Dock
	{
	public:
		struct AssetDesc
		{
			fs::path path;
			HexEngine::ITexture2D* icon = nullptr;
			HexEngine::ITexture2D* generatedIcon = nullptr;
			bool selected = false;
			bool dragging = false;
			std::wstring assetNameFull;
			std::wstring assetNameShort;
		};

		Explorer(Element* parent, const HexEngine::Point& position, const HexEngine::Point& size);

		virtual bool OnInputEvent(HexEngine::InputEvent event, HexEngine::InputData* data) override;

		void UpdateFolderView();

		void SetProjectPath(const fs::path& path);

		virtual void Render(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h) override;

		void OpenAssetFolder(const fs::path& relativePath, HexEngine::FileSystem* fs);

		void UpdateAssets(const fs::path& relativePath, HexEngine::FileSystem* fs);

		void LoadAsset(const fs::path& path);

		void SelectAll();

		void SetMassMaterial();

		AssetDesc* FindAssetInView(const fs::path& filename);

		void EditAssetName(AssetDesc* asset);

		AssetDesc* GetCurrentlyDraggedAsset() const { return _draggingAsset; }

		void ImportAllMeshes();

	private:
		void RenderAssetExplorer(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h);

		bool OnClickFolder(HexEngine::TreeList* list, HexEngine::ListNode* item, int32_t mouseButton);

		void RecurseList(HexEngine::ListNode* parent, const fs::path& path);

		virtual void PostRenderChildren(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h) override;

		void CreateNewMaterial(const fs::path& baseDir);

		void OnEnterSearchText(const std::wstring& text);

	private:
		HexEngine::TreeList* _folderView;
		fs::path _projectPath;
		fs::path _assetsPath;
		std::vector<AssetDesc> _assetsInView;
		HexEngine::TabView* _tab;
		AssetDesc* _hoveredAsset = nullptr;
		AssetDesc* _lastHoveredAsset = nullptr;
		HexEngine::ContextMenu* _contextMenu = nullptr;
		AssetDesc* _draggingAsset = nullptr;
		float _hoverStartTime = 0.0f;
		HexEngine::Point _dragStart;
		int32_t _scrollOffset = 0;
		HexEngine::FileSystem* _currentlyBrowsedFS = nullptr;
		fs::path _currentlyBrowsedFolder;
		AssetDesc* _assetNameToEdit = nullptr;
		std::wstring _editingAssetTempName;
		std::wstring _editingAssetExtension;
		HexEngine::LineEdit* _fileSearchBar = nullptr;
		std::wstring _searchFilter;
		HexEngine::Canvas _canvas;
		//DrawList _drawList;
	};
}
