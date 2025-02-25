
#pragma once

#include "Element.hpp"
#include "LineEdit.hpp"

namespace HexEngine
{
	class HEX_API DragInt : public LineEdit
	{
	public:
		using OnDrag = std::function<void(int32_t*, int32_t, int32_t)>;

		DragInt(Element* parent, const Point& position, const Point& size, const std::wstring& label, int32_t* value, int32_t min, int32_t max, int32_t scale);

		virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override;
		virtual bool OnInputEvent(InputEvent event, InputData* data) override;
		void SetOnDrag(OnDrag fn);

	private:
		int32_t* _value;
		int32_t _min;
		int32_t _max;
		int32_t _scale;
		int32_t _lastValue;
		bool _dragging = false;
		int32_t _startDrag = -1;
		OnDrag _onDrag = nullptr;
	};
}
