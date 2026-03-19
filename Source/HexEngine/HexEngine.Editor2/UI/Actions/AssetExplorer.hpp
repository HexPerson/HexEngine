#pragma once

#include <HexEngine.Core\HexEngine.hpp>

namespace HexEditor
{
	class AssetExplorer : public HexEngine::ScrollView
	{
	public:
		struct AssetDesc
		{
			fs::path path;
			HexEngine::ITexture2D* icon = nullptr;
			HexEngine::ITexture2D* generatedIcon = nullptr;
			bool selected = false;
			bool dragging = false;
			bool ownsIcon = false;
			std::wstring assetNameFull;
			std::wstring assetNameShort;
		};

		AssetExplorer(HexEngine::Element* parent, const HexEngine::Point& position, const HexEngine::Point& size);
		virtual ~AssetExplorer();

		void SetSearchFilter(const std::wstring& text);
		void UpdateAssets(const fs::path& relativePath, HexEngine::FileSystem* fs);
		AssetDesc* GetCurrentlyDraggedAsset() const;

		virtual void Render(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h) override;
		virtual void PostRenderChildren(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h) override;
		virtual bool OnInputEvent(HexEngine::InputEvent event, HexEngine::InputData* data) override;

	private:
		void CreateNewMaterial(const fs::path& baseDir);
		void SelectAll();
		void SetMassMaterial();
		void LoadAsset(const fs::path& path);
		void ImportAllMeshes();
		AssetDesc* FindAssetInView(const fs::path& filename);
		void EditAssetName(AssetDesc* asset);
		void ClearAssetIcons();
		void CloseContextMenu();
		int32_t ComputeRequiredContentHeight() const;

	private:
		std::vector<AssetDesc> _assetsInView;
		AssetDesc* _hoveredAsset = nullptr;
		AssetDesc* _lastHoveredAsset = nullptr;
		HexEngine::ContextMenu* _contextMenu = nullptr;
		AssetDesc* _draggingAsset = nullptr;
		float _hoverStartTime = 0.0f;
		HexEngine::Point _dragStart = { -1, -1 };
		HexEngine::FileSystem* _currentlyBrowsedFS = nullptr;
		fs::path _currentlyBrowsedFolder;
		AssetDesc* _assetNameToEdit = nullptr;
		std::wstring _editingAssetTempName;
		std::wstring _editingAssetExtension;
		std::wstring _searchFilter;
	};
}
