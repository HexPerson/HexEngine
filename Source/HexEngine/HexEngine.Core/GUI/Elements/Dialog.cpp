
#include "Dialog.hpp"
#include "../../FileSystem/ResourceSystem.hpp"
#include "../GuiRenderer.hpp"
#include "../UIManager.hpp"

namespace HexEngine
{
	Dialog::Dialog(Element* parent, const Point& position, const Point& size, const std::wstring& title, CallbackFn callback) :
		Element(parent, position, size),
		_title(title),
		_callback(callback)
	{
		_logo = ITexture2D::Create("EngineData.Textures/UI/hex_logo_small.png");
	}

	Dialog::~Dialog()
	{
	}	

	void Dialog::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		// handle dragging
		// 
		if (_beingDragged)
		{
			g_pEnv->_inputSystem->GetMousePosition(_position.x, _position.y);

			_position.x -= _dragStart.x;
			_position.y -= _dragStart.y;
		}

		// window background
		renderer->FillQuad(_position.x, _position.y, _size.x, _size.y, renderer->_style.win_back);

		// title fill
		renderer->FillQuad(_position.x, _position.y, _size.x, renderer->_style.win_title_height, renderer->_style.win_title_colour1);

		// window border
		renderer->Frame(_position.x, _position.y, _size.x, _size.y, 1, renderer->_style.win_border);

		// title text
		//if(IsMouseOver() == false)
		//	renderer->PrintText(style.font, (uint8_t)Style::FontSize::Regular, _position.x + 40, _position.y + style.win_title_height / 2, style.win_title, FontAlign::CentreUD, _title);
		//else
		renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Small, _position.x + 40, _position.y + renderer->_style.win_title_height / 2, renderer->_style.win_highlight, FontAlign::CentreUD, _title);

		// title bottom border
		renderer->Line(_position.x, _position.y + renderer->_style.win_title_height, _position.x + _size.x, _position.y + renderer->_style.win_title_height, renderer->_style.win_border);

		int32_t closeButtonSize = (int32_t)((float)renderer->_style.win_title_height * 0.5f);
		if (IsMouseOver(_position.x + _size.x - (closeButtonSize + 8), _position.y + (renderer->_style.win_title_height / 2) - (closeButtonSize / 2) - 1, closeButtonSize, closeButtonSize))
		{
			_hoveringCloseButton = true;
		}
		else
		{
			_hoveringCloseButton = false;
		}

		// highlight title bar
		//renderer->Line(_position.x + _size.x - 1, _position.y + 1, _position.x + _size.x - 1, _position.y + 1 + style.win_title_height - 2, style.win_highlight); // right
		//renderer->Line(_position.x + 1, _position.y + style.win_title_height- 1, _position.x + _size.x - 2, _position.y + style.win_title_height - 1, style.win_highlight); // bottom

		// highlight main window
		//renderer->Line(_position.x + _size.x - 1, _position.y + style.win_title_height + 1, _position.x + _size.x - 1, _position.y + _size.y - 1, style.win_highlight); // right
		//renderer->Line(_position.x + 1, _position.y + _size.y - 1, _position.x + _size.x - 2, _position.y + _size.y - 1, style.win_highlight); // bottom

		// close button
		
		renderer->FillTexturedQuad(renderer->_style.img_win_close.get(), _position.x + _size.x - (closeButtonSize + 8), _position.y + (renderer->_style.win_title_height / 2) - (closeButtonSize / 2) - 1, closeButtonSize, closeButtonSize, _hoveringCloseButton ? math::Color(1, 0, 0, 1) : math::Color(1, 1, 1, 1));

		renderer->FillTexturedQuad(_logo.get(), _position.x + 3, _position.y + 1, renderer->_style.win_title_height-2, renderer->_style.win_title_height-2, renderer->_style.text_highlight);

		
	}

	Point Dialog::GetAbsolutePosition() const
	{
		auto pos = GetPosition();

		// add in the title bar
		pos.y += g_pEnv->GetUIManager().GetRenderer()->_style.win_title_height;

		if (Element* parent = GetParent(); parent != nullptr)
		{
			pos += parent->GetAbsolutePosition();
		}

		return pos;
	}

	bool Dialog::OnInputEvent(InputEvent event, InputData* data)
	{
		if (event == InputEvent::MouseDown)
		{
			if (data->MouseDown.button == VK_LBUTTON && IsMouseOver())
			{
				if(_hoveringCloseButton)
				{
					DeleteMe();
					return true;
				}

				if (IsMouseOver(_position.x, _position.y, _size.x, g_pEnv->GetUIManager().GetRenderer()->_style.win_title_height))
				{
					_dragStart.x = data->MouseDown.xpos - _position.x;
					_dragStart.y = data->MouseDown.ypos - _position.y;
					_beingDragged = true;
				}

				BringToFront();
				return true;
			}
		}
		else if (event == InputEvent::MouseUp)
		{
			if (data->MouseDown.button == VK_LBUTTON && _beingDragged)
			{
				_beingDragged = false;
				return true;
			}
		}
		return false;
	}
}