

#pragma once

#include "Element.hpp"

namespace HexEngine
{
	class Dialog : public Element
	{
	public:
		using CallbackFn = std::function<void ()>;		

		Dialog(Element* parent, const Point& position, const Point& size, const std::wstring& title, CallbackFn callback = nullptr);

		virtual ~Dialog();

		virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override;

		virtual bool OnInputEvent(InputEvent event, InputData* data) override;

		virtual Point GetAbsolutePosition() const override;

	private:
		std::wstring _title;
		bool _beingDragged = false;
		Point _dragStart;
		ITexture2D* _logo = nullptr;
		bool _hoveringCloseButton = false;

	protected:
		CallbackFn _callback;
	};
}
