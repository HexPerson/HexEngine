
#include "Vector3Edit.hpp"
#include "../GuiRenderer.hpp"
#include "../UIManager.hpp"

namespace HexEngine
{
	const int32_t Vector3EditBoxWidth = 75;

	Vector3Edit::Vector3Edit(Element* parent, const Point& position, const Point& size, const std::wstring& label, math::Vector3* vector, std::function<void (const math::Vector3&)> callback) :
		Element(parent, position, size),
		_label(label),
		_vector(vector),
		_callback(callback)
	{
		int32_t xpos = 90;
		for (int i = 0; i < 3; ++i)
		{
			_lineEdits[i] = new DragFloat(this, Point(xpos, 0), Point(Vector3EditBoxWidth, size.y), L"", &((float*)&vector->x)[i], -9999.0f, 9999.0f, 0.1f);

			_lineEdits[i]->SetValue(std::format(L"{:.2f}", ((float*)&vector->x)[i]));	
			_lineEdits[i]->SetOnDrag(std::bind(&Vector3Edit::OnSetAxisValueFloat, this, i, std::placeholders::_1));
			_lineEdits[i]->SetOnInputFn(std::bind(&Vector3Edit::OnSetAxisValue, this, i, std::placeholders::_1, std::placeholders::_2));

			xpos += Vector3EditBoxWidth + 20;
		}

		// give the axis labels some default values
		_axisLabels[0] = L"X";
		_axisLabels[1] = L"Y";
		_axisLabels[2] = L"Z";
	}

	void Vector3Edit::OnSetAxisValue(int32_t axis, LineEdit* edit, const std::wstring& value)
	{
		if (axis >= 0 && axis <= 2)
		{
			math::Vector3 changedValue = *_vector;

			((float*)&changedValue.x)[axis] = std::stof(value);

			if (_callback)
				_callback(changedValue);

			*_vector = changedValue;
		}
	}

	void Vector3Edit::OnSetAxisValueFloat(int32_t axis, float value)
	{
		if (axis >= 0 && axis <= 2)
		{
			math::Vector3 changedValue = *_vector;

			((float*)&changedValue.x)[axis] = value;

			if (_callback)
				_callback(changedValue);

			*_vector = changedValue;
		}
	}

	void Vector3Edit::SetAxisLabels(const std::wstring labels[3])
	{
		for (int i = 0; i < 3; ++i)
			_axisLabels[i] = labels[i];
	}

	void Vector3Edit::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		auto pos = GetAbsolutePosition();

		const math::Color axisCols[3] = {
			math::Color(HEX_RGBA_TO_FLOAT4(255,64,64,255)),
			math::Color(HEX_RGBA_TO_FLOAT4(80,226,75,255)),
			math::Color(HEX_RGBA_TO_FLOAT4(4,155,213,255))
		};

		if(_label.length() > 0)
			renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Tiny, pos.x, pos.y + _size.y / 2, renderer->_style.text_regular, FontAlign::CentreUD, _label);

		// draw the axis boxes
		for (int32_t i = 0; i < 3; ++i)
		{
			// Update the line edits if the vector changed
			if (((float*)&_vector->x)[i] != ((float*)&_lastVectorVal.x)[i])
			{
				_lineEdits[i]->SetValue(std::format(L"{:.2f}", ((float*)&_vector->x)[i]));
			}

			auto editPos = _lineEdits[i]->GetAbsolutePosition();

			renderer->FillQuad(editPos.x - 13, editPos.y, 12, _size.y, axisCols[i]);
			renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Tiny, editPos.x - 13 + 6, editPos.y + _size.y / 2, math::Color(0, 0, 0, 1), FontAlign::CentreLR | FontAlign::CentreUD, _axisLabels[i]);
		}

		_lastVectorVal = *_vector;
	}

	int32_t Vector3Edit::GetLabelWidth() const
	{
		int32_t width = 0, height = 0;

		if (_label.length() > 0)
			g_pEnv->_uiManager->GetRenderer()->_style.font->MeasureText((int32_t)Style::FontSize::Tiny, _label, width, height);

		return width;
	}
}