
#include "SceneView.hpp"
#include "../EditorUI.hpp"
#include <algorithm>
#include <unordered_set>

namespace HexEditor
{
	namespace
	{
		constexpr int32_t kUtilityBarHeight = 28;
		constexpr uint32_t kBoxColliderMaxFaces = 100;
		constexpr uint32_t kSphereColliderMaxFaces = 600;

		enum class AutoColliderType
		{
			Box,
			Sphere,
			TriangleMesh
		};

		AutoColliderType GetAutoColliderType(const HexEngine::Mesh& mesh)
		{
			uint32_t faceCount = mesh.GetNumFaces();
			if (faceCount == 0)
			{
				faceCount = static_cast<uint32_t>(mesh.GetNumIndices() / 3);
			}

			if (faceCount <= kBoxColliderMaxFaces)
				return AutoColliderType::Box;

			//if (faceCount <= kSphereColliderMaxFaces)
			//	return AutoColliderType::Sphere;

			return AutoColliderType::TriangleMesh;
		}

		/*float GetSphereColliderRadius(const dx::BoundingBox& aabb)
		{
			const auto& extents = aabb.Extents;
			const float radius = std::max({ extents.x, extents.y, extents.z });
			return std::max(radius, 0.01f);
		}*/

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

		std::vector<HexEngine::Entity*> CollectPrefabRoots(const std::vector<HexEngine::Entity*>& spawnedEntities)
		{
			std::vector<HexEngine::Entity*> roots;
			if (spawnedEntities.empty())
				return roots;

			std::unordered_set<HexEngine::Entity*> spawnedSet;
			spawnedSet.reserve(spawnedEntities.size());
			for (auto* entity : spawnedEntities)
			{
				if (entity != nullptr)
					spawnedSet.insert(entity);
			}

			for (auto* entity : spawnedEntities)
			{
				if (entity == nullptr)
					continue;

				auto* parent = entity->GetParent();
				if (parent == nullptr || spawnedSet.find(parent) == spawnedSet.end())
				{
					roots.push_back(entity);
				}
			}

			if (roots.empty() && !spawnedEntities.empty())
			{
				roots.push_back(spawnedEntities.front());
			}

			return roots;
		}
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
		const std::function<bool()>& isGameRunning,
		const std::function<void()>& onSavePrefab,
		const std::function<void()>& onExitPrefab,
		const std::function<bool()>& isPrefabMode) :
		Element(parent, position, size),
		_onRunGame(onRunGame),
		_onStopGame(onStopGame),
		_onSavePrefab(onSavePrefab),
		_onExitPrefab(onExitPrefab),
		_isGameRunning(isGameRunning),
		_isPrefabMode(isPrefabMode)
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
			HexEngine::Point(_tabView->GetSize().x, std::max(1, _tabView->GetSize().y - tabHeaderHeight)));

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
		_runButton->SetIcon(HexEngine::ITexture2D::Create("EngineData.Textures/UI/play.png"));

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
		_stopButton->SetIcon(HexEngine::ITexture2D::Create("EngineData.Textures/UI/stop.png"));

		_savePrefabButton = new HexEngine::Button(
			this,
			HexEngine::Point(166, 4),
			HexEngine::Point(100, kUtilityBarHeight - 8),
			L"Save Prefab",
			[this](HexEngine::Button*) {
				if (_onSavePrefab)
					_onSavePrefab();
				return true;
			});

		_exitPrefabButton = new HexEngine::Button(
			this,
			HexEngine::Point(272, 4),
			HexEngine::Point(100, kUtilityBarHeight - 8),
			L"Exit Prefab",
			[this](HexEngine::Button*) {
				if (_onExitPrefab)
					_onExitPrefab();
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

			if (data->MouseUp.button == VK_LBUTTON)
			{
				if (_dragAndDropEntity != nullptr)
				{
					if (_dragAndDropPrefabRoots.empty())
					{
						g_pUIManager->RecordEntityCreated(_dragAndDropEntity);
					}
					else
					{
						for (auto* rootEntity : _dragAndDropPrefabRoots)
						{
							if (rootEntity != nullptr)
								g_pUIManager->RecordEntityCreated(rootEntity);
						}
					}

					g_pUIManager->GetInspector()->InspectEntity(_dragAndDropEntity);
					_dragAndDropEntity = nullptr;
					_dragAndDropPrefabRoots.clear();
					_dragAndDropPrefabRootOffsets.clear();
					// AssetExplorer publishes a deferred "recently dropped" path on mouse-up.
					// Ignore that one-shot token since this drag/drop has already been finalized.
					_ignoreNextConsumedDroppedAsset = true;
					return true;
				}

				if (!IsMouseOverSceneViewport())
					return true;

				fs::path droppedAssetPath;
				bool usingLiveDraggedAsset = false;
				if (auto draggingAsset = g_pUIManager->GetExplorer()->GetCurrentlyDraggedAsset(); draggingAsset != nullptr)
				{
					droppedAssetPath = draggingAsset->path;
					usingLiveDraggedAsset = true;
				}
				else
				{
					g_pUIManager->GetExplorer()->ConsumeRecentlyDroppedAssetPath(droppedAssetPath);
					if (!droppedAssetPath.empty() && _ignoreNextConsumedDroppedAsset)
					{
						droppedAssetPath.clear();
						_ignoreNextConsumedDroppedAsset = false;
					}
				}

				if (usingLiveDraggedAsset)
				{
					_ignoreNextConsumedDroppedAsset = true;
				}

				if (!droppedAssetPath.empty())
				{
					if (droppedAssetPath.extension() == ".hmat")
					{
						auto hit = g_pUIManager->RayCastWorld();

						if (hit.entity != nullptr)
						{
							if (auto smc = hit.entity->GetComponent<HexEngine::StaticMeshComponent>(); smc != nullptr)
							{
								const fs::path previousMaterialPath = smc->GetMaterial() ? smc->GetMaterial()->GetFileSystemPath() : fs::path();
								auto newMaterial = HexEngine::Material::Create(droppedAssetPath);
								if (newMaterial != nullptr)
								{
									const fs::path newMaterialPath = newMaterial->GetFileSystemPath();
									smc->SetMaterial(newMaterial);
									g_pUIManager->RecordStaticMeshMaterialChange(hit.entity, previousMaterialPath, newMaterialPath);
								}
							}
						}
					}
					else if (droppedAssetPath.extension() == ".hprefab")
					{
						auto hit = g_pUIManager->RayCastWorld();
						if (hit.entity != nullptr)
						{
							auto currentScene = HexEngine::g_pEnv->_sceneManager->GetCurrentScene();
							if (currentScene != nullptr)
							{
								auto spawnedEntities = HexEngine::g_pEnv->_sceneManager->LoadPrefab(currentScene, droppedAssetPath);
								auto rootEntities = CollectPrefabRoots(spawnedEntities);
								if (!rootEntities.empty())
								{
									const auto anchorStart = rootEntities.front()->GetPosition();
									const auto placementDelta = hit.position - anchorStart;

									for (auto* rootEntity : rootEntities)
									{
										if (rootEntity != nullptr)
										{
											rootEntity->SetPosition(rootEntity->GetPosition() + placementDelta);
											g_pUIManager->RecordEntityCreated(rootEntity);
										}
									}

									g_pUIManager->GetInspector()->InspectEntity(rootEntities.front());
									return true;
								}
							}
						}
					}
				}
			}
			return true;
		}

		// handle resource drag & dropping

		if (event == HexEngine::InputEvent::MouseMove && IsMouseOverSceneViewport())
		{
			if (auto draggingAsset = g_pUIManager->GetExplorer()->GetCurrentlyDraggedAsset(); draggingAsset != nullptr)
			{
				if (draggingAsset->path.extension() == ".hmesh")
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
							auto rigidBody = _dragAndDropEntity->AddComponent<HexEngine::RigidBody>();
							auto mesh = HexEngine::Mesh::Create(draggingAsset->path);

							staticMesh->SetMesh(mesh);

							if (mesh != nullptr)
							{
								switch (GetAutoColliderType(*mesh))
								{
								case AutoColliderType::Box:
									rigidBody->AddBoxCollider(mesh->GetAABB());
									break;
								//case AutoColliderType::Sphere:
								//	rigidBody->AddSphereCollider(GetSphereColliderRadius(mesh->GetAABB()));
								//	break;
								case AutoColliderType::TriangleMesh:
									rigidBody->AddTriangleMeshCollider(mesh.get(), true);
									break;
								}
							}
						}
						else
						{
							_dragAndDropEntity->SetPosition(hit.position);
						}
					}
				}
				else if (draggingAsset->path.extension() == ".hprefab")
				{
					std::vector<HexEngine::Entity*> entsToIgnore = _dragAndDropPrefabRoots;
					if (entsToIgnore.empty() && _dragAndDropEntity != nullptr)
						entsToIgnore.push_back(_dragAndDropEntity);

					auto hit = g_pUIManager->RayCastWorld(entsToIgnore);
					if (hit.entity != nullptr)
					{
						if (_dragAndDropEntity == nullptr)
						{
							auto currentScene = HexEngine::g_pEnv->_sceneManager->GetCurrentScene();
							if (currentScene == nullptr)
								return true;

							auto spawnedEntities = HexEngine::g_pEnv->_sceneManager->LoadPrefab(currentScene, draggingAsset->path);
							_dragAndDropPrefabRoots = CollectPrefabRoots(spawnedEntities);
							_dragAndDropPrefabRootOffsets.clear();

							if (_dragAndDropPrefabRoots.empty())
								return true;

							_dragAndDropEntity = _dragAndDropPrefabRoots.front();

							const auto anchorStart = _dragAndDropEntity->GetPosition();
							_dragAndDropPrefabRootOffsets.reserve(_dragAndDropPrefabRoots.size());
							for (auto* rootEntity : _dragAndDropPrefabRoots)
							{
								if (rootEntity != nullptr)
									_dragAndDropPrefabRootOffsets.push_back(rootEntity->GetPosition() - anchorStart);
								else
									_dragAndDropPrefabRootOffsets.push_back(math::Vector3::Zero);
							}
						}

						if (_dragAndDropEntity != nullptr)
						{
							for (size_t i = 0; i < _dragAndDropPrefabRoots.size(); ++i)
							{
								auto* rootEntity = _dragAndDropPrefabRoots[i];
								if (rootEntity == nullptr)
									continue;

								const auto targetPosition = hit.position + _dragAndDropPrefabRootOffsets[i];
								rootEntity->SetPosition(targetPosition);
							}
						}
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
		const bool prefabMode = _isPrefabMode ? _isPrefabMode() : false;
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
			_runButton->EnableInput(!gameRunning && !prefabMode);
			_stopButton->EnableInput(gameRunning && !prefabMode);
		}

		if (_savePrefabButton != nullptr && _exitPrefabButton != nullptr)
		{
			_savePrefabButton->EnableInput(prefabMode);
			_exitPrefabButton->EnableInput(prefabMode);
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
		const int32_t viewportHeight = std::max(1, _size.y - kUtilityBarHeight - tabHeaderHeight);
		return HexEngine::Point(_size.x, viewportHeight);
	}

	bool SceneView::IsMouseOverSceneViewport() const
	{
		if (!IsSceneTabActive())
			return false;

		return IsMouseOver(GetSceneViewportAbsolutePosition(), GetSceneViewportSize());
	}
}
