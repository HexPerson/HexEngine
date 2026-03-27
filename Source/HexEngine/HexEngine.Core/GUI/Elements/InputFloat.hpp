#pragma once

#include "Element.hpp"
#include "LineEdit.hpp"

namespace HexEngine
{
	class HEX_API InputFloat : public LineEdit
	{
	public:
		using OnDrag = std::function<void(float, float, float)>;

		InputFloat(Element* parent, const Point& position, const Point& size, const std::wstring& label, float* value, float min, float max, float scale, int32_t decimalPlaces = 2);

		virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override;
		virtual bool OnInputEvent(InputEvent event, InputData* data) override;
		void SetOnDrag(OnDrag fn);

	private:
		void CommitTextValue();

	private:
		float* _value;
		float _min;
		float _max;
		float _scale;
		bool _dragging = false;
		int32_t _startDrag = -1;
		OnDrag _onDrag = nullptr;
	};
}
