#pragma once

#include "Element.hpp"

namespace HexEngine
{
	class Button;

	/**
	 * @brief Non-templated array editor control that owns add/remove/count UI behavior.
	 *
	 * Derived classes provide data access and item editor construction by
	 * implementing the protected virtual hooks.
	 */
	class HEX_API ArrayElementBase : public Element
	{
	public:
		using OnArrayChangedFn = std::function<void(ArrayElementBase*)>;

		ArrayElementBase(Element* parent, const Point& position, const Point& size, const std::wstring& label);
		virtual ~ArrayElementBase();

		virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override;

		/** @brief Rebuilds all item row controls from the current data model. */
		void RefreshItemRows();

		/** @brief Invoked after add/remove succeeds. */
		void SetOnArrayChanged(OnArrayChangedFn fn);

	protected:
		/** @brief Returns the current item count from the data source. */
		virtual int32_t GetItemCountInternal() const = 0;
		/** @brief Adds a new item to the data source. */
		virtual bool AddDefaultItemInternal() = 0;
		/** @brief Removes an item by index from the data source. */
		virtual bool RemoveItemInternal(int32_t index) = 0;
		/** @brief Builds UI controls for one item row. */
		virtual void BuildItemEditorInternal(int32_t index, Element* rowRoot) = 0;

		/** @brief Returns total row height in pixels for an item. */
		virtual int32_t GetItemHeightInternal(int32_t index) const;
		/** @brief Returns row title text for an item. */
		virtual std::wstring GetItemLabelInternal(int32_t index) const;

	private:
		bool OnAddButtonPressed(Button* button);
		bool OnRemoveButtonPressed(Button* button);
		void ClearRows();
		void NotifyArrayChanged();

	private:
		struct ItemRowVisual
		{
			int32_t top = 0;
			int32_t height = 0;
		};

		std::wstring _label;
		Button* _addButton = nullptr;
		Button* _removeButton = nullptr;
		std::vector<Element*> _itemRows;
		std::vector<ItemRowVisual> _rowVisuals;
		OnArrayChangedFn _onArrayChanged;
		int32_t _lastKnownWidth = -1;
	};
}
