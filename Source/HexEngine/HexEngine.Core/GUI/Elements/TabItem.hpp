
#pragma once

#include "Element.hpp"

namespace HexEngine
{
	class TabView;
	class HEX_API TabItem : public Element
	{
	public:
		TabItem(TabView* parent, const Point& position, const Point& size, const std::wstring& label);

		virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override;
		virtual bool OnInputEvent(InputEvent event, InputData* data) override;

		void SetSelected(bool selected = true);
		bool IsSelected() const { return _selected; }

		int32_t GetTabWidth();

		// Closable tabs draw an 'x' in their header; clicking it removes the tab
		// (TabView::RemoveTab) unless onClose returns false.
		using OnCloseFn = std::function<bool()>;
		void SetClosable(bool closable, OnCloseFn onClose = nullptr);
		bool IsClosable() const { return _closable; }

		// Called when the tab becomes active (true) or inactive (false), so content
		// can start/stop work it only needs while visible.
		using OnSelectedChangedFn = std::function<void(bool)>;
		void SetOnSelectedChanged(OnSelectedChangedFn fn) { _onSelectedChanged = std::move(fn); }

	private:
		bool IsMouseOverCloseButton() const;

		std::wstring _label;
		bool _selected = false;
		int32_t _tabWidth = 0;
		bool _closable = false;
		bool _closeHovered = false;
		OnCloseFn _onClose;
		OnSelectedChangedFn _onSelectedChanged;
	};
}