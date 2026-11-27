
#include "ListBox.hpp"
#include "../../Environment/IEnvironment.hpp"
#include "../GuiRenderer.hpp"
#include "../../Graphics/IGraphicsDevice.hpp"
#include <cmath>

namespace HexEngine
{
	ListBox::ListBox(Element* parent, const Point& position, const Point& size) :
		ScrollView(parent, position, size)
	{
		SetManualContentHeight(size.y);
	}

	ListBox::~ListBox()
	{
	}

	void ListBox::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		ScrollView::Render(renderer, w, h);

		if (_isCapturing)
		{
			auto pos = GetAbsolutePosition();

			renderer->FillQuad(pos.x, pos.y, _size.x, _size.y, renderer->_style.listbox_back);

			RenderItems(renderer, pos);
		}

		//renderer->Frame(pos.x, pos.y, _size.x, _size.y, 1, renderer->_style.listbox_border);
	}

	void ListBox::AddItem(const std::wstring& label, ITexture2D* icon)
	{
		_items.push_back({ label, icon });

		const int32_t lineHeight = 20;
		SetManualContentHeight(std::max(_size.y, (int32_t)_items.size() * lineHeight));
	}

	void ListBox::RenderItems(GuiRenderer* renderer, const Point& position)
	{
		_hoverIdx = -1;

		
		{
			const int32_t lineHeight = 20;
			Point size(_size.x, lineHeight);
			const float scroll = GetScrollOffset();
			const int32_t firstVisibleIndex = std::max(0, (int32_t)std::floor(scroll / (float)lineHeight));
			const int32_t pixelOffset = (int32_t)std::round(scroll - (float)firstVisibleIndex * (float)lineHeight);
			Point pos(position.x, position.y - pixelOffset);

			for (int32_t i = firstVisibleIndex; i < (int32_t)_items.size(); ++i)
			{
				const auto& item = _items[i];

				if (pos.y >= position.y + _size.y)
					break;

				if (pos.y + lineHeight > position.y && IsMouseOver(pos, size))
				{
					renderer->FillQuad(pos.x, pos.y, size.x, size.y, renderer->_style.listbox_highlight);

					_hoverIdx = i;
				}
				//else if (pos.y + lineHeight > position.y && i % 2 == 0)
				//	renderer->FillQuad(pos.x, pos.y, size.x, size.y, renderer->_style.listbox_alternate_colour);

				if (pos.y + lineHeight > position.y && item.icon)
					renderer->FillTexturedQuad(item.icon, pos.x + 4, pos.y + 1, 16, 16, math::Color(1, 1, 1, 1));

				if (pos.y + lineHeight > position.y)
					renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Tiny, pos.x + 24, pos.y + size.y / 2, renderer->_style.text_regular, FontAlign::CentreUD, item.label);

				pos.y += lineHeight;
			}
		}
	}

	bool ListBox::OnInputEvent(InputEvent event, InputData* data)
	{
		if (ScrollView::OnInputEvent(event, data))
		{
			return true;
		}

		if (event == InputEvent::MouseDown && data->MouseDown.button == VK_LBUTTON)
		{
			// Hit-test the actual click position rather than trusting _hoverIdx:
			// _hoverIdx is only refreshed when the cached canvas re-captures, and
			// the canvas stops redrawing once the mouse leaves the listbox, so it
			// can hold a stale index from the last frame the cursor was inside.
			const int32_t clickIdx = HitTestItem(data->MouseDown.xpos, data->MouseDown.ypos);

			if (OnClickItem && clickIdx != -1)
			{
				if (OnClickItem(this, &_items.at(clickIdx)))
					return true;
			}
		}
		else if (event == InputEvent::MouseMove)
		{
			if (IsMouseOver(true))
			{
				_canvas.Redraw();
				return true;
			}

			// Mouse left the listbox: drop the stale hover and re-capture once
			// so the highlight row clears instead of staying baked into the canvas.
			if (_hoverIdx != -1)
			{
				_hoverIdx = -1;
				_canvas.Redraw();
			}
		}

		return false;
	}

	int32_t ListBox::HitTestItem(int32_t mx, int32_t my) const
	{
		const auto position = GetAbsolutePosition();

		if (mx < position.x || mx >= position.x + _size.x || my < position.y || my >= position.y + _size.y)
			return -1;

		if (IsMousePointClipped(mx, my))
			return -1;

		const int32_t lineHeight = 20;
		const float contentY = (float)(my - position.y) + GetScrollOffset();
		const int32_t idx = (int32_t)std::floor(contentY / (float)lineHeight);

		if (idx < 0 || idx >= (int32_t)_items.size())
			return -1;

		return idx;
	}
}
