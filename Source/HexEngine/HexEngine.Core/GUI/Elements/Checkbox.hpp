
#pragma once

#include "Element.hpp"

namespace HexEngine
{
	class Checkbox : public Element
	{
	public:
		using OnCheckFn = std::function<void(Checkbox*, bool)>;

		Checkbox(Element* parent, const Point& position, const Point& size, const std::wstring& label, bool* value);

		~Checkbox();

		virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override;
		virtual bool OnInputEvent(InputEvent event, InputData* data) override;

		void SetOnCheckFn(OnCheckFn fn);

		virtual int32_t GetLabelWidth() const;

	private:
		bool* _value;
		std::wstring _label;
		ITexture2D* _tickImg;
		bool _hovering = false;
		OnCheckFn _onCheckFn;
	};
}
