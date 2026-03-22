
#pragma once

#include <HexEngine.Core\HexEngine.hpp>
#include <functional>
#include <vector>

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
			const std::function<bool()>& isGameRunning,
			const std::function<void()>& onSavePrefab,
			const std::function<void()>& onExitPrefab,
			const std::function<bool()>& isPrefabMode);

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
		std::vector<HexEngine::Entity*> _dragAndDropPrefabRoots;
		std::vector<math::Vector3> _dragAndDropPrefabRootOffsets;
		bool _ignoreNextConsumedDroppedAsset = false;
		HexEngine::TabView* _tabView = nullptr;
		HexEngine::TabItem* _sceneTab = nullptr;
		HexEngine::Button* _runButton = nullptr;
		HexEngine::Button* _stopButton = nullptr;
		HexEngine::Button* _savePrefabButton = nullptr;
		HexEngine::Button* _exitPrefabButton = nullptr;
		HexEngine::Element* _sceneSurface = nullptr;
		std::function<void()> _onRunGame;
		std::function<void()> _onStopGame;
		std::function<void()> _onSavePrefab;
		std::function<void()> _onExitPrefab;
		std::function<bool()> _isGameRunning;
		std::function<bool()> _isPrefabMode;
	};
}
