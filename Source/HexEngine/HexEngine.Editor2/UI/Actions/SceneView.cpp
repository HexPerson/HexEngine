
#include "SceneView.hpp"
#include "../EditorUI.hpp"
#include <algorithm>

namespace HexEditor
{
	namespace
	{
		constexpr int32_t kUtilityBarHeight = 28;

		class SceneSurface : public HexEngine::Element
		{
		public:
			SceneSurface(HexEngine::Element* parent, const HexEngine::Point& position, const HexEngine::Point& size) :
				HexEngine::Element(parent, position, size)
			{
			}

			virtual void Render(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h) override
			{
				if (auto scene = HexEngine::g_pEnv->_sceneManager->GetCurrentScene(); scene != nullptr)
				{
					if (auto mainCamera = scene->GetMainCamera(); mainCamera != nullptr)
					{
						const auto absPos = GetAbsolutePosition();

						renderer->FillTexturedQuad(mainCamera->GetRenderTarget(),
							absPos.x, absPos.y,
							_size.x, _size.y,
							math::Color(1, 1, 1, 1));
					}
				}
			}
		};
	}

	SceneView::SceneView(Element* parent, const HexEngine::Point& position, const HexEngine::Point& size) :
		Element(parent, position, size)
	{
		InitializeUi();
	}

	SceneView::SceneView(
		Element* parent,
		const HexEngine::Point& position,
		const HexEngine::Point& size,
		const std::function<void()>& onRunGame,
		const std::function<void()>& onStopGame,
		const std::function<bool()>& isGameRunning) :
		Element(parent, position, size),
		_onRunGame(onRunGame),
		_onStopGame(onStopGame),
		_isGameRunning(isGameRunning)
	{
		InitializeUi();
	}

	void SceneView::InitializeUi()
	{
		_tabView = new HexEngine::TabView(
			this,
			HexEngine::Point(0, kUtilityBarHeight),
			HexEngine::Point(_size.x, _size.y - kUtilityBarHeight));

		_sceneTab = _tabView->AddTab(L"Scene");

		_tabView->AddTab(L"Mesh Inspector");
		_tabView->AddTab(L"Shader Graph");

		const int32_t tabHeaderHeight = HexEngine::g_pEnv->GetUIManager().GetRenderer()->_style.tab_height;
		_sceneSurface = new SceneSurface(
			_sceneTab,
			HexEngine::Point(0, tabHeaderHeight),
			HexEngine::Point(_tabView->GetSize().x, max(1, _tabView->GetSize().y - tabHeaderHeight)));

		_runButton = new HexEngine::Button(
			this,
			HexEngine::Point(8, 4),
			HexEngine::Point(70, kUtilityBarHeight - 8),
			L"Run",
			[this](HexEngine::Button*) {
				if (_onRunGame)
					_onRunGame();
				return true;
			});

		_stopButton = new HexEngine::Button(
			this,
			HexEngine::Point(84, 4),
			HexEngine::Point(70, kUtilityBarHeight - 8),
			L"Stop",
			[this](HexEngine::Button*) {
				if (_onStopGame)
					_onStopGame();
				return true;
			});
	}

