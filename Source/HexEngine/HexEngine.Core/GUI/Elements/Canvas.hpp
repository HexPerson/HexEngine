
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

		bool BeginDraw(GuiRenderer* renderer, uint32_t width, uint32_t height);
		void EndDraw(GuiRenderer* renderer);
		void Present(GuiRenderer* renderer, uint32_t x, uint32_t y, uint32_t width, uint32_t height);

	private:
		ITexture2D* _renderTarget = nullptr;
		uint32_t _width = 0;
		uint32_t _height = 0;
		bool _needsRedraw = true;

		DrawList* _drawList = nullptr;

		std::vector<ITexture2D*> _prevRenderTargets;
	};
}
