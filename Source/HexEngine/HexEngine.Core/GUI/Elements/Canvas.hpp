
#pragma once

#include "../../Required.hpp"
#include "../../Graphics/ITexture2D.hpp"
#include "../GuiRenderer.hpp"

namespace HexEngine
{
	class HEX_API Canvas
	{
	public:
		~Canvas();

		bool Create(uint32_t width, uint32_t height);
		void Resize(uint32_t width, uint32_t height);
		void Destroy();
		void Redraw();
		bool NeedsRedrawing() const;

		bool BeginDraw(GuiRenderer* renderer, uint32_t width, uint32_t height, int32_t originX = 0, int32_t originY = 0);
		void EndDraw(GuiRenderer* renderer);
		void Present(GuiRenderer* renderer, uint32_t x, uint32_t y, uint32_t width, uint32_t height);
		void Present(GuiRenderer* renderer, uint32_t x, uint32_t y, uint32_t width, uint32_t height, const RECT& srcRect);

	private:
		ITexture2D* _renderTarget = nullptr;
		uint32_t _width = 0;
		uint32_t _height = 0;
		uint32_t _texWidth = 0;
		uint32_t _texHeight = 0;
		bool _needsRedraw = true;
		bool _capturing = false;

		//DrawList* _drawList = nullptr;

		std::vector<ITexture2D*> _prevRenderTargets;
		std::shared_ptr<IShader> _hdrPresentShader;
	};
}
