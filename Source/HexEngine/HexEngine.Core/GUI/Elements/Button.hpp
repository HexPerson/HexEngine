
#pragma once

#include "Element.hpp"

namespace HexEngine
{
	class HEX_API Button : public Element
	{
	public:
		Button(Element* parent, const Point& position, const Point& size, const std::wstring& label, std::function<bool(Button*)> action);
		Button(Element* parent, const Point& position, const Point& size, const std::shared_ptr<ITexture2D>& icon, std::function<bool(Button*)> action);

		virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override;
		virtual bool OnInputEvent(InputEvent event, InputData* data) override;

		void SetHighlightOverride(const math::Color& colour);
		void RemoveHighlightOverride();

		void SetIcon(std::shared_ptr<ITexture2D> icon);

	private:
		std::wstring _label;
		std::shared_ptr<ITexture2D> _icon;
		std::function<bool(Button*)> _action;
		bool _hasHighlightOverride = false;
		math::Color _highlightOverride;
		bool _wasHovering = false;
	};
}
