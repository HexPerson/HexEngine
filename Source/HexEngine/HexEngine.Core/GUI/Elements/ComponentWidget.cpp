
#include "ComponentWidget.hpp"
#include "../GuiRenderer.hpp"

namespace HexEngine
{
	const int32_t ComponentVerticalPadding = 10;

	ComponentWidget::ComponentWidget(Element* parent, const Point& position, const Point& size, const std::wstring& label) :
		Element(parent, position, size),
		_label(label)
	{}

	void ComponentWidget::OnAddChild(Element* child)
	{
		Element::OnAddChild(child);

		AddComponentChild(child);
	}

	void ComponentWidget::OnRemoveChild(Element* child)
	{
		Element::OnRemoveChild(child);

		RemoveComponentChild(child);		
	}

	void ComponentWidget::AddComponentChild(Element* child)
	{
		_totalHeight += (child->GetSize().y + ComponentVerticalPadding);

		CalculateLargestLabelWidth();

		// cast the parent (if any) to a component widget and send the child addition upwards, so nested widgets work correctly
		if (auto parent = dynamic_cast<ComponentWidget*>(GetParent()); parent != nullptr)
		{
			parent->AddComponentChild(child);
		}
	}

	void ComponentWidget::RemoveComponentChild(Element* child)
	{
		_totalHeight -= (child->GetSize().y + ComponentVerticalPadding);

		CalculateLargestLabelWidth();

		// cast the parent (if any) to a component widget and send the child addition upwards, so nested widgets work correctly
		if (auto parent = dynamic_cast<ComponentWidget*>(GetParent()); parent != nullptr)
		{
			parent->RemoveComponentChild(child);
		}
	}

	void ComponentWidget::CalculateLargestLabelWidth()
	{
		_largestLabelWidth = 0;

		for (auto& child : _children)
		{
			if (auto width = child->GetLabelWidth(); width > _largestLabelWidth)
			{
				_largestLabelWidth = width;
			}
		}

		_largestLabelWidth += 5; // allow a bit of extra space

		for (auto& child : _children)
		{
			//if(_largestLabelWidth > child->GetLabelMinSize())
				child->SetLabelMinSize(_largestLabelWidth);
		}
	}

	void ComponentWidget::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		auto pos = GetAbsolutePosition();

		renderer->FillQuad(pos.x, pos.y, _size.x, _totalHeight + 30, renderer->_style.inspector_widget_back);
		renderer->Frame(pos.x, pos.y, _size.x, _totalHeight + 30, 1, renderer->_style.win_border);

		renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Tiny, pos.x + _size.x / 2, pos.y + 2, renderer->_style.text_highlight, FontAlign::CentreLR, _label);
	}

	Point ComponentWidget::GetNextPos()
	{
		return Point(10, 22 + _totalHeight);
	}

	int32_t ComponentWidget::GetTotalHeight() const
	{
		return _totalHeight + 30;
	}
}