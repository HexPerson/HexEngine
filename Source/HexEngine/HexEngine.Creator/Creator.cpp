
#include "Creator.hpp"

namespace HexCreator
{
	void Creator::OnCreateGame()
	{

	}

	void Creator::OnGUI()
	{
		auto renderer = g_pEnv->_uiManager->GetRenderer();	

		for (auto& tile : _app._tiles)
		{
			//tile->surface()->set_dirty_bounds(ultralight::IntRect(0, 0, tile->surface()->width(), tile->surface()->height()));

			renderer->FillTexturedQuad(
				tile->surface()->_texture,
				0, 0,
				tile->surface()->width(),
				tile->surface()->height(),
				math::Color(0xFFFFFFFF));
		}
	}
}