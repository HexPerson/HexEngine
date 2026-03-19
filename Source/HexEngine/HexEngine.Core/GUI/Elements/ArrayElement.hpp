#pragma once

#include "ArrayElementBase.hpp"
#include <type_traits>

namespace HexEngine
{
	/**
	 * @brief Typed array editor control that binds a homogeneous `std::vector<T>`.
	 *
	 * The control provides add/remove/count behavior from ArrayElementBase and
	 * delegates per-item UI construction to user-supplied callbacks.
	 */
	template<typename T>
	class ArrayElement : public ArrayElementBase
	{
	public:
		using CreateItemFn = std::function<T()>;
		using BuildItemEditorFn = std::function<void(Element* parent, T& item, int32_t index)>;
		using GetItemHeightFn = std::function<int32_t(const T& item, int32_t index)>;
		using GetItemLabelFn = std::function<std::wstring(const T& item, int32_t index)>;

		ArrayElement(
			Element* parent,
			const Point& position,
			const Point& size,
			const std::wstring& label,
			std::vector<T>& items,
			BuildItemEditorFn buildItemEditor,
			CreateItemFn createItem = nullptr,
			GetItemHeightFn getItemHeight = nullptr,
			GetItemLabelFn getItemLabel = nullptr) :
			ArrayElementBase(parent, position, size, label),
			_items(&items),
			_buildItemEditor(std::move(buildItemEditor)),
			_createItem(std::move(createItem)),
			_getItemHeight(std::move(getItemHeight)),
			_getItemLabel(std::move(getItemLabel))
		{
			RefreshItemRows();
		}

	protected:
		virtual int32_t GetItemCountInternal() const override
		{
			return _items ? (int32_t)_items->size() : 0;
		}

		virtual bool AddDefaultItemInternal() override
		{
			if (_items == nullptr)
				return false;

			if (_createItem)
			{
				_items->push_back(_createItem());
				return true;
			}

			if constexpr (std::is_default_constructible_v<T>)
			{
				_items->emplace_back();
				return true;
			}
			else
			{
				return false;
			}
		}

		virtual bool RemoveItemInternal(int32_t index) override
		{
			if (_items == nullptr || index < 0 || index >= (int32_t)_items->size())
				return false;

			_items->erase(_items->begin() + index);
			return true;
		}

		virtual void BuildItemEditorInternal(int32_t index, Element* rowRoot) override
		{
			if (_items == nullptr || _buildItemEditor == nullptr)
				return;

			if (index >= 0 && index < (int32_t)_items->size())
			{
				_buildItemEditor(rowRoot, _items->at(index), index);
			}
		}

		virtual int32_t GetItemHeightInternal(int32_t index) const override
		{
			if (_items && _getItemHeight && index >= 0 && index < (int32_t)_items->size())
			{
				return _getItemHeight(_items->at(index), index);
			}

			return ArrayElementBase::GetItemHeightInternal(index);
		}

		virtual std::wstring GetItemLabelInternal(int32_t index) const override
		{
			if (_items && _getItemLabel && index >= 0 && index < (int32_t)_items->size())
			{
				return _getItemLabel(_items->at(index), index);
			}

			return ArrayElementBase::GetItemLabelInternal(index);
		}

	private:
		std::vector<T>* _items = nullptr;
		BuildItemEditorFn _buildItemEditor;
		CreateItemFn _createItem;
		GetItemHeightFn _getItemHeight;
		GetItemLabelFn _getItemLabel;
	};
}
