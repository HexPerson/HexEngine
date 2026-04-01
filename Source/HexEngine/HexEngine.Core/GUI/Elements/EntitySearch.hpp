#pragma once

#include "Element.hpp"
#include "Button.hpp"
#include "LineEdit.hpp"
#include "ScrollView.hpp"

namespace HexEngine
{
	class Entity;

	struct HEX_API EntitySearchResult
	{
		std::string entityName;
		std::wstring displayName;
		Entity* entity = nullptr;
	};

	class HEX_API EntitySearch : public Element
	{
	public:
		using OnSelectFn = std::function<void(EntitySearch*, const EntitySearchResult&)>;
		using OnInputFn = std::function<void(EntitySearch*, const std::wstring&)>;

		EntitySearch(
			Element* parent,
			const Point& position,
			const Point& size,
			const std::wstring& label,
			OnSelectFn onSelect = nullptr);

		virtual ~EntitySearch();

		void SetOnSelectFn(OnSelectFn fn);
		void SetOnInputFn(OnInputFn fn);
		void SetValue(const std::wstring& value);
		const std::wstring& GetValue() const;
		void SetPrefabOverrideBinding(const std::string& componentName, const std::string& jsonPointer);
		bool GetSelectedResult(EntitySearchResult& outResult) const;
		void ClearSelection();
		void RefreshResults();

		virtual bool OnInputEvent(InputEvent event, InputData* data) override;
		virtual int32_t GetLabelWidth() const override;
		virtual void SetLabelMinSize(int32_t minSize) override;

	private:
		class EntitySearchRow;
		void OnSearchTextChanged(LineEdit* edit, const std::wstring& value);
		void OnPickResult(size_t index);
		void OpenPopup();
		void ClosePopup();
		void RebuildPopupRows();
		bool IsPopupOpen() const;
		void SetPickMode(bool enabled);
		bool TryPickEntityFromScene(EntitySearchResult& outResult) const;
		void RunDefaultQuery(const std::wstring& filter, std::vector<EntitySearchResult>& outResults) const;
		static std::wstring ToLowerCopy(const std::wstring& value);

	private:
		EntitySearchRow* CreateRow(Element* parent, int32_t y, size_t index);

	private:
		LineEdit* _edit = nullptr;
		Button* _pickButton = nullptr;
		ScrollView* _popup = nullptr;
		std::vector<EntitySearchResult> _results;
		size_t _highlightedIndex = 0;
		EntitySearchResult _selectedResult;
		bool _hasSelection = false;
		OnSelectFn _onSelect;
		OnInputFn _onInput;
		int32_t _popupMaxHeight = 240;
		size_t _maxResults = 200;
		bool _pickMode = false;
	};
}
