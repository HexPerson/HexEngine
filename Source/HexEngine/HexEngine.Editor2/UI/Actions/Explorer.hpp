
#pragma once

#include <HexEngine.Core\HexEngine.hpp>

namespace HexEditor
{
	class Explorer : public Dock
	{
	public:
		struct AssetDesc
		{
			fs::path path;
			ITexture2D* icon = nullptr;
			ITexture2D* generatedIcon = nullptr;
			bool selected = false;
			bool dragging = false;
			std::wstring assetNameFull;
			std::wstring assetNameShort;
		};

		Explorer(Element* parent, const Point& position, const Point& size);

		virtual bool OnInputEvent(InputEvent event, InputData* data) override;

		void UpdateFolderView();

		void SetProjectPath(const fs::path& path);

		virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override;

		void OpenAssetFolder(const fs::path& relativePath, FileSystem* fs);

		void UpdateAssets(const fs::path& relativePath, FileSystem* fs);

		void LoadAsset(const fs::path& path);

		void SelectAll();

		void SetMassMaterial();

		AssetDesc* FindAssetInView(const fs::path& filename);

		void EditAssetName(AssetDesc* asset);

		AssetDesc* GetCurrentlyDraggedAsset() const { return _draggingAsset; }

		//void EditFileName(const fs::path& 

	private:
		void RenderAssetExplorer(GuiRenderer* renderer, uint32_t w, uint32_t h);

		bool OnClickFolder(TreeList* list, ListNode* item, int32_t mouseButton);

		void RecurseList(ListNode* parent, const fs::path& path);

		virtual void PostRenderChildren(GuiRenderer* renderer, uint32_t w, uint32_t h) override;

		void CreateNewMaterial(const fs::path& baseDir);

		void OnEnterSearchText(const std::wstring& text);

	private:
		TreeList* _folderView;
		fs::path _projectPath;
		fs::path _assetsPath;
		std::vector<AssetDesc> _assetsInView;
		TabView* _tab;
		AssetDesc* _hoveredAsset = nullptr;
		AssetDesc* _lastHoveredAsset = nullptr;
		ContextMenu* _contextMenu = nullptr;
		AssetDesc* _draggingAsset = nullptr;
		float _hoverStartTime = 0.0f;
		Point _dragStart;
		int32_t _scrollOffset = 0;
		FileSystem* _currentlyBrowsedFS = nullptr;
		fs::path _currentlyBrowsedFolder;
		AssetDesc* _assetNameToEdit = nullptr;
		std::wstring _editingAssetTempName;
		std::wstring _editingAssetExtension;
		LineEdit* _fileSearchBar = nullptr;
		std::wstring _searchFilter;
		Canvas _canvas;
		//DrawList _drawList;
	};
}
