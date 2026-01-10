
#include "Dock.hpp"
#include "../GuiRenderer.hpp"
#include "../../Scene/SceneManager.hpp"

namespace HexEngine
{
	Dock::Dock(Element* parent, const Point& position, const Point& size, Anchor anchor) :
		Element(parent, position, size),
		_anchor(anchor)
	{}

	void Dock::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		if(_anchor != Anchor::Middle)			
			renderer->FillQuad(_position.x, _position.y, _size.x, _size.y, renderer->_style.win_back);

		renderer->Frame(_position.x, _position.y, _size.x, _size.y, 1, renderer->_style.win_border);

		if (_anchor == Anchor::Middle)
		{
			bool hasRenderered = false;
			if (auto scene = g_pEnv->_sceneManager->GetCurrentScene(); scene != nullptr)
			{
				if (auto mainCamera = scene->GetMainCamera(); mainCamera != nullptr)
				{
					renderer->FillTexturedQuad(mainCamera->GetRenderTarget(),
						GetPosition().x, GetPosition().y,
						GetSize().x, GetSize().y,
						math::Color(1, 1, 1, 1));

					hasRenderered = true;
				}
			}

			if(!hasRenderered)
				renderer->FillQuad(_position.x, _position.y, _size.x, _size.y, renderer->_style.win_render_area);
		}

		//switch (_anchor)
		//{
		//case Anchor::Left:
		//	renderer->Line(_position.x + _size.x, _position.y, _position.x + _size.x, _position.y + _size.y, style.win_border);
		//	//renderer->Line(_position.x + _size.x - 1, _position.y, _position.x + _size.x - 1, _position.y + _size.y, style.win_highlight);
		//	break;

		//case Anchor::Right:
		//	renderer->Line(_position.x, _position.y, _position.x, _position.y + _size.y, style.win_border);
		//	break;
		//}
	}

	bool Dock::OnInputEvent(InputEvent event, InputData* data)
	{
		if (_anchor == Anchor::Middle)
		{
			
		}
		return false;
	}
}