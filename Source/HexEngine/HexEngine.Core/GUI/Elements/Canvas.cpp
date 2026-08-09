
#include "Canvas.hpp"
#include "../../Environment/IEnvironment.hpp"
#include "../../Graphics/IGraphicsDevice.hpp"
#include "../../Graphics/IShader.hpp"
#include "../../Environment/LogFile.hpp"
#include <algorithm>

namespace
{
	// Canvas render targets are bucketed so elements being interactively
	// resized don't reallocate their texture every frame.
	constexpr uint32_t CanvasSizeBucket = 64;

	uint32_t BucketSize(uint32_t size)
	{
		return ((std::max(size, 1u) + CanvasSizeBucket - 1) / CanvasSizeBucket) * CanvasSizeBucket;
	}

	// Viewports of the canvases currently capturing, so nested captures
	// (e.g. a Button redrawing inside a ScrollView capture) restore the
	// enclosing canvas' viewport instead of the back buffer's.
	std::vector<HexEngine::Viewport> s_activeCanvasViewports;
}

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
		_texWidth = BucketSize(width);
		_texHeight = BucketSize(height);

		auto backBuffer = g_pEnv->_graphicsDevice->GetBackBuffer();

		_renderTarget = g_pEnv->_graphicsDevice->CreateTexture2D(
			_texWidth, _texHeight,
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

		if (_renderTarget && BucketSize(width) == _texWidth && BucketSize(height) == _texHeight)
		{
			_width = width;
			_height = height;
			return;
		}

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

	bool Canvas::BeginDraw(GuiRenderer* renderer, uint32_t width, uint32_t height, int32_t originX, int32_t originY)
	{
		if (width != _width || height != _height)
		{
			Resize(width, height);
		}

		if (_renderTarget == nullptr)
		{
			Create(width, height);

			if (_renderTarget == nullptr)
				return false;
		}

		if (_needsRedraw == true)
		{
			//_drawList = renderer->PushDrawList();

			// save the old render targets first
			g_pEnv->_graphicsDevice->GetRenderTargets(_prevRenderTargets);

			_renderTarget->ClearRenderTargetView(math::Color(0, 0, 0, 0));

			g_pEnv->_graphicsDevice->SetRenderTarget(_renderTarget);

			// Callers draw in window coordinates; shift the viewport so the
			// owning element's origin lands on canvas texel (0,0). Anything
			// outside the canvas is clipped by the render target bounds.
			const auto& bbVp = g_pEnv->_graphicsDevice->GetBackBufferViewport();
			Viewport vp((float)-originX, (float)-originY, bbVp.width, bbVp.height);
			g_pEnv->_graphicsDevice->SetViewport(vp);
			s_activeCanvasViewports.push_back(vp);

			_capturing = true;
		}

		return _needsRedraw;
	}

	void Canvas::EndDraw(GuiRenderer* renderer)
	{
		if (_capturing)
		{
			//renderer->ListDraw(_drawList);
			//renderer->PopDrawList();

			// reset back to original render targets
			g_pEnv->_graphicsDevice->SetRenderTargets(_prevRenderTargets);

			if (!s_activeCanvasViewports.empty())
				s_activeCanvasViewports.pop_back();

			if (s_activeCanvasViewports.empty())
				g_pEnv->_graphicsDevice->SetViewport(g_pEnv->_graphicsDevice->GetBackBufferViewport());
			else
				g_pEnv->_graphicsDevice->SetViewport(s_activeCanvasViewports.back());

			_capturing = false;
			_needsRedraw = false;
		}
	}

	bool Canvas::NeedsRedrawing() const
	{
		return _needsRedraw;
	}

	void Canvas::Present(GuiRenderer* renderer, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
	{
		if (_renderTarget == nullptr || _texWidth == 0 || _texHeight == 0)
			return;

		// The texture may be bucketed larger than the canvas; only sample the used region.
		math::Vector2 uv[2];
		uv[0] = math::Vector2(0.0f, 0.0f);
		uv[1] = math::Vector2((float)_width / (float)_texWidth, (float)_height / (float)_texHeight);

		auto backBuffer = g_pEnv->_graphicsDevice->GetBackBuffer();
		const bool hdrBackBuffer = backBuffer != nullptr && backBuffer->GetFormat() == DXGI_FORMAT_R16G16B16A16_FLOAT;

		if (hdrBackBuffer)
		{
			if (!_hdrPresentShader)
				_hdrPresentShader = IShader::Create("EngineData.Shaders/UIBasicHDRCanvas.hcs");

			renderer->FillTexturedQuadWithShader(_renderTarget,
				x, y,
				width, height,
				uv,
				math::Color(1, 1, 1, 1),
				_hdrPresentShader.get());
			return;
		}

		renderer->FillTexturedQuad(_renderTarget,
			x, y,
			width, height,
			uv,
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
		uv[0] = math::Vector2((float)clamped.left / (float)_texWidth, (float)clamped.top / (float)_texHeight);
		uv[1] = math::Vector2((float)clamped.right / (float)_texWidth, (float)clamped.bottom / (float)_texHeight);

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
