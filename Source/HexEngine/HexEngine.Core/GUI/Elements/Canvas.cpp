
#include "Canvas.hpp"
#include "../../Environment/IEnvironment.hpp"
#include "../../Graphics/IGraphicsDevice.hpp"
#include "../../Graphics/IShader.hpp"
#include "../../Environment/LogFile.hpp"
#include <algorithm>

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
			nullptr,
			(D3D11_CPU_ACCESS_FLAG)0,
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
			//_drawList = renderer->PushDrawList();

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
			//renderer->ListDraw(_drawList);
			//renderer->PopDrawList();

			// reset back to original render targets
			g_pEnv->_graphicsDevice->SetRenderTargets(_prevRenderTargets);

			_needsRedraw = false;
		}
	}

	bool Canvas::NeedsRedrawing() const
	{
		return _needsRedraw;
	}

	void Canvas::Present(GuiRenderer* renderer, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
	{
		if (_renderTarget == nullptr)
			return;

		auto backBuffer = g_pEnv->_graphicsDevice->GetBackBuffer();
		const bool hdrBackBuffer = backBuffer != nullptr && backBuffer->GetFormat() == DXGI_FORMAT_R16G16B16A16_FLOAT;

		if (hdrBackBuffer)
		{
			if (!_hdrPresentShader)
				_hdrPresentShader = IShader::Create("EngineData.Shaders/UIBasicHDRCanvas.hcs");

			renderer->FillTexturedQuadWithShader(_renderTarget,
				x, y,
				width, height,
				math::Color(1, 1, 1, 1),
				_hdrPresentShader.get());
			return;
		}

		renderer->FillTexturedQuad(_renderTarget,
			x, y,
			width, height,
			math::Color(1, 1, 1, 1));
	}

	void Canvas::Present(GuiRenderer* renderer, uint32_t x, uint32_t y, uint32_t width, uint32_t height, const RECT& srcRect)
	{
		if (_renderTarget == nullptr || _width == 0 || _height == 0)
			return;

		RECT clamped = srcRect;
		clamped.left = std::clamp(clamped.left, 0L, (long)_width);
		clamped.top = std::clamp(clamped.top, 0L, (long)_height);
		clamped.right = std::clamp(clamped.right, 0L, (long)_width);
		clamped.bottom = std::clamp(clamped.bottom, 0L, (long)_height);

		if (clamped.right <= clamped.left || clamped.bottom <= clamped.top)
			return;

		math::Vector2 uv[2];
		uv[0] = math::Vector2((float)clamped.left / (float)_width, (float)clamped.top / (float)_height);
		uv[1] = math::Vector2((float)clamped.right / (float)_width, (float)clamped.bottom / (float)_height);

		auto backBuffer = g_pEnv->_graphicsDevice->GetBackBuffer();
		const bool hdrBackBuffer = backBuffer != nullptr && backBuffer->GetFormat() == DXGI_FORMAT_R16G16B16A16_FLOAT;

		if (hdrBackBuffer)
		{
			if (!_hdrPresentShader)
				_hdrPresentShader = IShader::Create("EngineData.Shaders/UIBasicHDRCanvas.hcs");

			renderer->FillTexturedQuadWithShader(_renderTarget, x, y, width, height, uv, math::Color(1, 1, 1, 1), _hdrPresentShader.get());
			return;
		}

		renderer->FillTexturedQuad(_renderTarget, x, y, width, height, uv, math::Color(1, 1, 1, 1));
	}
}
