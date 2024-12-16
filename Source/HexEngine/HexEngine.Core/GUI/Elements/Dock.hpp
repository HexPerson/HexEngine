
#pragma once

#include "Element.hpp"

namespace HexEngine
{
	class Dock : public Element
	{
	public:
		enum class Anchor
		{
			Left,
			Right,
			Top,
			Bottom,
			Middle
		};

		enum class RoamState
		{
			None,
			FreeLook,	// right mouse
			Move,		// middle mouse
			Drag,		// left mouse
		};

		Dock(Element* parent, const Point& position, const Point& size, Anchor anchor);

		virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override;

		virtual bool OnInputEvent(InputEvent event, InputData* data) override;

		RoamState GetRoamState() const { return _roamState; }

		const Point& GetRoamingMouseStartPos() const { return _mouseActionStartPos; }

		void SetRoamingMouseStartPos(const Point& pos) {
			_mouseActionStartPos = pos;
		}

	private:
		Anchor _anchor;
		RoamState _roamState = RoamState::None;
		Point _mouseActionStartPos;
	};
}
