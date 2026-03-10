
#pragma once

#include <HexEngine.Core\HexEngine.hpp>

namespace HexEditor
{
	class MaterialEditor : public HexEngine::Dialog
	{
	public:
		MaterialEditor(Element* parent, const HexEngine::Point& position, const HexEngine::Point& size);
		~MaterialEditor();

		virtual void Render(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h) override;
	};
}
