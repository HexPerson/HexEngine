
#pragma once

#include "Element.hpp"
#include "InputFloat.hpp"

namespace HexEngine
{
	class HEX_API Vector3Edit : public Element
	{
	public:
		Vector3Edit(Element* parent, const Point& position, const Point& size, const std::wstring& label, math::Vector3* vector, std::function<void(const math::Vector3&)> callback=nullptr);

		void SetAxisLabels(const std::wstring labels[3]);

		virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override;

		virtual int32_t GetLabelWidth() const override;
		virtual std::wstring GetLabelText() const override;

	private:
		void OnSetAxisValue(int32_t axis, LineEdit* edit, const std::wstring& value);
		void OnSetAxisValueFloat(int32_t axis, float value);

	private:
		std::wstring _label;
		math::Vector3* _vector;
		math::Vector3 _lastVectorVal;
		std::wstring _axisLabels[3];
		InputFloat* _lineEdits[3] = { nullptr };
		std::function<void(const math::Vector3&)> _callback;
	};
}