	bool SceneView::OnInputEvent(HexEngine::InputEvent event, HexEngine::InputData* data)
	{
		if (event == HexEngine::InputEvent::MouseDown && IsMouseOverSceneViewport())
		{
			_mouseActionStartPos.x = data->MouseDown.xpos;
			_mouseActionStartPos.y = data->MouseDown.ypos;

			switch (data->MouseDown.button)
			{
			case VK_RBUTTON:
				_roamState = RoamState::FreeLook;
				break;
			}
			return false;
		}
		else if (event == HexEngine::InputEvent::MouseUp)
		{
			_roamState = RoamState::None;

			if (data->MouseUp.button == VK_LBUTTON && _dragAndDropEntity != nullptr)
			{
				g_pUIManager->GetInspector()->InspectEntity(_dragAndDropEntity);
				_dragAndDropEntity = nullptr;
			}
			return true;
		}

		// handle resource drag & dropping

		if (event == HexEngine::InputEvent::MouseMove && IsMouseOverSceneViewport())
		{
			if (auto draggingAsset = g_pUIManager->GetExplorer()->GetCurrentlyDraggedAsset(); draggingAsset != nullptr && draggingAsset->path.extension() == ".hmesh")
			{
				auto hit = g_pUIManager->RayCastWorld({ _dragAndDropEntity });

				if (hit.entity != nullptr)
				{
					if (_dragAndDropEntity == nullptr)
					{
						_dragAndDropEntity = HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->CreateEntity(
							ws2s(draggingAsset->assetNameShort),
							hit.position,
							math::Quaternion(),
							math::Vector3(1.0f)
						);

						auto staticMesh = _dragAndDropEntity->AddComponent<HexEngine::StaticMeshComponent>();

						staticMesh->SetMesh(HexEngine::Mesh::Create(draggingAsset->path));
					}
					else
					{
						_dragAndDropEntity->SetPosition(hit.position);
					}

				}
				
			}
		}

		return Element::OnInputEvent(event, data);
	}

	void SceneView::Render(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		const auto absPos = GetAbsolutePosition();
		renderer->FillQuad(absPos.x, absPos.y, _size.x, kUtilityBarHeight, renderer->_style.win_title_colour1);
		renderer->Frame(absPos.x, absPos.y, _size.x, kUtilityBarHeight, 1, renderer->_style.win_border);

		const bool gameRunning = _isGameRunning ? _isGameRunning() : false;
		const std::wstring stateLabel = gameRunning ? L"Running" : L"Stopped";
		renderer->PrintText(
			renderer->_style.font.get(),
			(uint8_t)HexEngine::Style::FontSize::Small,
			absPos.x + _size.x - 10,
			absPos.y + (kUtilityBarHeight / 2),
			gameRunning ? math::Color(0.4f, 1.0f, 0.4f, 1.0f) : renderer->_style.text_regular,
			HexEngine::FontAlign::Right | HexEngine::FontAlign::CentreUD,
			stateLabel);

		if (_runButton != nullptr && _stopButton != nullptr)
		{
			_runButton->EnableInput(!gameRunning);
			_stopButton->EnableInput(gameRunning);
		}

		if (!IsSceneTabActive())
			return;
	}

	HexEngine::TabItem* SceneView::AddWorkspaceTab(const std::wstring& label)
	{
		if (!_tabView)
			return nullptr;

		return _tabView->AddTab(label);
	}

	bool SceneView::IsSceneTabActive() const
	{
		return _tabView && _tabView->GetCurrentTabIndex() == 0;
	}

	HexEngine::Point SceneView::GetSceneSurfaceOffset() const
	{
		const int32_t tabHeaderHeight = HexEngine::g_pEnv->GetUIManager().GetRenderer()->_style.tab_height;
		return HexEngine::Point(0, kUtilityBarHeight + tabHeaderHeight);
	}

	HexEngine::Point SceneView::GetSceneViewportAbsolutePosition() const
	{
		if (_sceneSurface != nullptr)
			return _sceneSurface->GetAbsolutePosition();

		return GetAbsolutePosition() + GetSceneSurfaceOffset();
	}

	HexEngine::Point SceneView::GetSceneViewportSize() const
	{
		if (_sceneSurface != nullptr)
			return _sceneSurface->GetSize();

		const int32_t tabHeaderHeight = HexEngine::g_pEnv->GetUIManager().GetRenderer()->_style.tab_height;
		const int32_t viewportHeight = max(1, _size.y - kUtilityBarHeight - tabHeaderHeight);
		return HexEngine::Point(_size.x, viewportHeight);
	}

	bool SceneView::IsMouseOverSceneViewport() const
	{
		if (!IsSceneTabActive())
			return false;

		return IsMouseOver(GetSceneViewportAbsolutePosition(), GetSceneViewportSize());
	}
}
