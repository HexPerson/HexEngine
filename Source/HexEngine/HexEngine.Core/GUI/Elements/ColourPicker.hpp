
#pragma once

#include "Element.hpp"

namespace HexEngine
{
	class Dialog;

	/** @brief Colour field that opens a popup dialog for HSV/RGB/HEX editing. */
	class HEX_API ColourPicker : public Element
	{
	public:
		ColourPicker(Element* parent, const Point& position, const Point& size, const std::wstring& label, math::Color* col);
		virtual ~ColourPicker();

		virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override;
		virtual bool OnInputEvent(InputEvent event, InputData* data) override;
		virtual int32_t GetLabelWidth() const override;
		virtual std::wstring GetLabelText() const override;

	private:
		void OpenPopup();
		void EnsureValidColour();
		std::wstring BuildHexLabel() const;

	private:
		std::wstring _label;
		math::Color* _colour;
		math::Color _ownedColour;
		Dialog* _popup = nullptr;
	};
}
