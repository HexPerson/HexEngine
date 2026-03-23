
#pragma once

#include "Element.hpp"
#include "DragFloat.hpp"

namespace HexEngine
{
	class HEX_API Vector2Edit : public Element
	{
	public:
		Vector2Edit(Element* parent, const Point& position, const Point& size, const std::wstring& label, math::Vector2* vector, std::function<void(const math::Vector2&)> callback);

		void SetAxisLabels(const std::wstring labels[2]);

		virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override;

		virtual int32_t GetLabelWidth() const override;
		virtual std::wstring GetLabelText() const override;

	private:
		void OnSetAxisValue(int32_t axis, LineEdit* edit, const std::wstring& value);
		void OnSetAxisValueFloat(int32_t axis, float value);

	private:
		std::wstring _label;
		math::Vector2* _vector;
		math::Vector2 _lastVectorVal;
		std::wstring _axisLabels[2];
		DragFloat* _lineEdits[3] = { nullptr };
		std::function<void(const math::Vector2&)> _callback;
	};
}
