#include "ScrollView.hpp"
#include "../GuiRenderer.hpp"
#include "../../Environment/IEnvironment.hpp"
#include <algorithm>
#include <cmath>

namespace HexEngine
{
	namespace
	{
		constexpr int32_t ScrollbarWidth = 10;
		constexpr int32_t ScrollbarPadding = 2;
		constexpr int32_t MinThumbHeight = 20;
	}

	ScrollView::ScrollView(Element* parent, const Point& position, const Point& size, const std::wstring& label) :
		Element(parent, position, size),
		_label(label)
	{
		_contentRoot = new Element(this, Point(0, 0), size);
	}

	Element* ScrollView::GetContentRoot() const
	{
		return _contentRoot;
	}

	void ScrollView::OnAddChild(Element* child)
	{
		Element::OnAddChild(child);

		// Keep ScrollView call-sites simple: direct children are automatically
		// moved under the internal content root so scrolling/layout works.
		if (child && _contentRoot && child != _contentRoot && child->GetParent() == this)
		{
			child->Reparent(_contentRoot, true);
			_canvas.Redraw();
		}
	}

	void ScrollView::SetScrollOffset(float offset)
	{
		_scrollOffset = std::clamp(offset, 0.0f, ComputeMaxScroll());
		_canvas.Redraw();
	}

	float ScrollView::GetScrollOffset() const
	{
		return _scrollOffset;
	}

	void ScrollView::SetScrollSpeed(float speed)
	{
		_scrollSpeed = std::max(1.0f, speed);
	}

	float ScrollView::GetScrollSpeed() const
	{
		return _scrollSpeed;
	}

	void ScrollView::SetManualContentHeight(int32_t height)
	{
		_manualContentHeight = std::max(0, height);
		SetScrollOffset(_scrollOffset);
	}

	int32_t ScrollView::GetManualContentHeight() const
	{
		return _manualContentHeight;
	}

	int32_t ScrollView::GetContentHeight() const
	{
		return ComputeContentHeight();
	}

	void ScrollView::PreRender(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		(void)renderer;

		if (_contentRoot == nullptr)
			return;

		// Safety net: if any controls were attached directly to this scroll view,
		// move them under the content root so scrolling and sizing stays correct.
		std::vector<Element*> directChildren = GetChildren();
		for (auto* child : directChildren)
		{
			if (child && child != _contentRoot && child->GetParent() == this)
			{
				child->Reparent(_contentRoot, true);
			}
		}

		SetScrollOffset(_scrollOffset);

		const int32_t contentHeight = std::max(_size.y, ComputeContentHeight());
		_contentRoot->SetSize(Point(_size.x, contentHeight));
		_contentRoot->SetPosition(Point(0, -(int32_t)std::round(_scrollOffset)));

		_contentRoot->EnableInput(IsMouseOver(true));

		//_canvas.Redraw();
		_isCapturing = _canvas.BeginDraw(renderer, w, h);
	}

	void ScrollView::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		(void)w;
		(void)h;

		const auto pos = GetAbsolutePosition();

		renderer->FillQuad(pos.x, pos.y, _size.x, _size.y, renderer->_style.inspector_widget_back);

		//renderer->FillQuad(pos.x, pos.y, _size.x, _size.y, math::Color(HEX_RGB_TO_FLOAT3(255, 0, 0)));

