
#pragma once

#include <HexEngine.Core\HexEngine.hpp>

namespace HexEditor
{
	class MaterialEditor : public Dialog
	{
	public:
		MaterialEditor(Element* parent, const Point& position, const Point& size);
		~MaterialEditor();

		virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override;
	};
}
