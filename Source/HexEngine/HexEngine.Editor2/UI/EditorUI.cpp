
#include "EditorUI.hpp"
#include "../Editor.hpp"
#include "Actions\ProjectManager.hpp"
#include "Actions\ProjectGenerator.hpp"
#include "Actions\Settings.hpp"
#include "Actions\Terrain.hpp"
#include "Actions\NavMeshTool.hpp"
#include <HexEngine.Core\FileSystem\SceneSaveFile.hpp>

#include "Gadgets\ScaleGadget.hpp"
#include "Gadgets\PositionGadget.hpp"
#include "Gadgets\DuplicateGadget.hpp"

namespace HexEditor
{
	EditorUI::EditorUI()
	{
		g_pUIManager = this;

		HexEngine::Transform::SetEditorTranslateCommitCallback(
			[](HexEngine::Entity* entity, const math::Vector3& before, const math::Vector3& after)
			{
				if (g_pUIManager != nullptr)
				{
					g_pUIManager->RecordEntityPositionChange(entity, before, after);
				}
			});

		_gadgets.push_back(new ScaleGadget);
		_gadgets.push_back(new PositionGadget);
		_gadgets.push_back(new DuplicateGadget);
	}

	EditorUI::~EditorUI()
	{		
		HexEngine::Transform::SetEditorTranslateCommitCallback({});
		HexEngine::g_pEnv->_inputSystem->RemoveInputListener(this);
		g_pUIManager = nullptr;
	}

