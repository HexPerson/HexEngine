
#include "EditorUI.hpp"
#include "../Editor.hpp"
#include "Actions\ProjectManager.hpp"
#include "Actions\ProjectGenerator.hpp"
#include "Actions\Settings.hpp"
#include "Actions\Terrain.hpp"
#include "Gadgets\ScaleGadget.hpp"
#include "Gadgets\PositionGadget.hpp"
#include "Gadgets\DuplicateGadget.hpp"

namespace HexEditor
{
	EditorUI::EditorUI()
	{
		g_pUIManager = this;

		_gadgets.push_back(new ScaleGadget);
		_gadgets.push_back(new PositionGadget);
		_gadgets.push_back(new DuplicateGadget);
	}

	EditorUI::~EditorUI()
	{		
		HexEngine::g_pEnv->_inputSystem->RemoveInputListener(this);
		g_pUIManager = nullptr;
	}

	void EditorUI::Create(uint32_t width, uint32_t height)
	{
		UIManager::Create(width, height);

		CreateDocks(width, height);
		CreateEntityList();
		CreateMenuBar();

		// Always start with the project manager
		_projectManager = ProjectManager::CreateProjectManagerDialog(_rootElement, std::bind(&EditorUI::OnProjectManagerCompleted, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
	}

	void EditorUI::OnProjectManagerCompleted(const fs::path& projectFolder, const std::string& projectName, bool didLoadExisting, const std::wstring& namespaceName, HexEngine::LoadingDialog* loadingDlg)
	{
		_projectManager = nullptr;

		_projectFolderPath = projectFolder;
		_projectFilePath = projectFolder / projectName;

		_projectFile = new HexEngine::ProjectFile(_projectFilePath, std::ios::out | std::ios::trunc);
		_projectFile->_projectName = projectName;

		if (didLoadExisting)
		{
			HexEngine::ProjectFile file(_projectFilePath, std::ios::in);

			if (!file.Load())
			{
				LOG_CRIT("Cannot open project file '%s'", _projectFilePath.string().c_str());
				return;
			}

			// Create the file system
			//
			g_pEditor->CreateFileSystem(_projectFolderPath);

			auto updateLoadingDialog = [&](const std::wstring& entityName, int32_t loaded, int32_t total)
			{
				loadingDlg->SetPercentage((float)loaded / (float)total);
				loadingDlg->SetText(std::format(L"Loading {} {:d}/{:d}", entityName, loaded, total));
			};

			std::shared_ptr<HexEngine::Scene> sceneToActivateAfterLoad;
			bool loadedGameFromIntegrator = false;

			for (auto& scene : file._scenes)
			{
				if (scene->IsSceneAttached() == false)
				{
					auto newScene = HexEngine::g_pEnv->_sceneManager->CreateEmptyScene(false, (EditorUI*)&HexEngine::g_pEnv->GetUIManager(), true);

					if (sceneToActivateAfterLoad == nullptr)
					{
						sceneToActivateAfterLoad = newScene;
					}
					else
					{
						newScene->SetFlags(HexEngine::SceneFlags::Disabled);
					}

					HexEngine::g_pEnv->_sceneManager->SetActiveScene(newScene);

					scene->_scene = newScene;

					if (loadedGameFromIntegrator == false && _integrator.LoadGame() == false)
					{
						LOG_CRIT("Failed to load game");
						return;
					}

					loadedGameFromIntegrator = true;

					scene->Load(updateLoadingDialog);	

					_entityList->Repaint();

					//newScene->GetMainCamera()->SetViewport(math::Viewport(0, 0, _centralDock->GetSize().x, _centralDock->GetSize().y));
					//g_pEnv->_sceneRenderer->Resize(_centralDock->GetSize().x, _centralDock->GetSize().y);

					HexEngine::SceneSaveFile* saveFile = new HexEngine::SceneSaveFile(scene->GetAbsolutePath(), std::ios::out, newScene);

					_sceneFiles.push_back(saveFile);
				}
			}

			loadingDlg->DeleteMe();

			HexEngine::g_pEnv->_sceneManager->SetActiveScene(sceneToActivateAfterLoad);

			_projectFile->_scenes = _sceneFiles;
			_entityList->RefreshList();

			
		}
		else
		{
			// We created a new project so it needs a new scene
			auto newScene = HexEngine::g_pEnv->_sceneManager->CreateEmptyScene(true, (EditorUI*)&HexEngine::g_pEnv->GetUIManager(), true);

			HexEngine::g_pEnv->_sceneManager->SetActiveScene(newScene);

			if (auto mainCamera = newScene->CreateEntity("MainCamera"); mainCamera != nullptr)
			{
				auto cameraComponent = mainCamera->AddComponent<HexEngine::Camera>();
			}

			newScene->CreateDefaultSunLight();

			HexEngine::SceneSaveFile* sceneFile = new HexEngine::SceneSaveFile(_projectFolderPath / "Data/Scenes/New Scene.hscene", std::ios::out | std::ios::trunc, newScene);

			sceneFile->Save();

			_sceneFiles.push_back(sceneFile);

			_projectFile->_scenes.push_back(sceneFile);

			_projectFile->Save();

			// generate the project
			ProjectGenerationParams params;
			params.path = projectFolder / "Code";
			params.projectName = projectName;
			params.sdkPath = HexEngine::g_pEnv->GetFileSystem().GetBaseDirectory().parent_path().parent_path().parent_path(); // this is....awful
			params.nameSpace = std::string(namespaceName.begin(), namespaceName.end());
			params.primaryScenePath = sceneFile->GetAbsolutePath();

			if (auto p = params.projectName.find(".json"); p != params.projectName.npos)
			{
				params.projectName = params.projectName.substr(0, p);
			}

			ProjectGenerator generator;
			if (!generator.Create(params))
			{
				LOG_CRIT("Failed to generate code");
				return;
			}

			if (_integrator.LoadGame() == false)
			{
				LOG_CRIT("Failed to load game");
				return;
			}
		}		
	}

	void EditorUI::CreateLineEditDialog(const std::wstring& label, std::function<void(EditorUI*, const std::wstring&)> callback)
	{
		const int32_t sizeX = 400;
		const int32_t sizeY = 80;

		HexEngine::LineEditDialog* dlg = new HexEngine::LineEditDialog(_rootElement, HexEngine::Point(GetWidth() / 2 - sizeX / 2, GetHeight() / 2 - sizeY / 2), HexEngine::Point(sizeX, sizeY), label, std::bind(callback, this, std::placeholders::_2));
	}

	

	void EditorUI::CreateMenuBar()
	{
		_mainMenu = new HexEngine::MenuBar(_rootElement, HexEngine::Point(), HexEngine::Point(_width, 30));
		{
			HexEngine::MenuBar::RootItem* file = new HexEngine::MenuBar::RootItem;
			file->name = L"File";
			_mainMenu->AddRootItem(file);
			{
				HexEngine::MenuBar::Item* actionNew = new HexEngine::MenuBar::Item;
				actionNew->name = L"New Scene";
				actionNew->action = std::bind(&EditorUI::CreateLineEditDialog, this, L"Enter a scene name", &EditorUI::OnCreateNewSceneAction);
				_mainMenu->AddSubItem(file, actionNew);

				HexEngine::MenuBar::Item* actionDel = new HexEngine::MenuBar::Item;
				actionDel->name = L"Delete Scene";
				actionDel->action = std::bind(&EditorUI::OnDeleteSceneAction, this);
				_mainMenu->AddSubItem(file, actionDel);

				HexEngine::MenuBar::Item* actionSave = new HexEngine::MenuBar::Item;
				actionSave->name = L"Save";
				actionSave->action = std::bind(&EditorUI::OnSaveAction, this);
				_mainMenu->AddSubItem(file, actionSave);

				HexEngine::MenuBar::Item* actionExport = new HexEngine::MenuBar::Item;
				actionExport->name = L"Export";
				actionExport->action = std::bind(&EditorUI::OnExportAction, this);
				_mainMenu->AddSubItem(file, actionExport);

				HexEngine::MenuBar::Item* actionRun = new HexEngine::MenuBar::Item;
				actionRun->name = L"Run";
				actionRun->action = std::bind(&EditorUI::RunGame, this);
				_mainMenu->AddSubItem(file, actionRun);

				HexEngine::MenuBar::Item* actionStop = new HexEngine::MenuBar::Item;
				actionStop->name = L"Stop";
				actionStop->action = std::bind(&EditorUI::StopGame, this);
				_mainMenu->AddSubItem(file, actionStop);
			}
		}
		{
			HexEngine::MenuBar::RootItem* edit = new HexEngine::MenuBar::RootItem;
			edit->name = L"Edit";
			//edit->type = MenuBar::Item::Type::RootMenu;
			_mainMenu->AddRootItem(edit);
			{
				HexEngine::MenuBar::Item* actionPaintTrees = new HexEngine::MenuBar::Item;
				actionPaintTrees->name = L"Paint Trees";
				actionPaintTrees->action = std::bind(&EditorUI::OnStartPaintTreeDialog, this);
				_mainMenu->AddSubItem(edit, actionPaintTrees);
			}
		}
		{
			HexEngine::MenuBar::RootItem* scene = new HexEngine::MenuBar::RootItem;
			scene->name = L"Scene";
			//edit->type = MenuBar::Item::Type::RootMenu;
			_mainMenu->AddRootItem(scene);
			{
				/*MenuBar::RootItem* testsub = new MenuBar::RootItem;
				testsub->name = L"TestSub";
				menu->AddSubItem(scene, testsub);

				MenuBar::Item* testsub2 = new MenuBar::Item;
				testsub2->name = L"TestSub2";
				menu->AddSubItem(testsub, testsub);*/

				HexEngine::MenuBar::Item* actionNewPlane = new HexEngine::MenuBar::Item;
				actionNewPlane->name = L"Add plane";
				actionNewPlane->action = std::bind(&EditorUI::OnAddPrimitive, this, PrimitiveType::Plane);
				_mainMenu->AddSubItem(scene, actionNewPlane);

				HexEngine::MenuBar::Item* actionNewCube = new HexEngine::MenuBar::Item;
				actionNewCube->name = L"Add cube";
				actionNewCube->action = std::bind(&EditorUI::OnAddPrimitive, this, PrimitiveType::Cube);
				_mainMenu->AddSubItem(scene, actionNewCube);

				HexEngine::MenuBar::Item* actionNewSphere = new HexEngine::MenuBar::Item;
				actionNewSphere->name = L"Add sphere";
				actionNewSphere->action = std::bind(&EditorUI::OnAddPrimitive, this, PrimitiveType::Sphere);
				_mainMenu->AddSubItem(scene, actionNewSphere);

				HexEngine::MenuBar::Item* actionNew = new HexEngine::MenuBar::Item;
				actionNew->name = L"Add point light";
				actionNew->action = std::bind(&EditorUI::OnAddLight, this);
				_mainMenu->AddSubItem(scene, actionNew);

				HexEngine::MenuBar::Item* actionNewSL = new HexEngine::MenuBar::Item;
				actionNewSL->name = L"Add spot light";
				actionNewSL->action = std::bind(&EditorUI::OnAddSpotLight, this);
				_mainMenu->AddSubItem(scene, actionNewSL);

				HexEngine::MenuBar::Item* actionNewBB = new HexEngine::MenuBar::Item;
				actionNewBB->name = L"Add billboard";
				actionNewBB->action = std::bind(&EditorUI::OnAddBillboard, this);
				_mainMenu->AddSubItem(scene, actionNewBB);

				HexEngine::MenuBar::Item* actionNewTerrain = new HexEngine::MenuBar::Item;
				actionNewTerrain->name = L"Add terrain";
				actionNewTerrain->action = std::bind(&EditorUI::OnAddPrimitive, this, PrimitiveType::Terrain);
				_mainMenu->AddSubItem(scene, actionNewTerrain);

				HexEngine::MenuBar::Item* actionNewOcean = new HexEngine::MenuBar::Item;
				actionNewOcean->name = L"Add ocean";
				actionNewOcean->action = std::bind(&EditorUI::OnAddPrimitive, this, PrimitiveType::Ocean);
				_mainMenu->AddSubItem(scene, actionNewOcean);

				HexEngine::MenuBar::Item* actionSettings = new HexEngine::MenuBar::Item;
				actionSettings->name = L"Settings";
				actionSettings->action = std::bind(&EditorUI::ShowSettingsDialog, this);
				_mainMenu->AddSubItem(scene, actionSettings);
			}
		}

		{
			HexEngine::MenuBar::RootItem* scene = new HexEngine::MenuBar::RootItem;
			scene->name = L"Debug";
			//edit->type = MenuBar::Item::Type::RootMenu;
			_mainMenu->AddRootItem(scene);
			{
				HexEngine::MenuBar::Item* debugScene = new HexEngine::MenuBar::Item;
				debugScene->name = L"Debug scene";
				debugScene->action = std::bind(
					[]() {		
						static HexEngine::HVar* r_debugScene = HexEngine::g_pEnv->_commandManager->FindHVar("r_debugScene");
						r_debugScene->_val.b = !r_debugScene->_val.b;
					});
				_mainMenu->AddSubItem(scene, debugScene);

				HexEngine::MenuBar::Item* debugPhysics = new HexEngine::MenuBar::Item;
				debugPhysics->name = L"Debug physics";
				debugPhysics->action = std::bind(
					[]() {
						static HexEngine::HVar* phys_debug = HexEngine::g_pEnv->_commandManager->FindHVar("phys_debug");
						phys_debug->_val.b = !phys_debug->_val.b;
					});	
				_mainMenu->AddSubItem(scene, debugPhysics);

				HexEngine::MenuBar::Item* profileAlbedoOnly = new HexEngine::MenuBar::Item;
				profileAlbedoOnly->name = L"Albedo only";
				profileAlbedoOnly->action = std::bind(
					[]() {
						static HexEngine::HVar* r_profileAlbedoOnly = HexEngine::g_pEnv->_commandManager->FindHVar("r_profileAlbedoOnly");
						r_profileAlbedoOnly->_val.b = !r_profileAlbedoOnly->_val.b;
					});
				_mainMenu->AddSubItem(scene, profileAlbedoOnly);

				HexEngine::MenuBar::Item* profileDisableDirectionalLights = new HexEngine::MenuBar::Item;
				profileDisableDirectionalLights->name = L"Disable directional lights";
				profileDisableDirectionalLights->action = std::bind(
					[]() {
						static HexEngine::HVar* r_profileDisableDirectionalLights = HexEngine::g_pEnv->_commandManager->FindHVar("r_profileDisableDirectionalLights");
						r_profileDisableDirectionalLights->_val.b = !r_profileDisableDirectionalLights->_val.b;
					});
				_mainMenu->AddSubItem(scene, profileDisableDirectionalLights);

				HexEngine::MenuBar::Item* profileDisablePointLights = new HexEngine::MenuBar::Item;
				profileDisablePointLights->name = L"Disable point lights";
				profileDisablePointLights->action = std::bind(
					[]() {
						static HexEngine::HVar* r_profileDisablePointLights = HexEngine::g_pEnv->_commandManager->FindHVar("r_profileDisablePointLights");
						r_profileDisablePointLights->_val.b = !r_profileDisablePointLights->_val.b;
					});
				_mainMenu->AddSubItem(scene, profileDisablePointLights);

				HexEngine::MenuBar::Item* profileDisableSpotLights = new HexEngine::MenuBar::Item;
				profileDisableSpotLights->name = L"Disable spot lights";
				profileDisableSpotLights->action = std::bind(
					[]() {
						static HexEngine::HVar* r_profileDisableSpotLights = HexEngine::g_pEnv->_commandManager->FindHVar("r_profileDisableSpotLights");
						r_profileDisableSpotLights->_val.b = !r_profileDisableSpotLights->_val.b;
					});
				_mainMenu->AddSubItem(scene, profileDisableSpotLights);

				HexEngine::MenuBar::Item* profileDisablePost = new HexEngine::MenuBar::Item;
				profileDisablePost->name = L"Disable post-processing";
				profileDisablePost->action = std::bind(
					[]() {
						static HexEngine::HVar* r_profileDisablePost = HexEngine::g_pEnv->_commandManager->FindHVar("r_profileDisablePost");
						r_profileDisablePost->_val.b = !r_profileDisablePost->_val.b;
					});
				_mainMenu->AddSubItem(scene, profileDisablePost);

				HexEngine::MenuBar::Item* profileDisableBloom = new HexEngine::MenuBar::Item;
				profileDisableBloom->name = L"Disable bloom";
				profileDisableBloom->action = std::bind(
					[]() {
						static HexEngine::HVar* r_profileDisableBloom = HexEngine::g_pEnv->_commandManager->FindHVar("r_profileDisableBloom");
						r_profileDisableBloom->_val.b = !r_profileDisableBloom->_val.b;
					});
				_mainMenu->AddSubItem(scene, profileDisableBloom);


				/*HexEngine::MenuBar::Item* profileDepthOnly = new HexEngine::MenuBar::Item;
				profileDepthOnly->name = L"Depth only";
				profileDepthOnly->action = std::bind(
					[]() {
						static HexEngine::HVar* r_profileDepthOnly = HexEngine::g_pEnv->_commandManager->FindHVar("r_profileDepthOnly");
						r_profileDepthOnly->_val.b = !r_profileDepthOnly->_val.b;
					});
				_mainMenu->AddSubItem(scene, profileDepthOnly);*/
			}
		}
	}

	void EditorUI::OnStartPaintTreeDialog()
	{
		const int32_t dlgWidth = 400;
		const int32_t dlgHeight = 500;

		HexEngine::Dialog* dlg = new HexEngine::Dialog(HexEngine::g_pEnv->GetUIManager().GetRootElement(), HexEngine::Point::GetScreenCenterWithOffset(-dlgWidth / 2, -dlgHeight / 2), HexEngine::Point(dlgWidth, dlgHeight), L"Vegetation Paint Tool");

		//LineEdit* meshInput = 
	}

	void EditorUI::RunGame()
	{
		_integrator.RunGame();
	}

	void EditorUI::StopGame()
	{
		_integrator.StopGame();
	}

	void EditorUI::OnDeleteSceneAction()
	{
		auto scene = HexEngine::g_pEnv->_sceneManager->GetCurrentScene();

		HexEngine::g_pEnv->_sceneManager->UnloadScene(scene.get());

		_sceneFiles.erase(std::remove_if(_sceneFiles.begin(), _sceneFiles.end(), [scene](HexEngine::SceneSaveFile* ssf) {
			return ssf->GetScene() == scene;
			}));
	}

	void EditorUI::ShowSettingsDialog()
	{
		auto settingsDlg = Settings::CreateSettingsDialog(_rootElement, nullptr);
	}

	void EditorUI::OnAddPrimitive(PrimitiveType type)
	{
		switch (type)
		{
		case PrimitiveType::Cube:
		{
			auto primitive = HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->CreateEntity("Cube");
			auto meshComponent = primitive->AddComponent<HexEngine::StaticMeshComponent>();
			auto mesh = HexEngine::Mesh::Create("EngineData.Models/Primitives/cube.hmesh");

			meshComponent->SetMesh(mesh);

			auto body = primitive->AddComponent<HexEngine::RigidBody>();
			body->AddBoxCollider(mesh->GetAABB());

			break;
		}
		case PrimitiveType::Sphere:
		{
			auto primitive = HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->CreateEntity("Sphere");
			auto meshComponent = primitive->AddComponent<HexEngine::StaticMeshComponent>();
			auto mesh = HexEngine::Mesh::Create("EngineData.Models/Primitives/sphere.hmesh");

			meshComponent->SetMesh(mesh);
			break;
		}
		case PrimitiveType::Plane:
		{
			auto primitive = HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->CreateEntity("Plane");
			auto meshComponent = primitive->AddComponent<HexEngine::StaticMeshComponent>();
			auto mesh = HexEngine::Mesh::Create("EngineData.Models/Primitives/plane.hmesh");

			meshComponent->SetMesh(mesh);
			break;
		}

		case PrimitiveType::Terrain:
		{
			Terrain::CreateTerrainDialog(_rootElement, nullptr);
			break;
		}

		case PrimitiveType::Ocean:
		{
			Terrain::CreateOceanDialog(_rootElement, nullptr);
			break;
		}
		}
	}

	void EditorUI::OnAddBillboard()
	{
		auto billboard = HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->CreateEntity("Bill", math::Vector3(0.0f, 10.0f, 0.0f), math::Quaternion::Identity, math::Vector3(5.0f));

		auto bb = billboard->AddComponent<HexEngine::Billboard>();
		bb->SetTexture(HexEngine::ITexture2D::Create("EngineData.Textures/particles/smoke01.png"));
		//bb->SetTexture(ITexture2D::Create("Textures/test.png"));
	}

	void EditorUI::OnSaveAction()
	{
		// save the project file
		if (_projectFile)
			_projectFile->Save();

		// then save all the scenes
		for (auto& ssf : _sceneFiles)
		{
			ssf->Save();
		}
	}

	void EditorUI::OnExportAction()
	{
		if (_projectFile)
			_projectFile->Save();
		/*auto fileName = _projectPath;

		HexEngine::SceneSaveFile file(fileName, std::ios::out | std::ios::binary);

		if (!file.Save())
		{
			LOG_CRIT("Failed to save file: %S", fileName.c_str());
		}

		file.Close();*/
	}

	void EditorUI::OnAddLight()
	{
		HexEngine::Entity* light = HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->CreateEntity("PointLight");

		auto pointLight = light->AddComponent<HexEngine::PointLight>();
		pointLight->SetRadius(100.0f);
		pointLight->SetLightStength(4.0f);
	}

	void EditorUI::OnAddSpotLight()
	{
		HexEngine::Entity* light = HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->CreateEntity("SpotLight");

		auto pointLight = light->AddComponent<HexEngine::SpotLight>();
		pointLight->SetLightStength(4.0f);
	}

	void EditorUI::OnCreateNewSceneAction(const std::wstring& sceneName)
	{
		auto scene = HexEngine::g_pEnv->_sceneManager->CreateEmptyScene(true, this, true);

		if (auto mainCamera = scene->CreateEntity("MainCamera"); mainCamera != nullptr)
		{
			auto cameraComponent = mainCamera->AddComponent<HexEngine::Camera>();
		}

		scene->CreateDefaultSunLight();
		scene->SetName(sceneName);

		_entityList->RefreshList();

		std::wstring dataLocalPath = L"Data/Scenes/" + sceneName + L".hscene";
		auto scenePath = _projectFolderPath / dataLocalPath;

		HexEngine::SceneSaveFile* ssf = new HexEngine::SceneSaveFile(scenePath, std::ios::out, scene);

		_projectFile->_scenes.push_back(ssf);
		_sceneFiles.push_back(ssf);

		//_projectManager = ProjectManager::CreateProjectManagerDialog(_rootElement, std::bind(&EditorUI::OnProjectManagerCompleted, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
	}

	void EditorUI::CreateDocks(uint32_t width, uint32_t height)
	{
		const float dockPercentage = 0.16f;

		int32_t dockWidth = (int32_t)((float)width * dockPercentage);
		int32_t lowerDockHeight = (int32_t)((float)height * 0.25f);

		auto& style = HexEngine::g_pEnv->GetUIManager().GetRenderer()->_style;

		_sceneView = new SceneView(
			_rootElement,
			HexEngine::Point(dockWidth, style.win_title_height + 2),
			HexEngine::Point(width - (dockWidth * 2), height - (style.win_title_height + 2) - lowerDockHeight),
			[this]() { RunGame(); },
			[this]() { StopGame(); },
			[this]() { return _integrator.GetState() == GameTestState::Started; });

		_leftDock = new HexEngine::Dock(_rootElement, HexEngine::Point(0, style.win_title_height + 2), HexEngine::Point(dockWidth, height - (style.win_title_height + 2) - (lowerDockHeight)), HexEngine::Dock::Anchor::Left);
		_rightDock = new Inspector(_rootElement, HexEngine::Point(width - dockWidth, style.win_title_height + 2), HexEngine::Point(dockWidth, height - (style.win_title_height + 2) - (lowerDockHeight)));

		_lowerDock = new Explorer(_rootElement, HexEngine::Point(0, height - (lowerDockHeight)), HexEngine::Point(width, lowerDockHeight));

		const auto sceneViewportPos = _sceneView->GetSceneViewportAbsolutePosition();
		const auto sceneViewportSize = _sceneView->GetSceneViewportSize();

		HexEngine::g_pEnv->_inputSystem->SetInputViewport(
			sceneViewportPos.x,
			sceneViewportPos.y,
			sceneViewportSize.x,
			sceneViewportSize.y);
	}

	void EditorUI::CreateEntityList()
	{
		_entityList = new EntityList(_leftDock, HexEngine::Point(10, 10), HexEngine::Point(_leftDock->GetSize().x - 20, _leftDock->GetSize().y - 20));
	}

	void EditorUI::Update(float frameTime)
	{
		CheckCentralDockRoamState();
		_integrator.Update();

		if (_mainMenu->IsOpen())
		{
			_entityList->EnableInput(false);
		}
		else
		{
			_entityList->EnableInput(true);
		}

		for (auto& gadget : _gadgets)
		{
			if (gadget->IsStarted())
			{
				gadget->Update();
			}
		}
	}

	void EditorUI::Render()
	{
		UIManager::Render();		

		//int32_t mx, my;
		//g_pEnv->_inputSystem->GetMousePosition(mx, my);

		//g_pEnv->_uiManager->GetRenderer()->FillQuad(mx, my, 10, 10, math::Color(1, 0, 0, 1));
	}

	void EditorUI::CheckCentralDockRoamState()
	{
		auto currentScene = HexEngine::g_pEnv->_sceneManager->GetCurrentScene();

		if (!currentScene)
			return;

		auto camera = currentScene->GetMainCamera();

		if (!camera)
			return;

		if (_sceneView->GetRoamState() == SceneView::RoamState::FreeLook)
		{
			
			auto cameraTransform = camera->GetEntity()->GetComponent<HexEngine::Transform>();

			if (camera)
			{
				if (_freeLookDir.Length() > 0.0f)
				{
					math::Vector3 dir = _freeLookDir * HexEngine::g_pEnv->_timeManager->GetFrameTime() * 200.0f * _freeLookMultiplier;

					const auto& currentPos = cameraTransform->GetPosition();

					cameraTransform->SetPosition(currentPos + dir);
				}

				const auto& mouseStart = _sceneView->GetRoamingMouseStartPos();

				int32_t mx, my;
				HexEngine::g_pEnv->_inputSystem->GetMousePosition(mx, my);

				float dx, dy;
				dx = (float)mx - mouseStart.x;
				dy = (float)my - mouseStart.y;

				dx *= HexEngine::g_pEnv->_timeManager->GetFrameTime();
				dy *= HexEngine::g_pEnv->_timeManager->GetFrameTime();


				if (dx != 0)
				{
					float yaw = camera->GetYaw() - dx * 9.0f;
					camera->SetYaw(yaw);
				}

				if (dy != 0)
				{
					float pitch = camera->GetPitch() - dy * 9.0f;
					camera->SetPitch(pitch);
				}

				_sceneView->SetRoamingMouseStartPos(HexEngine::Point(mx, my));
			}
		}
	}

	bool EditorUI::OnInputEvent(HexEngine::InputEvent event, HexEngine::InputData* data)
	{
		if (HexEngine::g_pEnv->_commandManager->GetConsole()->GetActive())
			return false;

		if (UIManager::OnInputEvent(event, data) == false)
			return false;

		if (_sceneView->GetRoamState() != SceneView::RoamState::FreeLook && _integrator.GetState() != GameTestState::Started)
		{
			bool anyGadgetRunning = false;
			bool keyhandled = false;

			for (auto& gadget : _gadgets)
			{
				keyhandled |= gadget->OnInputEvent(event, data);

				if (gadget->IsStarted())
					anyGadgetRunning = true;
			}

			if (anyGadgetRunning || keyhandled)
				return false;
		}

		if (_sceneView->GetRoamState() == SceneView::RoamState::FreeLook)
		{
			if (event == HexEngine::InputEvent::KeyDown)
			{
				auto camera = HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera();
				auto cameraTransform = camera->GetEntity()->GetComponent<HexEngine::Transform>();

				switch (data->KeyDown.key)
				{
				case 'W': _freeLookDir = cameraTransform->GetForward(); break;
				case 'S': _freeLookDir = -cameraTransform->GetForward(); break;
				case 'A': _freeLookDir = -cameraTransform->GetRight(); break;
				case 'D': _freeLookDir = cameraTransform->GetRight(); break;
				default: return false;
				}



				return false;
			}
			else if (event == HexEngine::InputEvent::KeyUp)
			{
				switch (data->KeyDown.key)
				{
				case 'W':
				case 'S':
				case 'A':
				case 'D':
					_freeLookDir = math::Vector3::Zero;
					break;
				}
			}
			else if (event == HexEngine::InputEvent::MouseWheel)
			{
				if (data->MouseWheel.delta > 0)
					_freeLookMultiplier += 1.0f;
				else if (data->MouseWheel.delta < 0)
					_freeLookMultiplier -= 1.0f;
			}
			return false;
		}

		
		
		if (event == HexEngine::InputEvent::MouseDown && data->MouseDown.button == VK_LBUTTON && _sceneView->IsMouseOverSceneViewport())
		{
			if (auto inspecting = _rightDock->GetInspectingEntity(); inspecting != nullptr && inspecting->IsEditorGizmoHovered())
			{
				return false;
			}

			auto hit = RayCastWorld();

			if (hit.entity)
			{
				_rightDock->InspectEntity(hit.entity);
			}
		}
		else if (event == HexEngine::InputEvent::KeyDown && _sceneView->IsMouseOverSceneViewport())
		{
			if (_integrator.GetState() == GameTestState::Started && data->KeyDown.key == VK_ESCAPE)
			{
				_integrator.StopGame();
			}
		}

		return false;
	}

	HexEngine::RayHit EditorUI::RayCastWorld(const std::vector<HexEngine::Entity*>& entsToIgnore)
	{
		// pick entity
		auto currentScene = HexEngine::g_pEnv->_sceneManager->GetCurrentScene();

		if (currentScene)
		{
			auto mainCamera = currentScene->GetMainCamera();

			if (mainCamera)
			{
				int32_t mx, my;
				HexEngine::g_pEnv->_inputSystem->GetMousePosition(mx, my);

				const HexEngine::Point centerSize = _sceneView->GetSceneViewportSize();
				HexEngine::Point centerLoc = _sceneView->GetSceneViewportAbsolutePosition();
				HexEngine::Point centerPos = centerLoc.GetCenter(centerSize);

				const auto& vp = mainCamera->GetViewport();

				//auto vpCenterX = mainCamera->GetViewport().x + mainCamera->GetViewport().width / 2;
				//auto vpCenterY = mainCamera->GetViewport().y + mainCamera->GetViewport().height / 2;

				mx -= centerLoc.x;
				//my -= centerLoc.y;

				float scaleX = vp.width / (float)centerSize.x;
				float scaleY = vp.height / (float)centerSize.y;

				float fmx = (float)mx * scaleX;
				float fmy = (float)my * scaleY;

				auto screenRay = HexEngine::g_pEnv->_inputSystem->GetScreenToWorldRay(mainCamera, fmx, fmy/*, _centralDock->GetSize().x, _centralDock->GetSize().y*/);

				math::Ray ray;
				ray.direction = screenRay;
				ray.position = mainCamera->GetEntity()->GetPosition();

				HexEngine::RayHit hit;

				if (HexEngine::PhysUtils::RayCast(
					ray,
					mainCamera->GetFarZ(),
					LAYERMASK(HexEngine::Layer::StaticGeometry) |
					LAYERMASK(HexEngine::Layer::DynamicGeometry),
					&hit,
					entsToIgnore)
					)
				{
					return hit;
				}
			}
		}

		return {};
	
	}

	void EditorUI::OnAddEntity(HexEngine::Entity* entity)
	{
		_entityList->AddEntity(entity);

		//auto& name = entity->GetName();

		//_entityList->AddItem(std::wstring(name.begin(), name.end()), nullptr);

		if (entity->GetName() == "SkySphere")
		{
			HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->SetSkySphere(entity);
		}
	}

	void EditorUI::OnRemoveEntity(HexEngine::Entity* entity)
	{
		_entityList->RemoveEntity(entity);

		/*auto& name = entity->GetName();

		_entityList->RemoveItem(std::wstring(name.begin(), name.end()));*/
	}

	void EditorUI::OnAddComponent(HexEngine::Entity* entity, HexEngine::BaseComponent* component)
	{

	}

	void EditorUI::OnRemoveComponent(HexEngine::Entity* entity, HexEngine::BaseComponent* component)
	{

	}
}
