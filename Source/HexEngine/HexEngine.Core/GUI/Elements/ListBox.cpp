
#include "ListBox.hpp"
#include "../../Environment/IEnvironment.hpp"
#include "../GuiRenderer.hpp"
#include "../../Graphics/IGraphicsDevice.hpp"

namespace HexEngine
{
	ListBox::ListBox(Element* parent, const Point& position, const Point& size) :
		Element(parent, position, size)
	{
		_renderTarget = g_pEnv->_graphicsDevice->CreateTexture2D(
			size.x,
			size.y,
			DXGI_FORMAT_R8G8B8A8_UNORM,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0,
			1, 0,
			D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			D3D11_SRV_DIMENSION_TEXTURE2D);
	}

	ListBox::~ListBox()
	{
		SAFE_DELETE(_renderTarget);
	}

	void ListBox::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		auto pos = GetAbsolutePosition();

		renderer->FillQuad(pos.x, pos.y, _size.x, _size.y, renderer->_style.listbox_back);
		

		RenderItems(renderer, pos);

		renderer->Frame(pos.x, pos.y, _size.x, _size.y, 1, renderer->_style.listbox_border);

		//renderer->FillTexturedQuad(_renderTarget, pos.x, pos.y, _size.x, _size.y, math::Color(1, 1, 1, 1));
		//renderer->FullScreenTexturedQuad(_renderTarget);
	}

	void ListBox::AddItem(const std::wstring& label, ITexture2D* icon)
	{
		_items.push_back({ label, icon });
	}

	void ListBox::RenderItems(GuiRenderer* renderer, const Point& position)
	{
		_hoverIdx = -1;

		// acquire the current render state
		/*g_pEnv->_graphicsDevice->GetRenderTargets(_oldRenderTargets, &_oldDepthStenchil);
		_oldViewport = g_pEnv->_graphicsDevice->GetBackBufferViewport();

		g_pEnv->_graphicsDevice->SetRenderTarget(_renderTarget);
		_renderTarget->ClearRenderTargetView(math::Color(0, 0, 0, 0));

		D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)_size.x, (float)_size.y, 0.0f, 1.0f };*/
		//g_pEnv->_graphicsDevice->SetViewports({ vp });
		{
			Point pos(position.x, position.y);			
			const int32_t lineHeight = 20;
			Point size(_size.x, lineHeight);

			int32_t i = -1;
			for (auto& item : _items)
			{
				if (++i < _offset)
					continue;

				if (IsMouseOver(pos, size))
				{
					renderer->FillQuad(pos.x, pos.y, size.x, size.y, renderer->_style.listbox_highlight);

					_hoverIdx = i;
				}
				else if (i % 2 == 0)
					renderer->FillQuad(pos.x, pos.y, size.x, size.y, renderer->_style.listbox_alternate_colour);

				if (item.icon)
					renderer->FillTexturedQuad(item.icon, pos.x + 4, pos.y + 1, 16, 16, math::Color(1, 1, 1, 1));

				renderer->PrintText(renderer->_style.font, (uint8_t)Style::FontSize::Tiny, pos.x + 24, pos.y + size.y / 2, renderer->_style.text_regular, FontAlign::CentreUD, item.label);

				pos.y += lineHeight;

				if (pos.y >= GetAbsolutePosition().y + GetSize().y)
					break;
			}
		}
		// reset
		//g_pEnv->_graphicsDevice->SetRenderTargets(_oldRenderTargets, _oldDepthStenchil);
		//g_pEnv->_graphicsDevice->SetViewports({ _oldViewport });
	}

	bool ListBox::OnInputEvent(InputEvent event, InputData* data)
	{
		if (event == InputEvent::MouseDown && data->MouseDown.button == VK_LBUTTON)
		{
			if (OnClickItem && _hoverIdx != -1)
			{
				if (OnClickItem(this, &_items.at(_hoverIdx)))
					return true;
			}
		}
		else if (event == InputEvent::MouseWheel && IsMouseOver(true))
		{
			_offset += data->MouseWheel.delta / 10;

			if (_offset > _items.size() - 1)
				_offset = _items.size() - 1;
			else if (_offset < 0)
				_offset = 0;
		}
		return false;
	}
}