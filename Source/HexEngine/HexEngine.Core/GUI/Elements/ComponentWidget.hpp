
#pragma once

#include "Element.hpp"

namespace HexEngine
{
	class HEX_API ComponentWidget : public Element
	{
	public:
		ComponentWidget(Element* parent, const Point& position, const Point& size, const std::wstring& label);

		virtual void OnAddChild(Element* child) override;
		virtual void OnRemoveChild(Element* child) override;

		virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override;

		Point GetNextPos();

		int32_t GetTotalHeight() const;

		void CalculateLargestLabelWidth();

		void AddComponentChild(Element* child);
		void RemoveComponentChild(Element* child);

	private:
		int32_t _totalHeight = 0;
		std::wstring _label;
		int32_t _largestLabelWidth = 0;
	};
}
