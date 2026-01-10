
#pragma once

#include <HexEngine.Core\HexEngine.hpp>

namespace HexEditor
{
	class SceneView : public Element
	{
	public:
		enum class RoamState
		{
			None,
			FreeLook,	// right mouse
			Move,		// middle mouse
			Drag,		// left mouse
		};

		SceneView(Element* parent, const Point& position, const Point& size);

		virtual bool OnInputEvent(InputEvent event, InputData* data) override;
		virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override;

		RoamState GetRoamState() const { return _roamState; }
		const Point& GetRoamingMouseStartPos() const { return _mouseActionStartPos; }
		void SetRoamingMouseStartPos(const Point& pos) {
			_mouseActionStartPos = pos;
		}

	private:
		RoamState _roamState = RoamState::None;
		Point _mouseActionStartPos;
		Entity* _dragAndDropEntity = nullptr;
	};
}