	void EditorUI::Create(uint32_t width, uint32_t height)
	{
		UIManager::Create(width, height);

		CreateDocks(width, height);
		CreateEntityList();
		CreateMenuBar();
		NotifyEditorToolPluginsCreateUI();
		_prefabController.SetDependencies(this, &_integrator, _rightDock, _entityList, _lowerDock);

		// Always start with the project manager
		_projectManager = ProjectManager::CreateProjectManagerDialog(_rootElement, std::bind(&EditorUI::OnProjectManagerCompleted, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
	}

	void EditorUI::OnProjectManagerCompleted(const fs::path& projectFolder, const std::string& projectName, bool didLoadExisting, const std::wstring& namespaceName, HexEngine::LoadingDialog* loadingDlg)
	{
		_projectManager = nullptr;
		_transactions.Clear();

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

					HexEngine::g_pEnv->_debugGui->EnableProfiling(true);

					//newScene->GetMainCamera()->SetViewport(math::Viewport(0, 0, _centralDock->GetSize().x, _centralDock->GetSize().y));
					//g_pEnv->_sceneRenderer->Resize(_centralDock->GetSize().x, _centralDock->GetSize().y);

					HexEngine::SceneSaveFile* saveFile = new HexEngine::SceneSaveFile(scene->GetAbsolutePath(), std::ios::out, newScene);

					_sceneFiles.push_back(saveFile);
				}
			}

			loadingDlg->DeleteMe();

			HexEngine::g_pEnv->_sceneManager->SetActiveScene(sceneToActivateAfterLoad);

			_projectFile->_scenes = _sceneFiles;
			_entityListRefreshPending = true;

			
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

				HexEngine::MenuBar::Item* actionSavePrefab = new HexEngine::MenuBar::Item;
				actionSavePrefab->name = L"Save Prefab Stage";
				actionSavePrefab->action = std::bind(&EditorUI::SavePrefabStage, this);
				_mainMenu->AddSubItem(file, actionSavePrefab);

				HexEngine::MenuBar::Item* actionExitPrefab = new HexEngine::MenuBar::Item;
				actionExitPrefab->name = L"Exit Prefab Stage";
				actionExitPrefab->action = std::bind(&EditorUI::ClosePrefabStage, this, true);
				_mainMenu->AddSubItem(file, actionExitPrefab);

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
				HexEngine::MenuBar::Item* actionUndo = new HexEngine::MenuBar::Item;
				actionUndo->name = L"Undo (Ctrl+Z)";
				actionUndo->action = std::bind(&EditorUI::UndoLastTransaction, this);
				_mainMenu->AddSubItem(edit, actionUndo);

				HexEngine::MenuBar::Item* actionRedo = new HexEngine::MenuBar::Item;
				actionRedo->name = L"Redo (Ctrl+Y)";
				actionRedo->action = std::bind(&EditorUI::RedoLastTransaction, this);
				_mainMenu->AddSubItem(edit, actionRedo);

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

				HexEngine::MenuBar::Item* actionNewEmpty = new HexEngine::MenuBar::Item;
				actionNewEmpty->name = L"Add empty entity";
				actionNewEmpty->action = std::bind(&EditorUI::OnAddEmptyEntity, this);
				_mainMenu->AddSubItem(scene, actionNewEmpty);

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

				HexEngine::MenuBar::Item* actionNavMesh = new HexEngine::MenuBar::Item;
				actionNavMesh->name = L"Navigation Mesh...";
				actionNavMesh->action = std::bind(&EditorUI::ShowNavMeshDialog, this);
				_mainMenu->AddSubItem(scene, actionNavMesh);

				HexEngine::MenuBar::Item* actionGenerateHlod = new HexEngine::MenuBar::Item;
				actionGenerateHlod->name = L"Generate HLOD (Scaffold)";
				actionGenerateHlod->action = std::bind(&EditorUI::OnGenerateHLOD, this);
				_mainMenu->AddSubItem(scene, actionGenerateHlod);

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

	void EditorUI::NotifyEditorToolPluginsCreateUI()
	{
		if (_mainMenu == nullptr || HexEngine::g_pEnv == nullptr || HexEngine::g_pEnv->_pluginSystem == nullptr)
			return;

		const auto& plugins = HexEngine::g_pEnv->_pluginSystem->GetAllPlugins();
		for (const auto& plugin : plugins)
		{
			if (plugin.iface == nullptr)
				continue;

			if (auto* editorTool = plugin.iface->GetEditorToolPlugin(); editorTool != nullptr)
			{
				editorTool->OnCreateUI(_mainMenu);
			}
		}
	}

	void EditorUI::BroadcastEditorToolMessage(HexEngine::Message& message)
	{
		if (HexEngine::g_pEnv == nullptr || HexEngine::g_pEnv->_pluginSystem == nullptr)
			return;

		const auto& plugins = HexEngine::g_pEnv->_pluginSystem->GetAllPlugins();
		for (const auto& plugin : plugins)
		{
			if (plugin.iface == nullptr)
				continue;

			if (auto* editorTool = plugin.iface->GetEditorToolPlugin(); editorTool != nullptr)
			{
				editorTool->OnMessage(&message, nullptr);
			}
		}
	}

	void EditorUI::RunGame()
	{
		if (IsPrefabStageActive())
		{
			LOG_WARN("Cannot run game while prefab stage is active. Exit prefab mode first.");
			return;
		}

		_integrator.RunGame();
	}

	void EditorUI::StopGame()
	{
		_integrator.StopGame();
	}

	bool EditorUI::IsPrefabInstanceEntity(HexEngine::Entity* entity) const
	{
		return _prefabController.IsPrefabInstanceEntity(entity);
	}

	bool EditorUI::IsPrefabInstanceRootEntity(HexEngine::Entity* entity) const
	{
		return _prefabController.IsPrefabInstanceRootEntity(entity);
	}

	bool EditorUI::HasPrefabInstanceOverrides(HexEngine::Entity* entity) const
	{
		return _prefabController.HasPrefabInstanceOverrides(entity);
	}

	bool EditorUI::GetPrefabInstancePropertyOverrides(HexEngine::Entity* entity, std::vector<PrefabController::PrefabPropertyOverride>& outOverrides) const
	{
		return _prefabController.GetPrefabInstancePropertyOverrides(entity, outOverrides);
	}

	bool EditorUI::RevertPrefabInstancePropertyOverride(HexEngine::Entity* entity, const std::string& componentName, const std::string& propertyPath)
	{
		return _prefabController.RevertPrefabInstancePropertyOverride(entity, componentName, propertyPath);
	}

	bool EditorUI::RevertPrefabInstanceComponentOverrides(HexEngine::Entity* entity, const std::string& componentName)
	{
		return _prefabController.RevertPrefabInstanceComponentOverrides(entity, componentName);
	}

	bool EditorUI::ApplySelectedPrefabInstanceOverridesToAsset(HexEngine::Entity* entity, const std::vector<PrefabController::PrefabPropertyOverride>& selectedOverrides)
	{
		return _prefabController.ApplySelectedPrefabInstanceOverridesToAsset(entity, selectedOverrides);
	}

	HexEngine::Entity* EditorUI::RevertPrefabInstance(HexEngine::Entity* entity)
	{
		return _prefabController.RevertPrefabInstance(entity);
	}

	bool EditorUI::ApplyPrefabInstanceToPrefabAsset(HexEngine::Entity* entity)
	{
		return _prefabController.ApplyPrefabInstanceToPrefabAsset(entity);
	}

	bool EditorUI::IsVariantStageEntity(HexEngine::Entity* entity) const
	{
		return _prefabController.IsVariantStageEntity(entity);
	}

	bool EditorUI::GetVariantStageEntityOverrideComponents(HexEngine::Entity* entity, std::unordered_set<std::string>& outComponentNames) const
	{
		return _prefabController.GetVariantStageEntityOverrideComponents(entity, outComponentNames);
	}

	bool EditorUI::RevertVariantStageComponentToBase(HexEngine::Entity* entity, const std::string& componentName)
	{
		return _prefabController.RevertVariantStageComponentToBase(entity, componentName);
	}

	bool EditorUI::OpenPrefabStage(const fs::path& prefabPath)
	{
		return _prefabController.OpenPrefabStage(prefabPath);
	}

	bool EditorUI::SavePrefabStage()
	{
		return _prefabController.SavePrefabStage();
	}

	bool EditorUI::ClosePrefabStage(bool saveChanges)
	{
		return _prefabController.ClosePrefabStage(saveChanges);
	}

	bool EditorUI::IsPrefabStageActive() const
	{
		return _prefabController.IsPrefabStageActive();
	}

	void EditorUI::OnDeleteSceneAction()
	{
		if (IsPrefabStageActive())
		{
			LOG_WARN("Cannot delete scene while prefab stage is active. Exit prefab mode first.");
			return;
		}

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

	void EditorUI::ShowNavMeshDialog()
	{
		auto* navMeshDialog = NavMeshTool::CreateEditorDialog(_rootElement);
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

			RecordEntityCreated(primitive);

			break;
		}
		case PrimitiveType::Sphere:
		{
			auto primitive = HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->CreateEntity("Sphere");
			auto meshComponent = primitive->AddComponent<HexEngine::StaticMeshComponent>();
			auto mesh = HexEngine::Mesh::Create("EngineData.Models/Primitives/sphere.hmesh");

			meshComponent->SetMesh(mesh);
			RecordEntityCreated(primitive);
			break;
		}
		case PrimitiveType::Plane:
		{
			auto primitive = HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->CreateEntity("Plane");
			auto meshComponent = primitive->AddComponent<HexEngine::StaticMeshComponent>();
			auto mesh = HexEngine::Mesh::Create("EngineData.Models/Primitives/plane.hmesh");

			meshComponent->SetMesh(mesh);
			RecordEntityCreated(primitive);
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

		RecordEntityCreated(billboard);
	}

	void EditorUI::OnGenerateHLOD()
	{
		auto* currentScene = HexEngine::g_pEnv->_sceneManager->GetCurrentScene().get();
		if (currentScene == nullptr)
			return;

		if (_projectFolderPath.empty())
		{
			LOG_WARN("Cannot generate HLOD because no project folder is active");
			return;
		}

		std::vector<HexEngine::StaticMeshComponent*> staticMeshes;
		currentScene->GetComponents(staticMeshes);
		if (staticMeshes.empty())
		{
			LOG_INFO("HLOD generation skipped: scene has no static meshes");
			return;
		}

		struct ClusterData
		{
			std::vector<HexEngine::StaticMeshComponent*> meshes;
		};

		const float clusterSize = 600.0f;
		std::unordered_map<std::string, ClusterData> clusters;
		clusters.reserve(staticMeshes.size());

		for (auto* smc : staticMeshes)
		{
			if (smc == nullptr || smc->GetMesh() == nullptr)
				continue;

			auto* entity = smc->GetEntity();
			if (entity == nullptr || entity->HasFlag(HexEngine::EntityFlags::DoNotRender))
				continue;

			if (entity->HasFlag(HexEngine::EntityFlags::ExcludeFromHLOD))
			{
				LOG_INFO("Ignoring entity '%s' for HLOD generation", smc->GetEntity()->GetName().c_str());
				continue;
			}

			const auto& pos = entity->GetPosition();
			const int32_t cx = static_cast<int32_t>(std::floor(pos.x / clusterSize));
			const int32_t cy = static_cast<int32_t>(std::floor(pos.y / clusterSize));
			const int32_t cz = static_cast<int32_t>(std::floor(pos.z / clusterSize));

			const std::string key = std::to_string(cx) + "_" + std::to_string(cy) + "_" + std::to_string(cz);
			clusters[key].meshes.push_back(smc);
		}

		fs::path hlodOutputDir = _projectFolderPath / "Data/HLOD";
		std::error_code mkErr;
		fs::create_directories(hlodOutputDir, mkErr);
		if (mkErr)
		{
			LOG_WARN("Failed to create HLOD output directory '%s': %s", hlodOutputDir.string().c_str(), mkErr.message().c_str());
			return;
		}

		HexEngine::FileSystem* gameFs = HexEngine::g_pEnv->GetResourceSystem().FindFileSystemByName(L"GameData");
		if (gameFs == nullptr)
		{
			LOG_WARN("Cannot generate HLOD because GameData filesystem is unavailable");
			return;
		}

		int32_t generatedClusters = 0;
		int32_t clusterIndex = 0;

		for (const auto& [clusterKey, cluster] : clusters)
		{
			if (cluster.meshes.size() < 2)
				continue;

			std::unordered_map<std::string, std::vector<HexEngine::StaticMeshComponent*>> materialGroups;
			materialGroups.reserve(cluster.meshes.size());

			for (auto* smc : cluster.meshes)
			{
				if (smc == nullptr || smc->GetMesh() == nullptr)
					continue;

				auto material = smc->GetMaterial();
				std::string matKey = "__default";
				if (material != nullptr)
				{
					auto fsPath = material->GetFileSystemPath();
					if (!fsPath.empty())
						matKey = fsPath.string();
				}

				materialGroups[matKey].push_back(smc);
			}

			int32_t materialGroupIndex = 0;
			for (const auto& [materialKey, groupMeshes] : materialGroups)
			{
				if (groupMeshes.empty())
					continue;

				std::vector<HexEngine::MeshVertex> combinedVertices;
				std::vector<HexEngine::MeshIndexFormat> combinedIndices;
				combinedVertices.reserve(8192);
				combinedIndices.reserve(16384);

				std::shared_ptr<HexEngine::Material> groupMaterial;
				math::Vector3 accumulatedWorldPos = math::Vector3::Zero;
				uint32_t accumulatedWorldPosCount = 0;

				for (auto* smc : groupMeshes)
				{
					auto mesh = smc->GetMesh();
					if (!mesh || mesh->HasAnimations())
						continue;

					if (!groupMaterial && smc->GetMaterial())
					{
						groupMaterial = smc->GetMaterial();
					}

					const auto& sourceVertices = mesh->GetVertices();
					const auto& sourceIndices = mesh->GetIndices();
					if (sourceVertices.empty() || sourceIndices.empty())
						continue;

					const auto worldTM = smc->GetEntity()->GetWorldTM();
					const auto indexOffset = static_cast<HexEngine::MeshIndexFormat>(combinedVertices.size());

					for (auto vertex : sourceVertices)
					{
						vertex._position = math::Vector4::Transform(vertex._position, worldTM);
						vertex._normal = math::Vector3::TransformNormal(vertex._normal, worldTM);
						vertex._tangent = math::Vector3::TransformNormal(vertex._tangent, worldTM);
						vertex._bitangent = math::Vector3::TransformNormal(vertex._bitangent, worldTM);
						vertex._normal.Normalize();
						vertex._tangent.Normalize();
						vertex._bitangent.Normalize();

						accumulatedWorldPos += math::Vector3(vertex._position.x, vertex._position.y, vertex._position.z);
						accumulatedWorldPosCount++;

						combinedVertices.push_back(vertex);
					}

					for (const auto index : sourceIndices)
					{
						combinedIndices.push_back(index + indexOffset);
					}

					LOG_DEBUG("Mesh '%s' was added to HLOD_%d_%d with material '%s'", smc->GetMesh()->GetName().c_str(), clusterIndex, materialGroupIndex, groupMaterial->GetName().c_str());
				}

				if (combinedVertices.empty() || combinedIndices.empty() || accumulatedWorldPosCount == 0)
					continue;

				const math::Vector3 clusterCenter = accumulatedWorldPos / static_cast<float>(accumulatedWorldPosCount);
				for (auto& vertex : combinedVertices)
				{
					vertex._position.x -= clusterCenter.x;
					vertex._position.y -= clusterCenter.y;
					vertex._position.z -= clusterCenter.z;
				}

				fs::path outputPath = hlodOutputDir / ("HLOD_" + std::to_string(clusterIndex) + "_" + std::to_string(materialGroupIndex++) + ".hmesh");
				if (fs::exists(outputPath))
					fs::remove(outputPath);

				/*{
					for (int32_t i = 1; i < 1024; ++i)
					{
						fs::path candidate = hlodOutputDir / ("HLOD_" + std::to_string(clusterIndex) + "_" + std::to_string(materialGroupIndex) + "_" + std::to_string(i) + ".hmesh");
						if (!fs::exists(candidate))
						{
							outputPath = candidate;
							break;
						}
					}
				}*/

				auto combinedMesh = std::shared_ptr<HexEngine::Mesh>(new HexEngine::Mesh(nullptr, outputPath.stem().string()), HexEngine::ResourceDeleter());
				combinedMesh->SetPaths(outputPath, gameFs);
				combinedMesh->SetLoader(HexEngine::g_pEnv->_meshLoader);
				combinedMesh->SetNumFaces(static_cast<uint32_t>(combinedIndices.size() / 3));
				combinedMesh->AddVertices(combinedVertices);
				combinedMesh->AddIndices(combinedIndices);

				dx::BoundingBox aabb;
				dx::BoundingBox::CreateFromPoints(
					aabb,
					static_cast<size_t>(combinedVertices.size()),
					reinterpret_cast<const math::Vector3*>(combinedVertices.data()),
					sizeof(HexEngine::MeshVertex));
				combinedMesh->SetAABB(aabb);

				dx::BoundingOrientedBox obb;
				dx::BoundingOrientedBox::CreateFromBoundingBox(obb, aabb);
				combinedMesh->SetOBB(obb);

				std::shared_ptr<HexEngine::Material> hlodMaterial = groupMaterial
					? std::make_shared<HexEngine::Material>(*groupMaterial)
					: HexEngine::Material::GetDefaultMaterial();

				if (hlodMaterial && !hlodMaterial->GetShadowMapShader())
				{
					auto fallbackShadowShader = HexEngine::IShader::Create("EngineData.Shaders/ShadowMapGeometry.hcs");
					if (!fallbackShadowShader)
						fallbackShadowShader = hlodMaterial->GetStandardShader();

					if (fallbackShadowShader)
						hlodMaterial->SetShadowMapShader(fallbackShadowShader);
				}

				combinedMesh->SetMaterial(hlodMaterial ? hlodMaterial : HexEngine::Material::GetDefaultMaterial());

				combinedMesh->Save();

				auto loadedHlodMesh = HexEngine::Mesh::Create(outputPath);
				if (!loadedHlodMesh)
				{
					LOG_WARN("Failed to load generated HLOD mesh '%s'", outputPath.string().c_str());
					continue;
				}

				auto* hlodEntity = currentScene->CreateEntity("HLOD_" + clusterKey + "_" + std::to_string(materialGroupIndex), clusterCenter);
				auto* hlodSmc = hlodEntity->AddComponent<HexEngine::StaticMeshComponent>();
				hlodSmc->SetMesh(loadedHlodMesh);
				hlodSmc->SetMaterial(hlodMaterial ? hlodMaterial : HexEngine::Material::GetDefaultMaterial());

				generatedClusters++;
			}

			clusterIndex++;
		}

		LOG_INFO("HLOD scaffold generation complete. Created %d cluster mesh(es).", generatedClusters);
	}

	void EditorUI::OnSaveAction()
	{
		if (IsPrefabStageActive())
		{
			SavePrefabStage();
		}

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
		auto hit = RayCastWorld({}, false);

		HexEngine::Entity* light = HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->CreateEntity("PointLight", hit.position);

		auto pointLight = light->AddComponent<HexEngine::PointLight>();
		pointLight->SetRadius(100.0f);
		pointLight->SetLightStength(4.0f);

		RecordEntityCreated(light);

		GetInspector()->InspectEntity(light);
	}

	void EditorUI::OnAddEmptyEntity()
	{
		auto currentScene = HexEngine::g_pEnv->_sceneManager->GetCurrentScene();
		if (!currentScene)
			return;

		auto* entity = currentScene->CreateEntity("Entity");
		if (entity == nullptr)
			return;

		RecordEntityCreated(entity);

		if (_entityList != nullptr)
		{
			_entityList->RefreshList();
		}

		if (_rightDock != nullptr)
		{
			_rightDock->InspectEntity(entity);
		}
	}

	void EditorUI::OnAddSpotLight()
	{
		auto hit = RayCastWorld({}, false);

		HexEngine::Entity* light = HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->CreateEntity("SpotLight", hit.position);

		auto pointLight = light->AddComponent<HexEngine::SpotLight>();
		pointLight->SetLightStength(4.0f);

		RecordEntityCreated(light);

		GetInspector()->InspectEntity(light);
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
			[this]() { return _integrator.GetState() == GameTestState::Started; },
			[this]() { SavePrefabStage(); },
			[this]() { ClosePrefabStage(true); },
			[this]() { return IsPrefabStageActive(); });

		_leftDock = new HexEngine::Element(_rootElement, HexEngine::Point(0, style.win_title_height + 2), HexEngine::Point(dockWidth, height - (style.win_title_height + 2) - (lowerDockHeight)));
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
		constexpr int32_t margin = 10;
		constexpr int32_t searchHeight = 24;
		constexpr int32_t spacing = 6;

		_entitySearch = new HexEngine::LineEdit(
			_leftDock,
			HexEngine::Point(margin, margin),
			HexEngine::Point(_leftDock->GetSize().x - margin * 2, searchHeight),
			L"");
		_entitySearch->SetUneditableText(L"Search ");
		_entitySearch->SetDoesCallbackWaitForReturn(false);
		_entitySearch->SetIcon(HexEngine::ITexture2D::Create("EngineData.Textures/UI/magnifying_glass.png"), math::Color(HEX_RGBA_TO_FLOAT4(140, 140, 140, 255)));

		_entityList = new EntityList(
			_leftDock,
			HexEngine::Point(margin, margin + searchHeight + spacing),
			HexEngine::Point(_leftDock->GetSize().x - margin * 2, _leftDock->GetSize().y - (margin * 2 + searchHeight + spacing)));

		_entitySearch->SetOnInputFn(
			[this](HexEngine::LineEdit*, const std::wstring& value)
			{
				if (_entityList != nullptr)
				{
					_entityList->SetFilterText(value);
				}
			});
	}

	void EditorUI::Update(float frameTime)
	{
		CheckCentralDockRoamState();
		_integrator.Update();

		if (_entityList != nullptr && _entityListRefreshPending.exchange(false))
		{
			_entityList->RefreshList();
		}

		if (g_pEditor != nullptr)
		{
			std::vector<fs::path> changedPrefabs;
			g_pEditor->ConsumePendingPrefabReloads(changedPrefabs);
			for (const auto& prefabPath : changedPrefabs)
			{
				_prefabController.RefreshPrefabInstancesFromAsset(prefabPath);
			}
		}

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
		uint32_t width, height;
		HexEngine::g_pEnv->GetScreenSize(width, height);

		GetRenderer()->FillQuad(0, 0, width, height, math::Color(HEX_RGBA_TO_FLOAT4(44, 44, 45, 255)));

		UIManager::Render();
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
					math::Vector3 dir = _freeLookDir * HexEngine::g_pEnv->_timeManager->GetFrameTime() * 20.0f * _freeLookMultiplier;

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

		if (event == HexEngine::InputEvent::KeyDown &&
			HexEngine::g_pEnv->_inputSystem->IsCtrlDown() &&
			data->KeyDown.key == 'F' &&
			_entitySearch != nullptr)
		{
			SetInputFocus(_entitySearch);
			return false;
		}

		TryBeginPendingComponentEdit(event, data);

		const bool uiHandled = UIManager::OnInputEvent(event, data) == false;

		TryCommitPendingComponentEdit(event, data);

		auto* focusedElement = g_pUIManager->GetInputFocus();
		const bool isTypingInLineEdit = dynamic_cast<HexEngine::LineEdit*>(focusedElement) != nullptr;

		// Keep gadget hotkey state in sync even when focused UI widgets consume input.
		if (event == HexEngine::InputEvent::KeyUp)
		{
			for (auto& gadget : _gadgets)
			{
				gadget->OnInputEvent(event, data);
			}
		}

		// Let gadget hotkeys (e.g. Ctrl+D duplicate) still work even when another
		// focused widget reports the key event as handled, but never while typing.
		if (_sceneView->GetRoamState() != SceneView::RoamState::FreeLook &&
			_integrator.GetState() != GameTestState::Started &&
			!isTypingInLineEdit &&
			event == HexEngine::InputEvent::KeyDown &&
			HexEngine::g_pEnv->_inputSystem->IsCtrlDown() &&
			data->KeyDown.key == 'D')
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

		if (uiHandled)
			return false;

		if (event == HexEngine::InputEvent::KeyDown && HexEngine::g_pEnv->_inputSystem->IsCtrlDown())
		{
			if (data->KeyDown.key == 'Z')
			{
				UndoLastTransaction();
				return false;
			}
			else if (data->KeyDown.key == 'Y')
			{
				RedoLastTransaction();
				return false;
			}
		}

		if (_sceneView->GetRoamState() != SceneView::RoamState::FreeLook && _integrator.GetState() != GameTestState::Started && !isTypingInLineEdit)
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

	void EditorUI::TryBeginPendingComponentEdit(HexEngine::InputEvent event, HexEngine::InputData* data)
	{
		if (_pendingComponentEditActive || _rightDock == nullptr)
			return;

		auto* inspectingEntity = _rightDock->GetInspectingEntity();
		if (inspectingEntity == nullptr)
			return;

		bool beginCapture = false;
		PendingComponentEditSource source = PendingComponentEditSource::None;

		if (event == HexEngine::InputEvent::MouseDown && data->MouseDown.button == VK_LBUTTON && _rightDock->IsMouseOver(true))
		{
			beginCapture = true;
			source = PendingComponentEditSource::Mouse;
		}
		else if (event == HexEngine::InputEvent::Char && data->Char.ch != VK_RETURN && IsFocusedElementWithinInspector())
		{
			beginCapture = true;
			source = PendingComponentEditSource::Keyboard;
		}

		if (!beginCapture)
			return;

		Detail::EntityComponentStateSnapshot beforeSnapshot;
		if (!Detail::CaptureEntityComponentState(inspectingEntity, beforeSnapshot))
			return;

		_pendingComponentEditBefore = std::move(beforeSnapshot);
		_pendingComponentEditActive = true;
		_pendingComponentEditSource = source;
	}

	void EditorUI::TryCommitPendingComponentEdit(HexEngine::InputEvent event, HexEngine::InputData* data)
	{
		if (!_pendingComponentEditActive)
			return;

		bool shouldCommit = false;
		bool shouldCancel = false;
		switch (_pendingComponentEditSource)
		{
		case PendingComponentEditSource::Mouse:
			shouldCommit = (event == HexEngine::InputEvent::MouseUp && data->MouseUp.button == VK_LBUTTON);
			break;
		case PendingComponentEditSource::Keyboard:
			shouldCommit = (event == HexEngine::InputEvent::Char && data->Char.ch == VK_RETURN);
			shouldCancel = (event == HexEngine::InputEvent::MouseDown && data->MouseDown.button == VK_LBUTTON);
			break;
		default:
			break;
		}

		if (shouldCancel)
		{
			_pendingComponentEditActive = false;
			_pendingComponentEditSource = PendingComponentEditSource::None;
			_pendingComponentEditBefore = {};
			return;
		}

		if (!shouldCommit)
			return;

		auto* entity = Detail::ResolveEntityByName(_pendingComponentEditBefore.sceneName, _pendingComponentEditBefore.entityName);
		if (entity != nullptr && !entity->IsPendingDeletion())
		{
			Detail::EntityComponentStateSnapshot afterSnapshot;
			if (Detail::CaptureEntityComponentState(entity, afterSnapshot))
			{
				RecordComponentPropertyStateChange(entity, _pendingComponentEditBefore, afterSnapshot);
			}
		}

		_pendingComponentEditActive = false;
		_pendingComponentEditSource = PendingComponentEditSource::None;
		_pendingComponentEditBefore = {};
	}

	bool EditorUI::IsFocusedElementWithinInspector() const
	{
		auto* focused = FindFocusedElement(_rootElement);
		return IsDescendantOf(focused, _rightDock);
	}

	HexEngine::Element* EditorUI::FindFocusedElement(HexEngine::Element* root)
	{
		if (root == nullptr)
			return nullptr;

		if (root->IsInputFocus())
			return root;

		for (auto* child : root->GetChildren())
		{
			if (auto* focused = FindFocusedElement(child); focused != nullptr)
				return focused;
		}

		return nullptr;
	}

	bool EditorUI::IsDescendantOf(const HexEngine::Element* element, const HexEngine::Element* ancestor)
	{
		if (element == nullptr || ancestor == nullptr)
			return false;

		for (auto* current = element; current != nullptr; current = current->GetParent())
		{
			if (current == ancestor)
				return true;
		}

		return false;
	}

	bool EditorUI::HasMatchingComponentLayout(const json& before, const json& after)
	{
		if (!before.is_array() || !after.is_array() || before.size() != after.size())
			return false;

		for (size_t i = 0; i < before.size(); ++i)
		{
			const auto beforeName = before[i].value("name", std::string());
			const auto afterName = after[i].value("name", std::string());
			if (beforeName != afterName)
				return false;
		}

		return true;
	}

	HexEngine::RayHit EditorUI::RayCastWorld(const std::vector<HexEngine::Entity*>& entsToIgnore, bool useMousePos)
	{
		// pick entity
		auto currentScene = HexEngine::g_pEnv->_sceneManager->GetCurrentScene();

		if (currentScene)
		{
			auto mainCamera = currentScene->GetMainCamera();

			if (mainCamera)
			{
				int32_t mx, my;
				const HexEngine::Point centerSize = _sceneView->GetSceneViewportSize();
				HexEngine::Point centerLoc = _sceneView->GetSceneViewportAbsolutePosition();
				HexEngine::Point centerPos = centerLoc.GetCenter(centerSize);
				const auto& vp = mainCamera->GetViewport();

				if (useMousePos)
				{
					HexEngine::g_pEnv->_inputSystem->GetMousePosition(mx, my);
					mx -= centerLoc.x;
					my -= centerLoc.y;
				}
				else
				{
					mx = centerPos.x - centerLoc.x;
					my = centerPos.y - centerLoc.y;
				}

				float scaleX = vp.width / (float)centerSize.x;
				float scaleY = vp.height / (float)centerSize.y;

				float fmx = (float)mx * scaleX;
				float fmy = (float)my * scaleY;

				auto screenRay = HexEngine::g_pEnv->_inputSystem->GetScreenToWorldRay(mainCamera, fmx, fmy/*, _centralDock->GetSize().x, _centralDock->GetSize().y*/);

				math::Ray ray;
				ray.direction = screenRay;
				ray.position = mainCamera->GetEntity()->GetPosition();

				HexEngine::RayHit hit;

				hit.position = ray.position + ray.direction * mainCamera->GetFarZ() * 0.25f;

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
		_entityListRefreshPending = true;

		//auto& name = entity->GetName();

		//_entityList->AddItem(std::wstring(name.begin(), name.end()), nullptr);

		if (entity->GetName() == "SkySphere")
		{
			HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->SetSkySphere(entity);
		}
	}

	void EditorUI::OnRemoveEntity(HexEngine::Entity* entity)
	{
		_entityListRefreshPending = true;

		if (_rightDock != nullptr && _rightDock->GetInspectingEntity() == entity)
		{
			_rightDock->InspectEntity(nullptr);
		}

		/*auto& name = entity->GetName();

		_entityList->RemoveItem(std::wstring(name.begin(), name.end()));*/
	}

	void EditorUI::OnAddComponent(HexEngine::Entity* entity, HexEngine::BaseComponent* component)
	{

	}

	void EditorUI::OnRemoveComponent(HexEngine::Entity* entity, HexEngine::BaseComponent* component)
	{

	}

	void EditorUI::OnMessage(HexEngine::Message* message, HexEngine::MessageListener* sender)
	{
		(void)sender;

		if (message == nullptr)
			return;

		if (auto* created = message->CastAs<HexEngine::EditorEntityCreatedMessage>(); created != nullptr)
		{
			if (created->entity != nullptr && !created->entity->IsPendingDeletion())
			{
				RecordEntityCreated(created->entity);
				created->handled = true;
			}
		}
	}

	void EditorUI::RecordEntityPositionChange(HexEngine::Entity* entity, const math::Vector3& before, const math::Vector3& after)
	{
		if (entity == nullptr || before == after)
			return;

		_transactions.Push(std::make_unique<PositionTransaction>(entity->GetName(), before, after));
		_prefabController.HandleTransformPositionEdit(entity, before, after);
	}

	void EditorUI::RecordEntityScaleChange(HexEngine::Entity* entity, const math::Vector3& before, const math::Vector3& after)
	{
		if (entity == nullptr || before == after)
			return;

		_transactions.Push(std::make_unique<ScaleTransaction>(entity->GetName(), before, after));
		_prefabController.HandleTransformScaleEdit(entity, before, after);
	}

	void EditorUI::RecordStaticMeshMaterialChange(HexEngine::Entity* entity, const fs::path& before, const fs::path& after)
	{
		if (entity == nullptr || before == after)
			return;

		_transactions.Push(std::make_unique<MaterialAssignmentTransaction>(entity->GetName(), before, after));
		_prefabController.HandleStaticMeshMaterialEdit(entity, before, after);
	}

	void EditorUI::RecordEntityRename(HexEngine::Entity* entity, const std::string& beforeName, const std::string& afterName)
	{
		if (entity == nullptr || beforeName.empty() || afterName.empty() || beforeName == afterName)
			return;

		_transactions.Push(std::make_unique<RenameTransaction>(beforeName, afterName));
	}

	void EditorUI::RecordEntityParentChange(HexEngine::Entity* entity, HexEngine::Entity* beforeParent, HexEngine::Entity* afterParent)
	{
		if (entity == nullptr || beforeParent == afterParent)
			return;

		const std::string beforeParentName = beforeParent ? beforeParent->GetName() : "";
		const std::string afterParentName = afterParent ? afterParent->GetName() : "";
		_transactions.Push(std::make_unique<ReparentTransaction>(entity->GetName(), beforeParentName, afterParentName));
		_entityListRefreshPending = true;
	}

	void EditorUI::RecordEntityVisibilityChange(HexEngine::Entity* entity, bool beforeHidden, bool afterHidden)
	{
		if (entity == nullptr || beforeHidden == afterHidden)
			return;

		_transactions.Push(std::make_unique<VisibilityTransaction>(entity->GetName(), beforeHidden, afterHidden));
	}

	void EditorUI::RecordComponentAdded(HexEngine::BaseComponent* component)
	{
		auto transaction = ComponentLifecycleTransaction::CreateForAddedComponent(component);
		if (transaction)
		{
			_transactions.Push(std::move(transaction));
		}

		if (component == nullptr)
			return;

		auto* entity = component->GetEntity();
		if (entity == nullptr)
			return;

		Detail::EntityComponentStateSnapshot afterSnapshot;
		if (!Detail::CaptureEntityComponentState(entity, afterSnapshot))
			return;

		Detail::EntityComponentStateSnapshot beforeSnapshot = afterSnapshot;
		if (beforeSnapshot.components.is_array())
		{
			const std::string componentName = component->GetComponentName();
			beforeSnapshot.components.erase(
				std::remove_if(beforeSnapshot.components.begin(), beforeSnapshot.components.end(),
					[&](const json& item)
					{
						return item.is_object() && item.value("name", std::string()) == componentName;
					}),
				beforeSnapshot.components.end());
		}

		_prefabController.HandleComponentPropertyEdit(entity, beforeSnapshot.components, afterSnapshot.components);
	}

	void EditorUI::RecordComponentDeleted(HexEngine::BaseComponent* component)
	{
		auto transaction = ComponentLifecycleTransaction::CreateForRemovedComponent(component);
		if (transaction)
		{
			_transactions.Push(std::move(transaction));
		}

		if (component == nullptr)
			return;

		auto* entity = component->GetEntity();
		if (entity == nullptr)
			return;

		Detail::EntityComponentStateSnapshot beforeSnapshot;
		if (!Detail::CaptureEntityComponentState(entity, beforeSnapshot))
			return;

		Detail::EntityComponentStateSnapshot afterSnapshot = beforeSnapshot;
		if (afterSnapshot.components.is_array())
		{
			const std::string componentName = component->GetComponentName();
			afterSnapshot.components.erase(
				std::remove_if(afterSnapshot.components.begin(), afterSnapshot.components.end(),
					[&](const json& item)
					{
						return item.is_object() && item.value("name", std::string()) == componentName;
					}),
				afterSnapshot.components.end());
		}

		_prefabController.HandleComponentPropertyEdit(entity, beforeSnapshot.components, afterSnapshot.components);
	}

	void EditorUI::RecordComponentPropertyStateChange(
		HexEngine::Entity* entity,
		const Detail::EntityComponentStateSnapshot& before,
		const Detail::EntityComponentStateSnapshot& after)
	{
		if (entity == nullptr || entity->IsPendingDeletion())
			return;

		if (!HasMatchingComponentLayout(before.components, after.components))
			return;

		if (before.components == after.components)
			return;

		_transactions.Push(std::make_unique<ComponentPropertyTransaction>(before, after));
		_prefabController.HandleComponentPropertyEdit(entity, before.components, after.components);
	}

	void EditorUI::RecordEntityCreated(HexEngine::Entity* entity)
	{
		auto transaction = EntityLifecycleTransaction::CreateForCreatedEntity(entity);
		if (transaction)
		{
			_transactions.Push(std::move(transaction));
		}
	}

	void EditorUI::RecordEntityDeleted(HexEngine::Entity* entity)
	{
		auto transaction = EntityLifecycleTransaction::CreateForDeletedEntity(entity);
		if (transaction)
		{
			_transactions.Push(std::move(transaction));
		}
	}

	bool EditorUI::UndoLastTransaction()
	{
		const bool undone = _transactions.Undo();
		if (undone && _entityList != nullptr)
		{
			_entityList->RefreshList();
		}

		if (undone && _rightDock != nullptr)
		{
			if (auto* inspecting = _rightDock->GetInspectingEntity(); inspecting != nullptr && !inspecting->IsPendingDeletion())
			{
				_rightDock->InspectEntity(nullptr);
				_rightDock->InspectEntity(inspecting);
			}
		}

		return undone;
	}

	bool EditorUI::RedoLastTransaction()
	{
		const bool redone = _transactions.Redo();
		if (redone && _entityList != nullptr)
		{
			_entityList->RefreshList();
		}

		if (redone && _rightDock != nullptr)
		{
			if (auto* inspecting = _rightDock->GetInspectingEntity(); inspecting != nullptr && !inspecting->IsPendingDeletion())
			{
				_rightDock->InspectEntity(nullptr);
				_rightDock->InspectEntity(inspecting);
			}
		}

		return redone;
	}
}
