#pragma once

#include "Element.hpp"
#include "LineEdit.hpp"
#include "ScrollView.hpp"
#include "../../FileSystem/IResource.hpp"

namespace HexEngine
{
	struct HEX_API AssetSearchResult
	{
		fs::path absolutePath;
		fs::path assetPath;
		std::wstring displayName;
		ResourceType type = ResourceType::None;
		ITexture2D* preview = nullptr;
		bool previewRequested = false;
	};

	class HEX_API AssetSearch : public Element
	{
	public:
		using QueryFn = std::function<void(
			const std::wstring& filter,
			const std::vector<ResourceType>& allowedTypes,
			std::vector<AssetSearchResult>& outResults)>;
		using OnDragAndDropFn = std::function<void(AssetSearch*, const AssetSearchResult&)>;
		using OnDoubleClickFn = std::function<void(AssetSearch*, const AssetSearchResult&)>;

		using OnSelectFn = std::function<void(AssetSearch*, const AssetSearchResult&)>;

		AssetSearch(
			Element* parent,
			const Point& position,
			const Point& size,
			const std::wstring& label,
			const std::vector<ResourceType>& allowedTypes,
			OnSelectFn onSelect = nullptr);

		virtual ~AssetSearch();

		void SetQueryFn(QueryFn fn);
		void SetOnSelectFn(OnSelectFn fn);

		void SetAllowedTypes(const std::vector<ResourceType>& allowedTypes);
		const std::vector<ResourceType>& GetAllowedTypes() const;

		void SetValue(const std::wstring& value);
		const std::wstring& GetValue() const;

		bool GetSelectedResult(AssetSearchResult& outResult) const;
		void ClearSelection();
		void RefreshResults();

		virtual void PostRenderChildren(GuiRenderer* renderer, uint32_t w, uint32_t h) override;
		virtual bool OnInputEvent(InputEvent event, InputData* data) override;
		virtual int32_t GetLabelWidth() const override;
		virtual void SetLabelMinSize(int32_t minSize) override;

		void SetOnDragAndDropFn(OnDragAndDropFn fn);
		void SetOnDoubleClickFn(OnDoubleClickFn fn);

	private:
		class AssetSearchRow;
		void OnSearchTextChanged(LineEdit* edit, const std::wstring& value);
		void OnPickResult(size_t index);
		void OpenPopup();
		void ClosePopup();
		void RebuildPopupRows();
		bool IsPopupOpen() const;
		bool IsFilterTypeAllowed(ResourceType type) const;
		void RunDefaultQuery(const std::wstring& filter, std::vector<AssetSearchResult>& outResults) const;
		void HandleDroppedPath(const fs::path& droppedPath);
		void HandleDoubleClick();
		bool BuildResultFromPath(const fs::path& inputPath, AssetSearchResult& outResult) const;
		static ResourceType ResourceTypeFromPath(const fs::path& path);
		static std::wstring ToLowerCopy(const std::wstring& value);

	private:
		class AssetSearchRow* CreateRow(Element* parent, int32_t y, size_t index);

	private:
		LineEdit* _edit = nullptr;
		ScrollView* _popup = nullptr;
		std::vector<ResourceType> _allowedTypes;
		std::vector<AssetSearchResult> _results;
		size_t _highlightedIndex = 0;
		AssetSearchResult _selectedResult;
		bool _hasSelection = false;
		QueryFn _queryFn;
		OnSelectFn _onSelect;
		int32_t _popupMaxHeight = 240;
		size_t _maxResults = 200;
		OnDragAndDropFn _onDragAndDropFn;
		OnDoubleClickFn _onDoubleClickFn;
	};
}