		if (!_label.empty())
		{
			renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Tiny, pos.x + 4, pos.y + 2, renderer->_style.text_highlight, 0, _label);
		}
	}

	void ScrollView::PostRenderChildren(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		(void)w;
		(void)h;

		if (!_isCapturing)
			return;

		_canvas.EndDraw(renderer);
		_isCapturing = false;

		const RECT viewport = GetViewportRect();
		_canvas.Present(renderer, viewport.left, viewport.top, _size.x, _size.y, viewport);

		const auto pos = GetAbsolutePosition();
		renderer->Frame(pos.x, pos.y, _size.x, _size.y, 1, renderer->_style.win_border);

		const RECT scrollbarRect = ComputeScrollbarRect();
		if (scrollbarRect.right > scrollbarRect.left)
		{
			renderer->FillQuad(
				scrollbarRect.left,
				scrollbarRect.top,
				scrollbarRect.right - scrollbarRect.left,
				scrollbarRect.bottom - scrollbarRect.top,
				renderer->_style.lineedit_back);

			const float maxScroll = ComputeMaxScroll();
			const int32_t contentHeight = std::max(_size.y, ComputeContentHeight());
			const int32_t trackHeight = scrollbarRect.bottom - scrollbarRect.top;
			const int32_t thumbHeight = std::clamp((int32_t)((float)_size.y / (float)contentHeight * (float)trackHeight), MinThumbHeight, trackHeight);
			const int32_t thumbTravel = std::max(0, trackHeight - thumbHeight);
			const int32_t thumbY = scrollbarRect.top + (maxScroll > 0.0f ? (int32_t)std::round((_scrollOffset / maxScroll) * (float)thumbTravel) : 0);

			const math::Color thumbColor = _isDraggingScrollbar ? renderer->_style.text_highlight : renderer->_style.listbox_highlight;
			renderer->FillQuad(scrollbarRect.left, thumbY, scrollbarRect.right - scrollbarRect.left, thumbHeight, thumbColor);
			renderer->Frame(scrollbarRect.left, thumbY, scrollbarRect.right - scrollbarRect.left, thumbHeight, 1, renderer->_style.win_border);
		}
	}

	bool ScrollView::OnInputEvent(InputEvent event, InputData* data)
	{
		const bool isMouseOverView = IsMouseOver(true);

		if (event == InputEvent::MouseWheel && isMouseOverView)
		{
			SetScrollOffset(_scrollOffset - data->MouseWheel.delta * _scrollSpeed);
			return true;
		}

		const RECT scrollbarRect = ComputeScrollbarRect();
		const bool hasScrollbar = scrollbarRect.right > scrollbarRect.left;

		if (event == InputEvent::MouseDown && data->MouseDown.button == VK_LBUTTON && hasScrollbar)
		{
			const int32_t mx = data->MouseDown.xpos;
			const int32_t my = data->MouseDown.ypos;
			const bool mouseOverScrollbar = mx >= scrollbarRect.left && mx < scrollbarRect.right && my >= scrollbarRect.top && my < scrollbarRect.bottom;

			if (mouseOverScrollbar)
			{
				_isDraggingScrollbar = true;
				_scrollbarDragStartY = my;
				_scrollbarDragStartOffset = _scrollOffset;
				return true;
			}
		}
		else if (event == InputEvent::MouseMove && _isDraggingScrollbar)
		{
			const int32_t my = (int32_t)std::round(data->MouseMove.y);
			UpdateScrollbarDrag(my);
			return true;
		}
		else if (event == InputEvent::MouseUp && data->MouseUp.button == VK_LBUTTON && _isDraggingScrollbar)
		{
			_isDraggingScrollbar = false;
			return true;
		}

		return false;
	}

	RECT ScrollView::GetViewportRect() const
	{
		const auto absPos = GetAbsolutePosition();
		RECT rect;
		rect.left = absPos.x;
		rect.top = absPos.y;
		rect.right = absPos.x + _size.x;
		rect.bottom = absPos.y + _size.y;
		return rect;
	}

	int32_t ScrollView::ComputeContentHeight() const
	{
		int32_t computed = 0;

		if (_contentRoot)
		{
			std::function<void(Element*, int32_t, int32_t&)> walk = [&](Element* node, int32_t baseY, int32_t& outMax)
			{
				if (node == nullptr || node->WantsDeletion())
					return;

				for (auto* child : node->GetChildren())
				{
					if (child == nullptr || child->WantsDeletion())
						continue;

					const int32_t localTop = baseY + child->GetPosition().y;
					const int32_t localBottom = localTop + child->GetSize().y;
					outMax = std::max(outMax, localBottom);

					walk(child, localTop, outMax);
				}
			};

			walk(_contentRoot, 0, computed);
		}

		computed = std::max(computed, _manualContentHeight);
		return std::max(computed, _size.y);
	}

	float ScrollView::ComputeMaxScroll() const
	{
		return std::max(0.0f, (float)ComputeContentHeight() - (float)_size.y);
	}

	RECT ScrollView::ComputeScrollbarRect() const
	{
		RECT rect = { 0, 0, 0, 0 };

		if (ComputeMaxScroll() <= 0.0f)
			return rect;

		const auto pos = GetAbsolutePosition();
		rect.left = pos.x + _size.x - ScrollbarWidth - ScrollbarPadding;
		rect.top = pos.y + ScrollbarPadding;
		rect.right = pos.x + _size.x - ScrollbarPadding;
		rect.bottom = pos.y + _size.y - ScrollbarPadding;
		return rect;
	}

	void ScrollView::UpdateScrollbarDrag(int32_t mouseY)
	{
		const RECT scrollbarRect = ComputeScrollbarRect();
		const int32_t trackHeight = scrollbarRect.bottom - scrollbarRect.top;
		if (trackHeight <= 0)
			return;

		const int32_t contentHeight = std::max(_size.y, ComputeContentHeight());
		const int32_t thumbHeight = std::clamp((int32_t)((float)_size.y / (float)contentHeight * (float)trackHeight), MinThumbHeight, trackHeight);
		const int32_t thumbTravel = std::max(1, trackHeight - thumbHeight);
		const float maxScroll = ComputeMaxScroll();
		if (maxScroll <= 0.0f)
			return;

		const int32_t deltaY = mouseY - _scrollbarDragStartY;
		const float scrollDelta = ((float)deltaY / (float)thumbTravel) * maxScroll;
		SetScrollOffset(_scrollbarDragStartOffset + scrollDelta);
	}
}
