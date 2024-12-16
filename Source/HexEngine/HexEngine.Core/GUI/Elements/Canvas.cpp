
#include "Canvas.hpp"
#include "../../Environment/IEnvironment.hpp"
#include "../../Graphics/IGraphicsDevice.hpp"
#include "../../Environment/LogFile.hpp"

namespace HexEngine
{
	Canvas::~Canvas()
	{
		Destroy();
	}

	bool Canvas::Create(uint32_t width, uint32_t height)
	{
		SAFE_DELETE(_renderTarget);

		_width = width;
		_height = height;

		auto backBuffer = g_pEnv->_graphicsDevice->GetBackBuffer();

		_renderTarget = g_pEnv->_graphicsDevice->CreateTexture2D(
			width, height,
			(DXGI_FORMAT)backBuffer->GetFormat(),
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0,
			1,
			0,
			D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			D3D11_SRV_DIMENSION_TEXTURE2D
		);

		if (!_renderTarget)
		{
			LOG_CRIT("Could not create render target for Canvas!");
			return false;
		}

		return true;
	}

	void Canvas::Resize(uint32_t width, uint32_t height)
	{
		if (_width == width && _height == height)
			return;

		SAFE_DELETE(_renderTarget);

		Create(width, height);
	}

	void Canvas::Destroy()
	{
		SAFE_DELETE(_renderTarget);
	}

	void Canvas::Redraw()
	{
		_needsRedraw = true;
	}

	bool Canvas::BeginDraw(GuiRenderer* renderer, uint32_t width, uint32_t height)
	{
		if (width != _width || height != _height)
		{
			Resize(width, height);
		}

		if (_needsRedraw == true)
		{
			_drawList = renderer->PushDrawList();

			// save the old render targets first
			g_pEnv->_graphicsDevice->GetRenderTargets(_prevRenderTargets);

			const auto& vp = g_pEnv->_graphicsDevice->GetBackBufferViewport();

			_renderTarget->ClearRenderTargetView(math::Color(0, 0, 0, 0));

			g_pEnv->_graphicsDevice->SetRenderTarget(_renderTarget);
		}

		return _needsRedraw;
	}

	void Canvas::EndDraw(GuiRenderer* renderer)
	{
		if (_needsRedraw)
		{
			renderer->ListDraw(_drawList);
			renderer->PopDrawList();

			// reset back to original render targets
			g_pEnv->_graphicsDevice->SetRenderTargets(_prevRenderTargets);

			_needsRedraw = false;
		}
	}


	void Canvas::Present(GuiRenderer* renderer, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
	{
		if (_renderTarget != nullptr)
		{
			renderer->FillTexturedQuad(_renderTarget,
				x, y,
				width, height,
				math::Color(1, 1, 1, 1));
		}
	}
}