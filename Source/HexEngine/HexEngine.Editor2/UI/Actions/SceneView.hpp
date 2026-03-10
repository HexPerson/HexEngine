
#pragma once

#include <HexEngine.Core\HexEngine.hpp>

namespace HexEditor
{
	class SceneView : public HexEngine::Element
	{
	public:
		enum class RoamState
		{
			None,
			FreeLook,	// right mouse
			Move,		// middle mouse
			Drag,		// left mouse
		};

		SceneView(Element* parent, const HexEngine::Point& position, const HexEngine::Point& size);

		virtual bool OnInputEvent(HexEngine::InputEvent event, HexEngine::InputData* data) override;
		virtual void Render(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h) override;

		RoamState GetRoamState() const { return _roamState; }
		const HexEngine::Point& GetRoamingMouseStartPos() const { return _mouseActionStartPos; }
		void SetRoamingMouseStartPos(const HexEngine::Point& pos) {
			_mouseActionStartPos = pos;
		}

	private:
		RoamState _roamState = RoamState::None;
		HexEngine::Point _mouseActionStartPos;
		HexEngine::Entity* _dragAndDropEntity = nullptr;
	};
}