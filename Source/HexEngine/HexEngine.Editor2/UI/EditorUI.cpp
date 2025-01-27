
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
		g_pEnv->_inputSystem->RemoveInputListener(this);
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

	void EditorUI::OnProjectManagerCompleted(const fs::path& projectFolder, const std::string& projectName, bool didLoadExisting, const std::wstring& namespaceName, LoadingDialog* loadingDlg)
	{
		_projectManager = nullptr;

		_projectFolderPath = projectFolder;
		_projectFilePath = projectFolder / projectName;

		_projectFile = new ProjectFile(_projectFilePath, std::ios::out | std::ios::trunc);
		_projectFile->_projectName = projectName;

		if (didLoadExisting)
		{
			ProjectFile file(_projectFilePath, std::ios::in);

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

			std::shared_ptr<Scene> sceneToActivateAfterLoad;
			bool loadedGameFromIntegrator = false;

			for (auto& scene : file._scenes)
			{
				if (scene->IsSceneAttached() == false)
				{
					auto newScene = g_pEnv->_sceneManager->CreateEmptyScene(false, (EditorUI*)g_pEnv->_uiManager);

					if (sceneToActivateAfterLoad == nullptr)
					{
						sceneToActivateAfterLoad = newScene;
					}
					else
					{
						newScene->SetFlags(SceneFlags::Disabled);
					}

					g_pEnv->_sceneManager->SetActiveScene(newScene);

					scene->_scene = newScene;

					if (loadedGameFromIntegrator == false && _integrator.LoadGame() == false)
					{
						LOG_CRIT("Failed to load game");
						return;
					}

					loadedGameFromIntegrator = true;

					scene->Load(updateLoadingDialog);					

					//newScene->GetMainCamera()->SetViewport(math::Viewport(0, 0, _centralDock->GetSize().x, _centralDock->GetSize().y));
					//g_pEnv->_sceneRenderer->Resize(_centralDock->GetSize().x, _centralDock->GetSize().y);

					SceneSaveFile* saveFile = new SceneSaveFile(scene->GetAbsolutePath(), std::ios::out, newScene);

					_sceneFiles.push_back(saveFile);
				}
			}

			loadingDlg->DeleteMe();

			g_pEnv->_sceneManager->SetActiveScene(sceneToActivateAfterLoad);			

			_projectFile->_scenes = _sceneFiles;
			_entityList->RefreshList();

			
		}
		else
		{
			// We created a new project so it needs a new scene
			auto newScene = g_pEnv->_sceneManager->CreateEmptyScene(true, (EditorUI*)g_pEnv->_uiManager);

			g_pEnv->_sceneManager->SetActiveScene(newScene);

			if (auto mainCamera = newScene->CreateEntity("MainCamera"); mainCamera != nullptr)
			{
				auto cameraComponent = mainCamera->AddComponent<Camera>();
			}

			newScene->CreateDefaultSunLight();

			SceneSaveFile* sceneFile = new SceneSaveFile(_projectFolderPath / "Data/Scenes/New Scene.hscene", std::ios::out | std::ios::trunc, newScene);

			sceneFile->Save();

			_sceneFiles.push_back(sceneFile);

			_projectFile->_scenes.push_back(sceneFile);

			_projectFile->Save();

			// generate the project
			ProjectGenerationParams params;
			params.path = projectFolder / "Code";
			params.projectName = projectName;
			params.sdkPath = g_pEnv->_fileSystem->GetBaseDirectory().parent_path().parent_path().parent_path(); // this is....awful
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

		LineEditDialog* dlg = new LineEditDialog(_rootElement, Point(GetWidth() / 2 - sizeX / 2, GetHeight() / 2 - sizeY / 2), Point(sizeX, sizeY), label, std::bind(callback, this, std::placeholders::_2));
	}

	void EditorUI::CreateMenuBar()
	{
		_mainMenu = new MenuBar(_rootElement, Point(), Point(_width, 30));
		{
			MenuBar::RootItem* file = new MenuBar::RootItem;
			file->name = L"File";
			_mainMenu->AddRootItem(file);
			{
				MenuBar::Item* actionNew = new MenuBar::Item;
				actionNew->name = L"New Scene";
				actionNew->action = std::bind(&EditorUI::CreateLineEditDialog, this, L"Enter a scene name", &EditorUI::OnCreateNewSceneAction);
				_mainMenu->AddSubItem(file, actionNew);

				MenuBar::Item* actionDel = new MenuBar::Item;
				actionDel->name = L"Delete Scene";
				actionDel->action = std::bind(&EditorUI::OnDeleteSceneAction, this);
				_mainMenu->AddSubItem(file, actionDel);

				MenuBar::Item* actionSave = new MenuBar::Item;
				actionSave->name = L"Save";
				actionSave->action = std::bind(&EditorUI::OnSaveAction, this);
				_mainMenu->AddSubItem(file, actionSave);

				MenuBar::Item* actionExport = new MenuBar::Item;
				actionExport->name = L"Export";
				actionExport->action = std::bind(&EditorUI::OnExportAction, this);
				_mainMenu->AddSubItem(file, actionExport);

				MenuBar::Item* actionRun = new MenuBar::Item;
				actionRun->name = L"Run";
				actionRun->action = std::bind(&EditorUI::RunGame, this);
				_mainMenu->AddSubItem(file, actionRun);

				MenuBar::Item* actionStop = new MenuBar::Item;
				actionStop->name = L"Stop";
				actionStop->action = std::bind(&EditorUI::StopGame, this);
				_mainMenu->AddSubItem(file, actionStop);
			}
		}
		{
			MenuBar::RootItem* edit = new MenuBar::RootItem;
			edit->name = L"Edit";
			//edit->type = MenuBar::Item::Type::RootMenu;
			_mainMenu->AddRootItem(edit);
			{

			}
		}
		{
			MenuBar::RootItem* scene = new MenuBar::RootItem;
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

				MenuBar::Item* actionNewPlane = new MenuBar::Item;
				actionNewPlane->name = L"Add plane";
				actionNewPlane->action = std::bind(&EditorUI::OnAddPrimitive, this, PrimitiveType::Plane);
				_mainMenu->AddSubItem(scene, actionNewPlane);

				MenuBar::Item* actionNewCube = new MenuBar::Item;
				actionNewCube->name = L"Add cube";
				actionNewCube->action = std::bind(&EditorUI::OnAddPrimitive, this, PrimitiveType::Cube);
				_mainMenu->AddSubItem(scene, actionNewCube);

				MenuBar::Item* actionNewSphere = new MenuBar::Item;
				actionNewSphere->name = L"Add sphere";
				actionNewSphere->action = std::bind(&EditorUI::OnAddPrimitive, this, PrimitiveType::Sphere);
				_mainMenu->AddSubItem(scene, actionNewSphere);

				MenuBar::Item* actionNew = new MenuBar::Item;
				actionNew->name = L"Add point light";
				actionNew->action = std::bind(&EditorUI::OnAddLight, this);
				_mainMenu->AddSubItem(scene, actionNew);

				MenuBar::Item* actionNewSL = new MenuBar::Item;
				actionNewSL->name = L"Add spot light";
				actionNewSL->action = std::bind(&EditorUI::OnAddSpotLight, this);
				_mainMenu->AddSubItem(scene, actionNewSL);

				MenuBar::Item* actionNewBB = new MenuBar::Item;
				actionNewBB->name = L"Add billboard";
				actionNewBB->action = std::bind(&EditorUI::OnAddBillboard, this);
				_mainMenu->AddSubItem(scene, actionNewBB);

				MenuBar::Item* actionNewTerrain = new MenuBar::Item;
				actionNewTerrain->name = L"Add terrain";
				actionNewTerrain->action = std::bind(&EditorUI::OnAddPrimitive, this, PrimitiveType::Terrain);
				_mainMenu->AddSubItem(scene, actionNewTerrain);

				MenuBar::Item* actionSettings = new MenuBar::Item;
				actionSettings->name = L"Settings";
				actionSettings->action = std::bind(&EditorUI::ShowSettingsDialog, this);
				_mainMenu->AddSubItem(scene, actionSettings);
			}
		}
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
		auto scene = g_pEnv->_sceneManager->GetCurrentScene();

		g_pEnv->_sceneManager->UnloadScene(scene.get());

		_sceneFiles.erase(std::remove_if(_sceneFiles.begin(), _sceneFiles.end(), [scene](SceneSaveFile* ssf) {
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
			auto primitive = g_pEnv->_sceneManager->GetCurrentScene()->CreateEntity("Cube");
			auto meshComponent = primitive->AddComponent<StaticMeshComponent>();
			auto mesh = Mesh::Create("EngineData.Models/Primitives/cube.hmesh");

			meshComponent->SetMesh(mesh);

			auto body = primitive->AddComponent<RigidBody>();
			body->AddBoxCollider(mesh->GetAABB());

			break;
		}
		case PrimitiveType::Sphere:
		{
			auto primitive = g_pEnv->_sceneManager->GetCurrentScene()->CreateEntity("Sphere");
			auto meshComponent = primitive->AddComponent<StaticMeshComponent>();
			auto mesh = Mesh::Create("EngineData.Models/Primitives/sphere.hmesh");

			meshComponent->SetMesh(mesh);
			break;
		}
		case PrimitiveType::Plane:
		{
			auto primitive = g_pEnv->_sceneManager->GetCurrentScene()->CreateEntity("Plane");
			auto meshComponent = primitive->AddComponent<StaticMeshComponent>();
			auto mesh = Mesh::Create("EngineData.Models/Primitives/plane.hmesh");

			meshComponent->SetMesh(mesh);
			break;
		}

		case PrimitiveType::Terrain:
		{
			Terrain::CreateTerrainDialog(_rootElement, nullptr);
			break;
		}
		}
	}

	void EditorUI::OnAddBillboard()
	{
		auto billboard = g_pEnv->_sceneManager->GetCurrentScene()->CreateEntity("Bill", math::Vector3(0.0f, 10.0f, 0.0f), math::Quaternion::Identity, math::Vector3(5.0f));

		auto bb = billboard->AddComponent<Billboard>();
		bb->SetTexture(ITexture2D::Create("EngineData.Textures/particles/smoke01.png"));
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
		Entity* light = g_pEnv->_sceneManager->GetCurrentScene()->CreateEntity("PointLight");

		auto pointLight = light->AddComponent<PointLight>();
		pointLight->SetRadius(100.0f);
		pointLight->SetLightStength(4.0f);
	}

	void EditorUI::OnAddSpotLight()
	{
		Entity* light = g_pEnv->_sceneManager->GetCurrentScene()->CreateEntity("SpotLight");

		auto pointLight = light->AddComponent<SpotLight>();
		pointLight->SetLightStength(4.0f);
	}

	void EditorUI::OnCreateNewSceneAction(const std::wstring& sceneName)
	{
		auto scene = g_pEnv->_sceneManager->CreateEmptyScene(true, this);

		if (auto mainCamera = scene->CreateEntity("MainCamera"); mainCamera != nullptr)
		{
			auto cameraComponent = mainCamera->AddComponent<Camera>();
		}

		scene->CreateDefaultSunLight();
		scene->SetName(sceneName);

		_entityList->RefreshList();

		std::wstring dataLocalPath = L"Data/Scenes/" + sceneName + L".hscene";
		auto scenePath = _projectFolderPath / dataLocalPath;

		SceneSaveFile* ssf = new SceneSaveFile(scenePath, std::ios::out, scene);

		_projectFile->_scenes.push_back(ssf);
		_sceneFiles.push_back(ssf);

		//_projectManager = ProjectManager::CreateProjectManagerDialog(_rootElement, std::bind(&EditorUI::OnProjectManagerCompleted, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
	}

	void EditorUI::CreateDocks(uint32_t width, uint32_t height)
	{
		const float dockPercentage = 0.16f;

		int32_t dockWidth = (int32_t)((float)width * dockPercentage);
		int32_t lowerDockHeight = (int32_t)((float)height * 0.25f);

		auto& style = g_pEnv->_uiManager->GetRenderer()->_style;

		_centralDock = new Dock(_rootElement, Point(dockWidth, style.win_title_height + 2), Point(width - (dockWidth * 2), height - (style.win_title_height + 2) - lowerDockHeight), Dock::Anchor::Middle);

		_leftDock = new Dock(_rootElement, Point(0, style.win_title_height + 2), Point(dockWidth, height - (style.win_title_height + 2) - (lowerDockHeight)), Dock::Anchor::Left);
		_rightDock = new Inspector(_rootElement, Point(width - dockWidth, style.win_title_height + 2), Point(dockWidth, height - (style.win_title_height + 2) - (lowerDockHeight)));

		_lowerDock = new Explorer(_rootElement, Point(0, height - (lowerDockHeight)), Point(width, lowerDockHeight));
	}

	void EditorUI::CreateEntityList()
	{
		_entityList = new EntityList(_leftDock, Point(10, 10), Point(_leftDock->GetSize().x - 20, _leftDock->GetSize().y - 20));
	}

	void EditorUI::Update(float frameTime)
	{
		CheckCentralDockRoamState();

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
		auto currentScene = g_pEnv->_sceneManager->GetCurrentScene();

		if (!currentScene)
			return;

		auto camera = currentScene->GetMainCamera();

		if (!camera)
			return;

		if (_centralDock->GetRoamState() == Dock::RoamState::FreeLook)
		{
			
			auto cameraTransform = camera->GetEntity()->GetComponent<Transform>();

			if (camera)
			{
				if (_freeLookDir.Length() > 0.0f)
				{
					math::Vector3 dir = _freeLookDir * g_pEnv->_timeManager->GetFrameTime() * 200.0f;

					const auto& currentPos = cameraTransform->GetPosition();

					cameraTransform->SetPosition(currentPos + dir);
				}

				const auto& mouseStart = _centralDock->GetRoamingMouseStartPos();

				int32_t mx, my;
				g_pEnv->_inputSystem->GetMousePosition(mx, my);

				float dx, dy;
				dx = (float)mx - mouseStart.x;
				dy = (float)my - mouseStart.y;

				dx *= g_pEnv->_timeManager->GetFrameTime();
				dy *= g_pEnv->_timeManager->GetFrameTime();


				if (dx != 0)
				{
					float yaw = camera->GetYaw() - dx * 7.0f;
					camera->SetYaw(yaw);
				}

				if (dy != 0)
				{
					float pitch = camera->GetPitch() - dy * 7.0f;
					camera->SetPitch(pitch);
				}

				_centralDock->SetRoamingMouseStartPos(Point(mx, my));
			}
		}
	}

	bool EditorUI::OnInputEvent(InputEvent event, InputData* data)
	{
		if (g_pEnv->_commandManager->GetConsole()->GetActive())
			return false;

		if (UIManager::OnInputEvent(event, data) == false)
			return false;

		if (_centralDock->GetRoamState() != Dock::RoamState::FreeLook && _integrator.GetState() != GameTestState::Started)
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

		if (_centralDock->GetRoamState() == Dock::RoamState::FreeLook)
		{
			if (event == InputEvent::KeyDown)
			{
				auto camera = g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera();
				auto cameraTransform = camera->GetEntity()->GetComponent<Transform>();

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
			else if (event == InputEvent::KeyUp)
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
			return false;
		}

		
		
		if (event == InputEvent::MouseDown && data->MouseDown.button == VK_LBUTTON && _centralDock->IsMouseOver(true))
		{
			// pick entity
			auto currentScene = g_pEnv->_sceneManager->GetCurrentScene();

			if (currentScene)
			{
				auto mainCamera = currentScene->GetMainCamera();

				if (mainCamera)
				{
					int32_t mx, my;
					g_pEnv->_inputSystem->GetMousePosition(mx, my);

					const Point& centerSize = _centralDock->GetSize();
					Point centerLoc = _centralDock->GetAbsolutePosition();
					Point centerPos = centerLoc.GetCenter(centerSize);

					const auto& vp = mainCamera->GetViewport();

					//auto vpCenterX = mainCamera->GetViewport().x + mainCamera->GetViewport().width / 2;
					//auto vpCenterY = mainCamera->GetViewport().y + mainCamera->GetViewport().height / 2;

					mx -= centerLoc.x;
					//my -= centerLoc.y;

					float scaleX = vp.width / (float)centerSize.x;
					float scaleY = vp.height / (float)centerSize.y;

					float fmx = (float)mx * scaleX;
					float fmy = (float)my * scaleY;

					auto screenRay = g_pEnv->_inputSystem->GetScreenToWorldRay(mainCamera, fmx, fmy/*, _centralDock->GetSize().x, _centralDock->GetSize().y*/);

					math::Ray ray;
					ray.direction = screenRay;
					ray.position = mainCamera->GetEntity()->GetPosition();

					RayHit hit;

					if (HexEngine::PhysUtils::RayCast(
						ray,
						mainCamera->GetFarZ(),
						LAYERMASK(Layer::StaticGeometry) |
						LAYERMASK(Layer::DynamicGeometry),
						&hit)
						)
					{
						if (hit.entity != nullptr)
						{
							_rightDock->InspectEntity(hit.entity);
						}
					}
				}
			}
		}
		else if (event == InputEvent::KeyDown && _centralDock->IsMouseOver(true))
		{
			if (_integrator.GetState() == GameTestState::Started && data->KeyDown.key == VK_ESCAPE)
			{
				_integrator.StopGame();
			}
		}

		return false;
	}

	void EditorUI::OnAddEntity(Entity* entity)
	{
		_entityList->AddEntity(entity);

		//auto& name = entity->GetName();

		//_entityList->AddItem(std::wstring(name.begin(), name.end()), nullptr);

		if (entity->GetName() == "SkySphere")
		{
			g_pEnv->_sceneManager->GetCurrentScene()->SetSkySphere(entity);
		}
	}

	void EditorUI::OnRemoveEntity(Entity* entity)
	{
		_entityList->RemoveEntity(entity);

		/*auto& name = entity->GetName();

		_entityList->RemoveItem(std::wstring(name.begin(), name.end()));*/
	}

	void EditorUI::OnAddComponent(Entity* entity, BaseComponent* component)
	{

	}

	void EditorUI::OnRemoveComponent(Entity* entity, BaseComponent* component)
	{

	}
}