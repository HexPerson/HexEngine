
#include "MaterialEditor.hpp"

namespace HexEditor
{
	MaterialEditor::MaterialEditor(Element* parent, const HexEngine::Point& position, const HexEngine::Point& size) :
		Dialog(parent, position, size, L"Material Editor")
	{

	}

	MaterialEditor::~MaterialEditor()
	{

	}

	void MaterialEditor::Render(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		Dialog::Render(renderer, w, h);
	}
}