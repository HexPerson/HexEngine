
#pragma once

#include <HexEngine.Core\HexEngine.hpp>
#include <functional>

namespace HexEngine
{
	class TabView;
	class TabItem;
	class Button;
	class Element;
}

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
		SceneView(
			Element* parent,
			const HexEngine::Point& position,
			const HexEngine::Point& size,
			const std::function<void()>& onRunGame,
			const std::function<void()>& onStopGame,
			const std::function<bool()>& isGameRunning);

		virtual bool OnInputEvent(HexEngine::InputEvent event, HexEngine::InputData* data) override;
		virtual void Render(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h) override;

		RoamState GetRoamState() const { return _roamState; }
		const HexEngine::Point& GetRoamingMouseStartPos() const { return _mouseActionStartPos; }
		void SetRoamingMouseStartPos(const HexEngine::Point& pos) {
			_mouseActionStartPos = pos;
		}

		HexEngine::TabItem* AddWorkspaceTab(const std::wstring& label);
		bool IsSceneTabActive() const;
		bool IsMouseOverSceneViewport() const;
		HexEngine::Point GetSceneViewportAbsolutePosition() const;
		HexEngine::Point GetSceneViewportSize() const;

	private:
		void InitializeUi();
		HexEngine::Point GetSceneSurfaceOffset() const;

	private:
		RoamState _roamState = RoamState::None;
		HexEngine::Point _mouseActionStartPos;
		HexEngine::Entity* _dragAndDropEntity = nullptr;
		HexEngine::TabView* _tabView = nullptr;
		HexEngine::TabItem* _sceneTab = nullptr;
		HexEngine::Button* _runButton = nullptr;
		HexEngine::Button* _stopButton = nullptr;
		HexEngine::Element* _sceneSurface = nullptr;
		std::function<void()> _onRunGame;
		std::function<void()> _onStopGame;
		std::function<bool()> _isGameRunning;
	};
}
