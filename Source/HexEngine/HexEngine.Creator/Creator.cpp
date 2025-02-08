
#include "Creator.hpp"

namespace HexCreator
{
	void Creator::OnCreateGame()
	{

	}

	void Creator::OnGUI()
	{
		_app.Run();

		auto renderer = g_pEnv->_uiManager->GetRenderer();	

		for (auto& tile : _app._tiles)
		{
			//tile->surface()->set_dirty_bounds(ultralight::IntRect(0, 0, tile->surface()->width(), tile->surface()->height()));

			//tile->view()->Focus();

			tile->surface()->Update();

			renderer->FillTexturedQuad(
				tile->surface()->_texture,
				0, 0,
				tile->surface()->width(),
				tile->surface()->height(),
				math::Color(0xFFFFFFFF));

			//tile->surface()->ClearDirtyBounds();
		}
	}
}