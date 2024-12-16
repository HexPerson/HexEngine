
#include "MaterialEditor.hpp"

namespace HexEditor
{
	MaterialEditor::MaterialEditor(Element* parent, const Point& position, const Point& size) :
		Dialog(parent, position, size, L"Material Editor")
	{

	}

	MaterialEditor::~MaterialEditor()
	{

	}

	void MaterialEditor::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		Dialog::Render(renderer, w, h);
	}
}