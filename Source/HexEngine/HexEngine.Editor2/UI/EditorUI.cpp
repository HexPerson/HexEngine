
#include "EditorUI.hpp"
#include <HexEngine.Core/Environment/TimeManager.hpp>
#include <HexEngine.Core/Scene/SceneManager.hpp>
#include "../Editor.hpp"
#include "Actions\ProjectManager.hpp"
#include "Actions\ProjectGenerator.hpp"
#include "Actions\Settings.hpp"
#include "Actions\Terrain.hpp"
#include "Actions\NavMeshTool.hpp"
#include "Actions\MultiplayerTool.hpp"
#include "Actions\BuildDialog.hpp"
#include <HexEngine.Core\FileSystem\SceneSaveFile.hpp>
#include <HexEngine.Core\FileSystem\PrefabLoader.hpp>
#include <HexEngine.Core\Entity\Component\PlayerStartComponent.hpp>
#include <HexEngine.Core\Entity\Component\RigidBody.hpp>
#include <HexEngine.Core\Physics\IRigidBody.hpp>
#include <HexEngine.Core\Input\CommandManager.hpp>
#include <cstring>
#include <ctime>
#include <map>
#include <tuple>
#include <unordered_set>

#include "Gadgets\ScaleGadget.hpp"
#include "Gadgets\PositionGadget.hpp"
#include "Gadgets\DuplicateGadget.hpp"

namespace HexEditor
{
	namespace
	{
		// Root of the HexEngine SDK that generated game projects build against:
		// the folder holding HexEngine.props (include/lib/runtime paths). Found
		// from the editor EXE's own location - never the working directory - so
		// it works both in a source checkout and in an install:
		//   1. HEXENGINE_SDK environment variable (explicit override)
		//   2. installed build: {app}\SDK\HexEngine.props
		//   3. development tree: the nearest ancestor of the EXE folder holding
		//      HexEngine.props (the repository root). Searched rather than assumed
		//      to be a fixed depth, because the editor runs from more than one
		//      output folder: <repo>\Bin\x64\<Config>\ (3 levels down) and, when
		//      launched from Visual Studio, the linker output
		//      <repo>\Source\HexEngine\x64\<Config>\ (4 levels down).
		fs::path ResolveSdkRoot()
		{
			std::error_code ec;

			wchar_t envSdk[MAX_PATH] = {};
			const DWORD envLen = GetEnvironmentVariableW(L"HEXENGINE_SDK", envSdk, MAX_PATH);
			if (envLen > 0 && envLen < MAX_PATH)
			{
				if (fs::exists(fs::path(envSdk) / "HexEngine.props", ec))
					return fs::path(envSdk);

				LOG_WARN("HEXENGINE_SDK is set to '%S' but it holds no HexEngine.props; ignoring it", envSdk);
			}

			wchar_t exePath[MAX_PATH] = {};
			GetModuleFileNameW(nullptr, exePath, MAX_PATH);
			const fs::path exeDir = fs::path(exePath).parent_path();

			if (fs::exists(exeDir / "SDK" / "HexEngine.props", ec))
				return exeDir / "SDK";

			for (fs::path dir = exeDir; !dir.empty(); dir = dir.parent_path())
			{
				if (fs::exists(dir / "HexEngine.props", ec))
					return dir;

				if (dir == dir.root_path())
					break;
			}

			// Nothing found: keep the historical <exe>\..\..\..\ guess so the
			// generated project at least names a plausible location, and say so.
			const fs::path fallback = exeDir.parent_path().parent_path().parent_path();
			LOG_WARN("HexEngine SDK not found (no SDK/HexEngine.props beside the editor, no HexEngine.props in any parent of '%S', HEXENGINE_SDK unset); generated projects will point at %S and will not build until HEXENGINE_SDK is set", exeDir.wstring().c_str(), fallback.wstring().c_str());
			return fallback;
		}
	}

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

		HexEngine::Transform::SetEditorRotateCommitCallback(
			[](HexEngine::Entity* entity, const math::Quaternion& before, const math::Quaternion& after)
			{
				if (g_pUIManager != nullptr)
				{
					g_pUIManager->RecordEntityRotationChange(entity, before, after);
				}
			});

		HexEngine::Transform::SetEditorScaleCommitCallback(
			[](HexEngine::Entity* entity, const math::Vector3& before, const math::Vector3& after)
			{
				if (g_pUIManager != nullptr)
				{
					g_pUIManager->RecordEntityScaleChange(entity, before, after);
				}
			});

		_gadgets.push_back(new ScaleGadget);
		_gadgets.push_back(new PositionGadget);
		_gadgets.push_back(new DuplicateGadget);
	}

	EditorUI::~EditorUI()
	{		
		HexEngine::Transform::SetEditorTranslateCommitCallback({});
		HexEngine::Transform::SetEditorRotateCommitCallback({});
		HexEngine::Transform::SetEditorScaleCommitCallback({});
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
		_prefabController.SetTransactionStack(&_transactions);
		if (_entityList != nullptr)
		{
			// Out-of-date prefab instances get a badge in the entity list.
			_entityList->_badgeProvider = [this](HexEngine::Entity* entity, math::Color& colour) -> std::wstring
			{
				if (entity != nullptr && entity->IsPrefabInstanceRoot() && _prefabController.IsPrefabInstanceOutOfDate(entity))
				{
					colour = math::Color(1.0f, 0.72f, 0.2f, 1.0f);
					return L"(prefab changed)";
				}
				return std::wstring();
			};
		}

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
			_projectLoading = true;
			std::vector<std::wstring> loadedScenePaths;

			for (auto& scene : file._scenes)
			{
				// A project file listing the same scene twice (written by the old
				// open-during-load race) must not load it twice: two live copies
				// saved over each other, the stale one last.
				{
					std::error_code pathEc;
					std::wstring key = fs::weakly_canonical(scene->GetAbsolutePath(), pathEc).wstring();
					std::transform(key.begin(), key.end(), key.begin(), ::towlower);
					if (std::find(loadedScenePaths.begin(), loadedScenePaths.end(), key) != loadedScenePaths.end())
					{
						LOG_WARN("Project lists scene '%s' more than once - loading it once", scene->GetAbsolutePath().string().c_str());
						continue;
					}
					loadedScenePaths.push_back(std::move(key));
				}

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
						_projectLoading = false;
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
			_projectLoading = false;
			_entityListRefreshPending = true;

			// Instances whose prefab changed while their scene wasn't loaded.
			// NOT called here: this runs on the project-load worker thread, and the
			// check always ends in RefreshViewsAfterInstanceChange() ->
			// EntityList::RefreshList(), which raced the main thread's own pending
			// refresh (both Clear()/AddNode the same TreeList -> use-after-free in
			// TreeList::AddNode). It can also sync instances and open dialogs. Defer
			// it to the main thread (EditorUI::Update).
			_prefabLoadCheckPending = true;

			
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
			params.sdkPath = ResolveSdkRoot();
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

				HexEngine::MenuBar::Item* actionUpdateInstances = new HexEngine::MenuBar::Item;
				actionUpdateInstances->name = L"Update Out-of-Date Prefab Instances";
				actionUpdateInstances->action = [this](HexEngine::MenuBar::Item*) { _prefabController.UpdateAllOutOfDatePrefabInstances(); };
				_mainMenu->AddSubItem(file, actionUpdateInstances);

				const std::pair<const wchar_t*, PrefabController::InstanceSyncPolicy> syncPolicies[] = {
					{ L"Prefab Changes: Ask Before Updating Instances", PrefabController::InstanceSyncPolicy::Ask },
					{ L"Prefab Changes: Always Update Instances", PrefabController::InstanceSyncPolicy::Always },
					{ L"Prefab Changes: Never Update Instances", PrefabController::InstanceSyncPolicy::Never } };
				for (const auto& [label, policy] : syncPolicies)
				{
					HexEngine::MenuBar::Item* actionPolicy = new HexEngine::MenuBar::Item;
					actionPolicy->name = label;
					actionPolicy->action = [this, policy = policy](HexEngine::MenuBar::Item*) { _prefabController.SetInstanceSyncPolicy(policy); };
					_mainMenu->AddSubItem(file, actionPolicy);
				}

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

				HexEngine::MenuBar::Item* actionNewTube = new HexEngine::MenuBar::Item;
				actionNewTube->name = L"Add tube light";
				actionNewTube->action = std::bind(&EditorUI::OnAddAreaLight, this, HexEngine::AreaLight::Shape::Tube);
				_mainMenu->AddSubItem(scene, actionNewTube);

				HexEngine::MenuBar::Item* actionNewRect = new HexEngine::MenuBar::Item;
				actionNewRect->name = L"Add rect light";
				actionNewRect->action = std::bind(&EditorUI::OnAddAreaLight, this, HexEngine::AreaLight::Shape::Rect);
				_mainMenu->AddSubItem(scene, actionNewRect);

				HexEngine::MenuBar::Item* actionNewBB = new HexEngine::MenuBar::Item;
				actionNewBB->name = L"Add billboard";
				actionNewBB->action = std::bind(&EditorUI::OnAddBillboard, this);
				_mainMenu->AddSubItem(scene, actionNewBB);

				// Add decal -> spawns a flat decal entity at the cursor with sensible
				// puddle-style defaults (mostly-smooth roughness, no metallic, normal
				// cutoff for floor-only projection). Artists then drop a texture onto
				// the Albedo / Mat slots in the inspector.
				HexEngine::MenuBar::Item* actionNewDecal = new HexEngine::MenuBar::Item;
				actionNewDecal->name = L"Add decal";
				actionNewDecal->action = std::bind(&EditorUI::OnAddDecal, this);
				_mainMenu->AddSubItem(scene, actionNewDecal);

				// Player Start -> drops a marker the engine spawns the player at
				// when the game is run (GameIntegrator::RunGame moves the main
				// camera to its transform before OnCreateGame).
				HexEngine::MenuBar::Item* actionNewPlayerStart = new HexEngine::MenuBar::Item;
				actionNewPlayerStart->name = L"Player Start";
				actionNewPlayerStart->action = std::bind(&EditorUI::OnAddPlayerStart, this);
				_mainMenu->AddSubItem(scene, actionNewPlayerStart);

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

				HexEngine::MenuBar::Item* actionMultiplayer = new HexEngine::MenuBar::Item;
				actionMultiplayer->name = L"Multiplayer...";
				actionMultiplayer->action = std::bind(&EditorUI::ShowMultiplayerDialog, this);
				_mainMenu->AddSubItem(scene, actionMultiplayer);

				HexEngine::MenuBar::Item* actionGenerateHlod = new HexEngine::MenuBar::Item;
				actionGenerateHlod->name = L"Generate HLOD";
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

	void EditorUI::TickEditorToolPlugins()
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
				editorTool->OnEditorFrameTick();
			}
		}
	}

	HexEngine::Entity* EditorUI::GetSelectedEntity()
	{
		return (_rightDock != nullptr) ? _rightDock->GetInspectingEntity() : nullptr;
	}

	bool EditorUI::SetSelectedEntity(HexEngine::Entity* entity)
	{
		if (_rightDock == nullptr)
			return false;

		_rightDock->InspectEntity(entity);
		return true;
	}

	std::string EditorUI::GetProjectName()
	{
		if (!_projectFilePath.empty()) return _projectFilePath.stem().string();
		if (!_projectFolderPath.empty()) return _projectFolderPath.filename().string();
		return std::string();
	}

	std::string EditorUI::GetProjectFolderPath()
	{
		return _projectFolderPath.empty() ? std::string() : _projectFolderPath.string();
	}

	std::string EditorUI::GetProjectFilePath()
	{
		return _projectFilePath.empty() ? std::string() : _projectFilePath.string();
	}

	bool EditorUI::OpenProject(const std::string& projectFilePath, std::string& error)
	{
		if (!_projectFolderPath.empty())
		{
			error = "a project is already open; switching projects requires an editor restart";
			return false;
		}

		fs::path p(projectFilePath);
		if (!fs::exists(p))
		{
			error = "project file does not exist: " + projectFilePath;
			return false;
		}

		// Same flow as ProjectManager::OnClickExistingProject: dismiss the
		// browser dialog, show a loading dialog, and run the load on a worker
		// thread (the completion handler expects to be off the main thread so
		// the loading dialog can repaint).
		if (_projectManager != nullptr)
		{
			_projectManager->DeleteMe();
			_projectManager = nullptr;
		}

		HexEngine::LoadingDialog* loadingDlg = new HexEngine::LoadingDialog(
			_rootElement,
			HexEngine::Point((int32_t)GetWidth() / 2 - 220, (int32_t)GetHeight() / 2 - 60),
			HexEngine::Point(440, 120), L"Loading");

		std::thread thread([this](const fs::path path, HexEngine::LoadingDialog* dlg)
			{
				OnProjectManagerCompleted(path.parent_path(), path.filename().string(), true, L"", dlg);
			},
			p, loadingDlg);
		thread.detach();

		return true;
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

	bool EditorUI::IsPrefabInstanceOutOfDate(HexEngine::Entity* entity) const
	{
		return _prefabController.IsPrefabInstanceOutOfDate(entity);
	}

	bool EditorUI::UpdatePrefabInstanceFromAsset(HexEngine::Entity* entity)
	{
		return _prefabController.UpdatePrefabInstanceFromAsset(entity);
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

	void EditorUI::ShowMultiplayerDialog()
	{
		auto* mpDialog = MultiplayerTool::CreateEditorDialog(_rootElement);
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

	namespace
	{
		// Vertex-clustering simplification (Rossignac-Borrel) for HLOD proxies.
		// Vertices are snapped to a grid of `cell` metres and merged only when
		// they share a cell, a dominant normal axis (hard building corners keep
		// their shading) AND a quantised UV: Universe meshes map faces to palette
		// swatches in a colour atlas, and merging vertices from different
		// swatches made triangles interpolate across the atlas (rainbow stripes
		// on roofs and pavements). Triangles that collapse, or that repeat
		// another with the same winding, are dropped. Position, normal and UV
		// are averaged within a bucket; the tangent frame comes from the first.
		void SimplifyHlodGeometry(std::vector<HexEngine::MeshVertex>& vertices, std::vector<HexEngine::MeshIndexFormat>& indices, float cell)
		{
			if (cell <= 0.0f || vertices.empty() || indices.size() < 3)
				return;

			struct CellKey
			{
				int32_t x, y, z;
				uint32_t n;
				int32_t u, v;
				bool operator==(const CellKey& o) const { return x == o.x && y == o.y && z == o.z && n == o.n && u == o.u && v == o.v; }
			};
			struct CellKeyHash
			{
				size_t operator()(const CellKey& k) const
				{
					uint64_t h = static_cast<uint64_t>(static_cast<uint32_t>(k.x)) * 0x9E3779B185EBCA87ull;
					h ^= static_cast<uint64_t>(static_cast<uint32_t>(k.y)) * 0xC2B2AE3D27D4EB4Full + (h << 6) + (h >> 2);
					h ^= static_cast<uint64_t>(static_cast<uint32_t>(k.z)) * 0x165667B19E3779F9ull + (h << 6) + (h >> 2);
					h ^= static_cast<uint64_t>(k.n) * 0x27D4EB2F165667C5ull;
					h ^= static_cast<uint64_t>(static_cast<uint32_t>(k.u)) * 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
					h ^= static_cast<uint64_t>(static_cast<uint32_t>(k.v)) * 0xBF58476D1CE4E5B9ull + (h << 6) + (h >> 2);
					return static_cast<size_t>(h);
				}
			};
			const auto normalAxis = [](const math::Vector3& n) -> uint32_t
			{
				const float ax = std::fabs(n.x), ay = std::fabs(n.y), az = std::fabs(n.z);
				if (ax >= ay && ax >= az) return n.x >= 0.0f ? 0u : 1u;
				if (ay >= az) return n.y >= 0.0f ? 2u : 3u;
				return n.z >= 0.0f ? 4u : 5u;
			};

			const float inv = 1.0f / cell;
			// UV quantum: 1/64 keeps palette swatches (1/16..1/32 of the atlas)
			// apart and stops tiled textures (UVs spanning repeats) from being
			// merged across more than a fraction of a tile.
			constexpr float kUvQuantum = 64.0f;
			std::unordered_map<CellKey, uint32_t, CellKeyHash> cells;
			cells.reserve(vertices.size() / 2 + 1);
			std::vector<uint32_t> remap(vertices.size());
			std::vector<HexEngine::MeshVertex> merged;
			std::vector<math::Vector3> posSum, nrmSum;
			std::vector<math::Vector2> uvSum;
			std::vector<uint32_t> count;

			for (size_t i = 0; i < vertices.size(); ++i)
			{
				const auto& v = vertices[i];
				const math::Vector3 p(v._position.x, v._position.y, v._position.z);
				const CellKey key{
					static_cast<int32_t>(std::floor(p.x * inv)),
					static_cast<int32_t>(std::floor(p.y * inv)),
					static_cast<int32_t>(std::floor(p.z * inv)),
					normalAxis(v._normal),
					static_cast<int32_t>(std::floor(v._texcoord.x * kUvQuantum)),
					static_cast<int32_t>(std::floor(v._texcoord.y * kUvQuantum)) };
				const auto [it, inserted] = cells.try_emplace(key, static_cast<uint32_t>(merged.size()));
				if (inserted)
				{
					merged.push_back(v);
					posSum.push_back(p);
					nrmSum.push_back(v._normal);
					uvSum.push_back(v._texcoord);
					count.push_back(1u);
				}
				else
				{
					posSum[it->second] += p;
					nrmSum[it->second] += v._normal;
					uvSum[it->second] += v._texcoord;
					++count[it->second];
				}
				remap[i] = it->second;
			}

			for (size_t j = 0; j < merged.size(); ++j)
			{
				auto& v = merged[j];
				const math::Vector3 p = posSum[j] / static_cast<float>(count[j]);
				v._position = math::Vector4(p.x, p.y, p.z, 1.0f);
				v._texcoord = uvSum[j] / static_cast<float>(count[j]);

				math::Vector3 n = nrmSum[j];
				if (n.LengthSquared() > 1e-12f)
				{
					n.Normalize();
					v._normal = n;
				}

				// Re-orthogonalise the representative's tangent frame against the
				// averaged normal, keeping its handedness.
				math::Vector3 t = v._tangent - v._normal * v._normal.Dot(v._tangent);
				if (t.LengthSquared() > 1e-12f)
				{
					t.Normalize();
					const math::Vector3 b = v._normal.Cross(t);
					const float handed = v._bitangent.Dot(b) < 0.0f ? -1.0f : 1.0f;
					v._tangent = t;
					v._bitangent = b * handed;
				}
			}

			struct TriKey
			{
				uint32_t a, b, c;
				bool operator==(const TriKey& o) const { return a == o.a && b == o.b && c == o.c; }
			};
			struct TriKeyHash
			{
				size_t operator()(const TriKey& k) const
				{
					uint64_t h = static_cast<uint64_t>(k.a) * 0x9E3779B185EBCA87ull;
					h ^= static_cast<uint64_t>(k.b) * 0xC2B2AE3D27D4EB4Full + (h << 6) + (h >> 2);
					h ^= static_cast<uint64_t>(k.c) * 0x165667B19E3779F9ull + (h << 6) + (h >> 2);
					return static_cast<size_t>(h);
				}
			};

			std::unordered_set<TriKey, TriKeyHash> seen;
			seen.reserve(indices.size() / 3);
			std::vector<HexEngine::MeshIndexFormat> outIndices;
			outIndices.reserve(indices.size());
			std::vector<uint32_t> compact(merged.size(), UINT32_MAX);
			std::vector<HexEngine::MeshVertex> outVertices;
			outVertices.reserve(merged.size());

			for (size_t t = 0; t + 2 < indices.size(); t += 3)
			{
				if (indices[t] >= remap.size() || indices[t + 1] >= remap.size() || indices[t + 2] >= remap.size())
					continue;

				const uint32_t a = remap[indices[t]];
				const uint32_t b = remap[indices[t + 1]];
				const uint32_t c = remap[indices[t + 2]];
				if (a == b || b == c || a == c)
					continue;

				// Canonical rotation (smallest first) keeps the winding, so a
				// two-sided wall's back face isn't mistaken for a duplicate.
				const TriKey key = (a < b && a < c) ? TriKey{ a, b, c } : (b < c ? TriKey{ b, c, a } : TriKey{ c, a, b });
				if (!seen.insert(key).second)
					continue;

				for (const uint32_t m : { a, b, c })
				{
					if (compact[m] == UINT32_MAX)
					{
						compact[m] = static_cast<uint32_t>(outVertices.size());
						outVertices.push_back(merged[m]);
					}
					outIndices.push_back(compact[m]);
				}
			}

			vertices.swap(outVertices);
			indices.swap(outIndices);
		}

		// True when the entity, or anything above it, is hidden, opted out of
		// HLOD, or can move: a baked proxy would freeze a mover in place and keep
		// drawing it after it drove off.
		bool IsHlodExcludedOrMover(HexEngine::Entity* entity)
		{
			static constexpr const char* kMoverComponents[] = {
				"Vehicle", "Drivable", "PathMover", "Door", "RoutineAgent", "Ragdoll", "SkeletalAnimation", "CharacterController",
				// Not movers, but they swap materials at runtime (signal heads light
				// red/amber/green): a proxy would freeze one state.
				"TrafficSignal" };

			for (HexEngine::Entity* e = entity; e != nullptr; e = e->GetParent())
			{
				if (e->HasFlag(HexEngine::EntityFlags::ExcludeFromHLOD) || e->HasFlag(HexEngine::EntityFlags::DoNotRender) || e->IsPendingDeletion())
					return true;

				if (e->GetLayer() == HexEngine::Layer::DynamicGeometry)
					return true;

				if (auto* rb = e->GetComponent<HexEngine::RigidBody>(); rb != nullptr)
				{
					auto* irb = rb->GetIRigidBody();
					if (irb != nullptr && irb->GetBodyType() != HexEngine::IRigidBody::BodyType::Static)
						return true;
				}

				for (auto* component : e->GetAllComponents())
				{
					const char* name = component ? component->GetComponentName() : nullptr;
					if (name == nullptr)
						continue;
					for (const char* mover : kMoverComponents)
					{
						if (std::strstr(name, mover) != nullptr)
							return true;
					}
				}
			}
			return false;
		}
	}

	namespace
	{
		// hlod_generate <name>: log every bake decision for entities whose name
		// contains <name> (diagnosing a mesh missing from its proxy).
		std::string g_hlodTraceFilter;
	}

	// Console / editor bridge: same bake as Scene > Generate HLOD.
	HEX_COMMAND(hlod_generate)
	{
		g_hlodTraceFilter = (args != nullptr && args->Count() > 0) ? args->GetAll()[0] : std::string();
		if (g_pUIManager != nullptr)
			g_pUIManager->OnGenerateHLOD();
		g_hlodTraceFilter.clear();
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

		HexEngine::FileSystem* gameFs = HexEngine::g_pEnv->GetResourceSystem().FindFileSystemByName(L"GameData");
		if (gameFs == nullptr)
		{
			LOG_WARN("Cannot generate HLOD because GameData filesystem is unavailable");
			return;
		}

		// The cell size comes from the runtime cvar (saved with the scene), so the
		// PVS maps each source to the same cell the bake put it in.
		float clusterSize = 200.0f;
		if (auto* commands = HexEngine::g_pEnv->_commandManager; commands != nullptr)
		{
			if (auto* var = commands->FindHVar("r_hlodClusterSize"); var != nullptr)
				clusterSize = std::max(1.0f, var->_val.f32);
		}
		// Meshes smaller than this (world bounding radius) are a few pixels at the
		// switch distance: left out of the proxy, they keep drawing (and LODing)
		// normally instead of bloating it - furniture, street props, kerbs.
		constexpr float kMinSourceRadius = 2.0f;
		// Vertex-clustering cell: about one pixel at 1080p / 60 deg FOV at 400 m.
		constexpr float kSimplifyCell = 0.5f;
		static const std::string kGroupName = "HLODProxies";

		// ---- Remove the previous bake (proxies, source marks, files) ----
		if (auto* oldGroup = currentScene->GetEntityByName(kGroupName); oldGroup != nullptr)
			currentScene->DestroyEntity(oldGroup);

		int32_t removedProxies = 0;
		{
			std::vector<HexEngine::StaticMeshComponent*> existing;
			currentScene->GetComponents(existing);
			std::vector<HexEngine::Entity*> oldProxies, oldGroups;
			for (auto* smc : existing)
			{
				auto* entity = smc ? smc->GetEntity() : nullptr;
				if (entity == nullptr || entity->IsPendingDeletion())
					continue;
				if (entity->GetName().rfind("HLOD_", 0) == 0)
				{
					oldProxies.push_back(entity);
					// A group left behind under a deduped name ("HLODProxies_1").
					if (auto* parent = entity->GetParent(); parent != nullptr && parent->GetName().rfind(kGroupName, 0) == 0 &&
						std::find(oldGroups.begin(), oldGroups.end(), parent) == oldGroups.end())
						oldGroups.push_back(parent);
				}
				else if (entity->HasFlag(HexEngine::EntityFlags::RepresentedByHLOD))
					entity->ClearFlags(HexEngine::EntityFlags::RepresentedByHLOD);
			}
			for (auto* entity : oldProxies)
			{
				if (!entity->IsPendingDeletion())
				{
					currentScene->DestroyEntity(entity);
					++removedProxies;
				}
			}
			for (auto* oldGroupEntity : oldGroups)
			{
				if (!oldGroupEntity->IsPendingDeletion())
					currentScene->DestroyEntity(oldGroupEntity);
			}
		}

		fs::path hlodOutputDir = _projectFolderPath / "Data/HLOD";
		std::error_code fsErr;
		fs::create_directories(hlodOutputDir, fsErr);
		if (fsErr)
		{
			LOG_WARN("Failed to create HLOD output directory '%s': %s", hlodOutputDir.string().c_str(), fsErr.message().c_str());
			return;
		}

		int32_t removedFiles = 0;
		std::vector<fs::path> oldFiles;
		for (const auto& file : fs::directory_iterator(hlodOutputDir, fsErr))
		{
			const fs::path& path = file.path();
			if (path.extension() == ".hmesh" && path.stem().string().rfind("HLOD_", 0) == 0)
				oldFiles.push_back(path);
		}
		for (const auto& path : oldFiles)
		{
			{
				std::error_code rmErr;
				if (fs::remove(path, rmErr))
					++removedFiles;
				else if (rmErr)
					LOG_WARN("HLOD: could not delete old proxy '%s': %s", path.string().c_str(), rmErr.message().c_str());
			}
		}

		// ---- Gather sources ----
		std::vector<HexEngine::StaticMeshComponent*> staticMeshes;
		currentScene->GetComponents(staticMeshes);

		struct Candidate
		{
			HexEngine::StaticMeshComponent* smc = nullptr;
			math::Vector3 world;
			int32_t lod = -1;
			// Set when the coarsest-LOD mesh standing for this source made it into a
			// proxy. Only then is the source hidden at distance: a mesh with no CPU
			// geometry used to be marked anyway and vanished past the switch.
			mutable bool merged = false;
		};
		std::vector<Candidate> candidates;
		candidates.reserve(staticMeshes.size());
		// LOD'd models: bake only the coarsest level present under each parent.
		std::unordered_map<HexEngine::Entity*, int32_t> maxLodByParent;
		int32_t skippedSmall = 0, skippedMover = 0;

		const auto traced = [](HexEngine::Entity* e) { return !g_hlodTraceFilter.empty() && e != nullptr && e->GetName().find(g_hlodTraceFilter) != std::string::npos; };

		for (auto* smc : staticMeshes)
		{
			auto* entity = smc ? smc->GetEntity() : nullptr;
			auto mesh = smc ? smc->GetMesh() : nullptr;
			if (entity == nullptr || mesh == nullptr || entity->IsPendingDeletion())
			{
				if (traced(entity)) LOG_INFO("HLOD trace '%s': no mesh / pending deletion", entity->GetName().c_str());
				continue;
			}
			if (entity->GetName().rfind("HLOD_", 0) == 0)
				continue;

			const HexEngine::Layer layer = entity->GetLayer();
			if (layer != HexEngine::Layer::StaticGeometry && layer != HexEngine::Layer::Decorative)
			{
				if (traced(entity)) LOG_INFO("HLOD trace '%s': layer %d", entity->GetName().c_str(), (int32_t)layer);
				continue;
			}
			if (entity->GetChunk() != nullptr || mesh->HasAnimations() || smc->IsBoundToBone())
			{
				if (traced(entity)) LOG_INFO("HLOD trace '%s': chunk %p anim %d bone %d", entity->GetName().c_str(), (void*)entity->GetChunk(), mesh->HasAnimations() ? 1 : 0, smc->IsBoundToBone() ? 1 : 0);
				continue;
			}
			if (IsHlodExcludedOrMover(entity))
			{
				if (traced(entity)) LOG_INFO("HLOD trace '%s': excluded / mover", entity->GetName().c_str());
				++skippedMover;
				continue;
			}
			if (entity->GetWorldBoundingSphere().Radius < kMinSourceRadius)
			{
				if (traced(entity)) LOG_INFO("HLOD trace '%s': small (radius %.2f)", entity->GetName().c_str(), entity->GetWorldBoundingSphere().Radius);
				++skippedSmall;
				continue;
			}
			if (traced(entity))
			{
				const auto w = entity->GetWorldTM().Translation();
				LOG_INFO("HLOD trace '%s': candidate, radius %.2f, lod %d, world (%.1f %.1f %.1f), %zu verts %zu indices",
					entity->GetName().c_str(), entity->GetWorldBoundingSphere().Radius, mesh->GetLodLevel(), w.x, w.y, w.z,
					mesh->GetVertices().size(), mesh->GetIndices().size());
			}

			Candidate c;
			c.smc = smc;
			c.world = entity->GetWorldTM().Translation();
			c.lod = mesh->GetLodLevel();
			if (c.lod != -1)
			{
				int32_t& maxLod = maxLodByParent[entity->GetParent()];
				maxLod = std::max(maxLod, c.lod);
			}
			candidates.push_back(c);
		}

		struct ClusterData
		{
			int32_t cx = 0, cy = 0, cz = 0;
			std::vector<const Candidate*> bake;      // meshes merged into the proxy
			std::vector<const Candidate*> sources;   // everything the proxy may stand in for (incl. finer LODs)
			math::Vector3 centreSum = math::Vector3::Zero;
		};
		std::map<std::tuple<int32_t, int32_t, int32_t>, ClusterData> clusters;

		for (const auto& c : candidates)
		{
			// Same packing as PVS::PackHlodClusterKeyFromPosition, on the WORLD position.
			const int32_t cx = static_cast<int32_t>(std::floor(c.world.x / clusterSize));
			const int32_t cy = static_cast<int32_t>(std::floor(c.world.y / clusterSize));
			const int32_t cz = static_cast<int32_t>(std::floor(c.world.z / clusterSize));
			auto& cluster = clusters[{ cx, cy, cz }];
			cluster.cx = cx; cluster.cy = cy; cluster.cz = cz;
			cluster.sources.push_back(&c);

			const bool coarsest = c.lod == -1 || c.lod == maxLodByParent[c.smc->GetEntity()->GetParent()];
			if (coarsest)
			{
				cluster.bake.push_back(&c);
				cluster.centreSum += c.world;
			}
		}

		auto* group = currentScene->CreateEntity(kGroupName, math::Vector3::Zero);
		group->SetFlag(HexEngine::EntityFlags::ExcludeFromHLOD | HexEngine::EntityFlags::DoNotBlockNavMesh);

		const std::string bakeStamp = std::to_string(static_cast<long long>(std::time(nullptr)));
		int32_t generatedProxies = 0, representedSources = 0, skippedNoGeometry = 0;
		uint64_t trianglesIn = 0, trianglesOut = 0;

		for (auto& [cell, cluster] : clusters)
		{
			if (cluster.bake.size() < 2)
				continue;

			const std::string clusterKey = std::to_string(cluster.cx) + "_" + std::to_string(cluster.cy) + "_" + std::to_string(cluster.cz);
			// One centre for every proxy of the cluster: the runtime switches the
			// whole cluster on one distance, so its material groups never disagree.
			const math::Vector3 clusterCentre = cluster.centreSum / static_cast<float>(cluster.bake.size());

			// Material + UV scale: one proxy (one draw) per group.
			std::map<std::tuple<HexEngine::Material*, float, float>, std::vector<const Candidate*>> materialGroups;
			for (const Candidate* c : cluster.bake)
			{
				const auto uv = c->smc->GetUVScale();
				materialGroups[{ c->smc->GetMaterial().get(), uv.x, uv.y }].push_back(c);
			}

			int32_t clusterProxies = 0;
			int32_t materialGroupIndex = 0;
			for (const auto& [groupKey, groupMeshes] : materialGroups)
			{
				std::vector<HexEngine::MeshVertex> combinedVertices;
				std::vector<HexEngine::MeshIndexFormat> combinedIndices;
				std::shared_ptr<HexEngine::Material> groupMaterial;

				for (const Candidate* c : groupMeshes)
				{
					auto* smc = c->smc;
					auto mesh = smc->GetMesh();
					const auto& sourceVertices = mesh->GetVertices();
					const auto& sourceIndices = mesh->GetIndices();
					if (sourceVertices.empty() || sourceIndices.size() < 3)
						continue;
					c->merged = true;

					if (!groupMaterial)
						groupMaterial = smc->GetMaterial();

					const math::Matrix worldTM = smc->GetEntity()->GetWorldTM();
					const math::Matrix normalTM = worldTM.Invert().Transpose(); // correct under non-uniform scale
					// A mirrored instance's triangles wind the other way; the proxy is
					// drawn unmirrored, so flip them back.
					const bool mirrored = worldTM.Determinant() < 0.0f;
					const auto indexOffset = static_cast<HexEngine::MeshIndexFormat>(combinedVertices.size());

					for (auto vertex : sourceVertices)
					{
						const math::Vector3 p = math::Vector3::Transform(math::Vector3(vertex._position.x, vertex._position.y, vertex._position.z), worldTM);
						vertex._position = math::Vector4(p.x - clusterCentre.x, p.y - clusterCentre.y, p.z - clusterCentre.z, 1.0f);
						vertex._normal = math::Vector3::TransformNormal(vertex._normal, normalTM);
						vertex._tangent = math::Vector3::TransformNormal(vertex._tangent, worldTM);
						vertex._bitangent = math::Vector3::TransformNormal(vertex._bitangent, worldTM);
						vertex._normal.Normalize();
						vertex._tangent.Normalize();
						vertex._bitangent.Normalize();
						combinedVertices.push_back(vertex);
					}

					for (size_t i = 0; i + 2 < sourceIndices.size(); i += 3)
					{
						combinedIndices.push_back(sourceIndices[i] + indexOffset);
						combinedIndices.push_back(sourceIndices[mirrored ? i + 2 : i + 1] + indexOffset);
						combinedIndices.push_back(sourceIndices[mirrored ? i + 1 : i + 2] + indexOffset);
					}
				}

				if (combinedVertices.empty() || combinedIndices.empty())
					continue;

				bool groupTraced = false;
				for (const Candidate* c : groupMeshes)
				{
					if (traced(c->smc->GetEntity()))
					{
						groupTraced = true;
						LOG_INFO("HLOD trace '%s': merged=%d into %s group %d (material '%s')", c->smc->GetEntity()->GetName().c_str(), c->merged ? 1 : 0,
							clusterKey.c_str(), materialGroupIndex, groupMaterial ? groupMaterial->GetName().c_str() : "<default>");
					}
				}
				const size_t trisBefore = combinedIndices.size() / 3;
				trianglesIn += combinedIndices.size() / 3;
				// Simplify in cluster-local space (positions are already relative to
				// the centre), so the grid is anchored per cluster.
				SimplifyHlodGeometry(combinedVertices, combinedIndices, kSimplifyCell);
				if (groupTraced)
					LOG_INFO("HLOD trace: group %s/%d simplified %zu -> %zu triangles", clusterKey.c_str(), materialGroupIndex, trisBefore, combinedIndices.size() / 3);
				if (combinedVertices.empty() || combinedIndices.empty())
					continue;
				trianglesOut += combinedIndices.size() / 3;

				const std::string proxyName = "HLOD_" + clusterKey + "_" + std::to_string(materialGroupIndex);
				// Unique file name per bake: the resource cache keys meshes by path,
				// so reusing a name would hand back the previous bake's mesh.
				const fs::path outputPath = hlodOutputDir / (proxyName + "_" + bakeStamp + ".hmesh");
				++materialGroupIndex;

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

				// The sources' own material (shared, saved by path) - the old bake
				// made an unsaved in-memory copy per proxy.
				auto proxyMaterial = groupMaterial ? groupMaterial : HexEngine::Material::GetDefaultMaterial();
				combinedMesh->SetMaterial(proxyMaterial);
				combinedMesh->Save();

				auto loadedProxyMesh = HexEngine::Mesh::Create(outputPath);
				if (!loadedProxyMesh)
				{
					LOG_WARN("Failed to load generated HLOD mesh '%s'", outputPath.string().c_str());
					continue;
				}

				auto* proxy = currentScene->CreateEntity(proxyName, clusterCentre);
				proxy->SetParent(group);
				// Not a navmesh blocker (the sources already are) and never re-baked.
				proxy->SetFlag(HexEngine::EntityFlags::ExcludeFromHLOD | HexEngine::EntityFlags::DoNotBlockNavMesh);
				auto* proxySmc = proxy->AddComponent<HexEngine::StaticMeshComponent>();
				proxySmc->SetMesh(loadedProxyMesh);
				proxySmc->SetMaterial(proxyMaterial);
				proxySmc->SetUVScale(math::Vector2(std::get<1>(groupKey), std::get<2>(groupKey)));
				// The sources are already in the GI; the ray-traced GI skips HLOD_
				// proxies by name, this covers the voxel GI.
				proxySmc->SetExcludeFromGI(true);

				++generatedProxies;
				++clusterProxies;
			}

			if (clusterProxies > 0)
			{
				// A source is hidden at distance only if its own geometry - or, for a
				// finer LOD, its model's coarsest level - was merged.
				std::unordered_set<HexEngine::Entity*> mergedLodParents;
				for (const Candidate* c : cluster.bake)
				{
					if (c->merged && c->lod != -1)
						mergedLodParents.insert(c->smc->GetEntity()->GetParent());
				}
				for (const Candidate* c : cluster.sources)
				{
					const bool represented = c->lod == -1 ? c->merged : mergedLodParents.count(c->smc->GetEntity()->GetParent()) != 0;
					if (!represented)
					{
						++skippedNoGeometry;
						continue;
					}
					c->smc->GetEntity()->SetFlag(HexEngine::EntityFlags::RepresentedByHLOD);
					++representedSources;
				}
			}
		}

		// Every PVS re-reads the cluster tables (the proxies were added through
		// the per-entity path, which only tags roles from the PREVIOUS tables).
		currentScene->ForceRebuildPVS();

		LOG_INFO("HLOD generation complete: %d proxies over %d cell(s) of %.0f m, standing in for %d meshes; %llu -> %llu triangles after simplification. "
			"Skipped %d small (< %.1f m radius), %d moving/excluded and %d without CPU geometry (they keep drawing). Removed %d old proxies and %d old files.",
			generatedProxies, static_cast<int32_t>(clusters.size()), clusterSize, representedSources,
			static_cast<unsigned long long>(trianglesIn), static_cast<unsigned long long>(trianglesOut),
			skippedSmall, kMinSourceRadius, skippedMover, skippedNoGeometry, removedProxies, removedFiles);
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
		// Save the project file first so anything the user just authored is
		// on disk before we hand off to MSBuild / AssetPacker.
		if (_projectFile)
			_projectFile->Save();

		BuildDialog::CreateBuildDialog(
			HexEngine::g_pEnv->GetUIManager().GetRootElement(),
			&_integrator);
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

	void EditorUI::OnAddDecal()
	{
		auto hit = RayCastWorld({}, false);

		auto scene = HexEngine::g_pEnv->_sceneManager->GetCurrentScene();
		if (!scene)
			return;

		// Decal placement: drop the entity at the hit point with a default 1m^3
		// box (artists rescale via the Transform gizmo). The DecalComponent on
		// its own has no visible mesh - the rendered effect comes from the decal
		// pass painting into the GBuffer at this entity's transform, so the
		// inspector is the primary editing surface (texture pickers, opacity,
		// per-channel weights).
		HexEngine::Entity* decalEnt = scene->CreateEntity("Decal", hit.position);
		if (decalEnt == nullptr)
			return;

		// Default extents: 2m XZ x 0.5m Y. Puddles want a flat, wide footprint.
		decalEnt->SetScale(math::Vector3(2.0f, 0.5f, 2.0f));

		decalEnt->AddComponent<HexEngine::DecalComponent>();

		RecordEntityCreated(decalEnt);

		GetInspector()->InspectEntity(decalEnt);
	}

	void EditorUI::OnAddPlayerStart()
	{
		auto hit = RayCastWorld({}, false);

		auto scene = HexEngine::g_pEnv->_sceneManager->GetCurrentScene();
		if (!scene)
			return;

		// Drop the marker at the cursor hit, facing the editor camera's heading
		// so the default forward is sensible (the user rotates it via the
		// Transform gizmo). The PlayerStartComponent itself has no mesh - the
		// gold box + blue arrow gizmo shows where and which way the player
		// spawns; the engine reads this entity's transform on game run.
		HexEngine::Entity* startEnt = scene->CreateEntity("PlayerStart", hit.position);
		if (startEnt == nullptr)
			return;

		startEnt->AddComponent<HexEngine::PlayerStartComponent>();

		RecordEntityCreated(startEnt);

		GetInspector()->InspectEntity(startEnt);
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

	void EditorUI::OnAddAreaLight(HexEngine::AreaLight::Shape shape)
	{
		auto hit = RayCastWorld({}, false);

		// Lifted off the surface under the cursor so the shape isn't buried in it.
		HexEngine::Entity* light = HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->CreateEntity(
			shape == HexEngine::AreaLight::Shape::Rect ? "RectLight" : "TubeLight",
			hit.position + math::Vector3(0.0f, 1.0f, 0.0f));

		auto areaLight = light->AddComponent<HexEngine::AreaLight>();
		areaLight->SetShape(shape);
		areaLight->SetLightStength(4.0f);

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

		// Deferred from the project-load worker thread (see OnProjectManagerCompleted).
		if (_prefabLoadCheckPending.exchange(false))
			_prefabController.CheckLoadedScenesForOutOfDateInstances();

		// RefreshList rebuilds the whole hierarchy (every entity -> a tree node,
		// under the scene lock). Two cases must NOT trigger it as things change:
		//  - while a scene is loading: the loader adds entities continuously, so a
		//    per-frame rebuild is O(n) each frame (O(n^2) over the load) and holds
		//    the scene lock the loader takes per entity - load got slower and
		//    slower toward the end. Rebuild once when loading finishes.
		//  - while the game runs: traffic spawns/despawns cars constantly; a full
		//    rebuild of a ~13k-entity scene was a ~20 ms hitch every 0.25 s. The
		//    list catches up when play is paused or stopped.
		if (_entityList != nullptr && _entityListRefreshPending.load())
		{
			const bool loading = HexEngine::g_pEnv->_sceneManager != nullptr && HexEngine::g_pEnv->_sceneManager->IsLoadingScene();
			const bool playing = _integrator.GetState() == GameTestState::Started;
			const bool paused = HexEngine::g_pEnv->_timeManager != nullptr && HexEngine::g_pEnv->_timeManager->_simulationPaused;
			if (!loading && (!playing || paused))
			{
				_entityListRefreshPending = false;
				_entityList->RefreshList();
			}
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

		// Give editor tool plugins a reliable per-frame main-thread pump (the editor
		// bridge drains its marshalled inspection queue here, so ECS reads no longer
		// depend on incidental mouse-over-viewport tool messages).
		TickEditorToolPlugins();
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
		// Scene hotkeys act on the scene: none of them while another workspace tab
		// (material graph, animation editor) is in front of it.
		const bool sceneTabActive = _sceneView->IsSceneTabActive();

		if (sceneTabActive &&
			_sceneView->GetRoamState() != SceneView::RoamState::FreeLook &&
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

		if (sceneTabActive && event == HexEngine::InputEvent::KeyDown && HexEngine::g_pEnv->_inputSystem->IsCtrlDown())
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

		if (sceneTabActive && _sceneView->GetRoamState() != SceneView::RoamState::FreeLook && _integrator.GetState() != GameTestState::Started && !isTypingInLineEdit)
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

		// W/E/R switch the scene-view transform gizmo (translate/rotate/scale).
		// Only when the mouse is over the viewport so panels keep their keys.
		if (event == HexEngine::InputEvent::KeyDown &&
			!isTypingInLineEdit &&
			!HexEngine::g_pEnv->_inputSystem->IsCtrlDown() &&
			_sceneView->GetRoamState() != SceneView::RoamState::FreeLook &&
			_integrator.GetState() != GameTestState::Started &&
			_sceneView->IsMouseOverSceneViewport())
		{
			switch (data->KeyDown.key)
			{
			case 'W':
				HexEngine::Transform::SetEditorGizmoMode(HexEngine::EditorGizmoMode::Translate);
				return false;

			case 'E':
				HexEngine::Transform::SetEditorGizmoMode(HexEngine::EditorGizmoMode::Rotate);
				return false;

			case 'R':
				HexEngine::Transform::SetEditorGizmoMode(HexEngine::EditorGizmoMode::Scale);
				return false;

			default:
				break;
			}
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

		
		
		if (event == HexEngine::InputEvent::MouseMove && _sceneView->IsMouseOverSceneViewport())
		{
			auto hit = RayCastWorld();

			HexEngine::EditorSceneViewportMouseMoveMessage sceneMoveMessage;
			sceneMoveMessage.screenX = static_cast<int32_t>(std::round(data->MouseMove.x));
			sceneMoveMessage.screenY = static_cast<int32_t>(std::round(data->MouseMove.y));
			sceneMoveMessage.hasHit = hit.entity != nullptr;
			sceneMoveMessage.worldPosition = hit.position;
			sceneMoveMessage.hitEntity = hit.entity;
			if (hit.entity != nullptr)
			{
				sceneMoveMessage.hitEntityId = hit.entity->GetId();
			}
			BroadcastEditorToolMessage(sceneMoveMessage);
		}
		else if (event == HexEngine::InputEvent::MouseDown && data->MouseDown.button == VK_LBUTTON && _sceneView->IsMouseOverSceneViewport())
		{
			if (auto inspecting = _rightDock->GetInspectingEntity(); inspecting != nullptr && inspecting->IsEditorGizmoHovered())
			{
				return false;
			}

			auto hit = RayCastWorld();

			HexEngine::EditorSceneViewportMouseDownMessage sceneClickMessage;
			sceneClickMessage.button = data->MouseDown.button;
			sceneClickMessage.screenX = data->MouseDown.xpos;
			sceneClickMessage.screenY = data->MouseDown.ypos;
			sceneClickMessage.hasHit = hit.entity != nullptr;
			sceneClickMessage.worldPosition = hit.position;
			sceneClickMessage.hitEntity = hit.entity;
			if (hit.entity != nullptr)
			{
				sceneClickMessage.hitEntityId = hit.entity->GetId();
			}
			BroadcastEditorToolMessage(sceneClickMessage);
			if (sceneClickMessage.handled)
			{
				return false;
			}

			if (hit.entity)
			{
				_rightDock->InspectEntity(hit.entity);
			}
		}
		else if (event == HexEngine::InputEvent::KeyDown && _sceneView->IsMouseOverSceneViewport())
		{
			if (_integrator.GetState() == GameTestState::Started && data->KeyDown.key == VK_ESCAPE)
			{
				// Esc only stops play while the game has the mouse captured. A game
				// that has freed the mouse is showing its own UI (dialog, map, menu)
				// and Esc means "close that" - stopping play too threw the tester out
				// of every conversation. Shift+Esc always stops; so does Stop.
				const bool shiftHeld = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
				const bool gameOwnsMouse = HexEngine::g_pEnv->_inputSystem != nullptr &&
					HexEngine::g_pEnv->_inputSystem->GetMouseLockMode() != HexEngine::MouseLockMode::Free;
				// A game with its own pause menu claims Esc (InputSystem::SetGameOwnsEscape).
				const bool gameOwnsEscape = HexEngine::g_pEnv->_inputSystem != nullptr &&
					HexEngine::g_pEnv->_inputSystem->GetGameOwnsEscape();
				if (shiftHeld || (gameOwnsMouse && !gameOwnsEscape))
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

				// Pass ABSOLUTE screen-space coordinates to GetScreenToWorldRay - it has
				// _hasCustomVP / _vp set up by Editor::OnResize + EditorUI::OnInit, so it
				// internally strips _vp.x/y to make the input viewport-local and divides
				// by _vp.width/height for NDC. The previous code subtracted centerLoc.x/y
				// here AND let the function subtract _vp.x/y (which is the same as
				// centerLoc) - net result was double-offset by the scene surface's screen
				// position, so the picked ray was offset by exactly that many pixels.
				// Likewise, scaling mx by vp.width/centerSize.x is a no-op for NDC math
				// (the position-over-size ratio is the same in either pixel space) and
				// only adds rounding error - dropping it.
				if (useMousePos)
				{
					HexEngine::g_pEnv->_inputSystem->GetMousePosition(mx, my);
				}
				else
				{
					const HexEngine::Point centerSize = _sceneView->GetSceneViewportSize();
					const HexEngine::Point centerLoc = _sceneView->GetSceneViewportAbsolutePosition();
					const HexEngine::Point centerPos = centerLoc.GetCenter(centerSize);
					mx = centerPos.x;
					my = centerPos.y;
				}

				auto screenRay = HexEngine::g_pEnv->_inputSystem->GetScreenToWorldRay(mainCamera, mx, my);

				math::Ray ray;
				ray.direction = screenRay;
				ray.position = mainCamera->GetEntity()->GetPosition();

				HexEngine::RayHit hit;
				bool hasHit = false;
				hit.position = ray.position + ray.direction * mainCamera->GetFarZ() * 0.25f;

				// Retry loop honoring EntityFlags::DoNotPickInEditor: any flagged entity we
				// hit gets added to the ignore list and we re-cast through it. Keeps tool
				// preview ghosts (e.g. the road painter's transparent ghost) out of the pick
				// path even though they keep their colliders so the committed version behaves
				// correctly. Bound iterations to avoid infinite loops if a flagged entity is
				// somehow re-emitted by the physics layer despite being in the ignore list.
				std::vector<HexEngine::Entity*> liveIgnoreList = entsToIgnore;
				constexpr int32_t kMaxPickRetries = 32;
				for (int32_t attempt = 0; attempt < kMaxPickRetries; ++attempt)
				{
					if (!HexEngine::PhysUtils::RayCast(
						ray,
						mainCamera->GetFarZ(),
						LAYERMASK(HexEngine::Layer::StaticGeometry) |
						LAYERMASK(HexEngine::Layer::DynamicGeometry),
						&hit,
						liveIgnoreList))
					{
						break;
					}

					if (hit.entity != nullptr && hit.entity->HasFlag(HexEngine::EntityFlags::DoNotPickInEditor))
					{
						liveIgnoreList.push_back(hit.entity);
						continue;
					}

					hasHit = true;
					break;
				}

				HexEngine::EditorWorldRayCastMessage pluginRayCast;
				pluginRayCast.rayOrigin = ray.position;
				pluginRayCast.rayDirection = ray.direction;
				pluginRayCast.maxDistance = mainCamera->GetFarZ();
				pluginRayCast.mask =
					LAYERMASK(HexEngine::Layer::StaticGeometry) |
					LAYERMASK(HexEngine::Layer::DynamicGeometry);
				pluginRayCast.ignoredEntities = entsToIgnore;
				pluginRayCast.hasHit = hasHit;
				if (hasHit)
				{
					pluginRayCast.hitEntity = hit.entity;
					pluginRayCast.hitEntityId = hit.entity != nullptr ? hit.entity->GetId() : HexEngine::InvalidEntityId;
					pluginRayCast.hitPosition = hit.position;
					pluginRayCast.hitNormal = hit.normal;
					pluginRayCast.hitDistance = hit.distance;
				}

				BroadcastEditorToolMessage(pluginRayCast);
				if (pluginRayCast.hasHit)
				{
					hit.entity = pluginRayCast.hitEntity;
					hit.position = pluginRayCast.hitPosition;
					hit.normal = pluginRayCast.hitNormal;
					hit.distance = pluginRayCast.hitDistance;
					hasHit = true;
				}

				if (hasHit)
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

		// The sky sphere belongs to the scene it was created in. This listener
		// serves every scene, and creating a scene (File > New Scene, the bridge's
		// new_scene, project load) adds its SkySphere while ANOTHER scene is
		// current: setting it on GetCurrentScene() left that scene pointing at a
		// foreign entity, which dangled once the new scene was unloaded and
		// crashed Scene::UpdateSkySphereMatrix.
		if (entity->GetName() == "SkySphere")
		{
			if (auto* owner = entity->GetScene(); owner != nullptr)
				owner->SetSkySphere(entity);
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

	void EditorUI::RecordEntityRotationChange(HexEngine::Entity* entity, const math::Quaternion& before, const math::Quaternion& after)
	{
		if (entity == nullptr || before == after)
			return;

		_transactions.Push(std::make_unique<RotationTransaction>(entity->GetName(), before, after));
		_prefabController.HandleTransformRotationEdit(entity, before, after);
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

	// --- IEditorContext: programmatic-authoring hooks (editor bridge) ---------

	HexEngine::FileSystem* EditorUI::GetProjectFileSystem()
	{
		return g_pEditor != nullptr ? g_pEditor->_projectFS : nullptr;
	}

	void EditorUI::BeginUndoGroup(const std::string& label)
	{
		_transactions.BeginGroup(label);
	}

	void EditorUI::EndUndoGroup()
	{
		_transactions.EndGroup();
	}

	bool EditorUI::Undo()
	{
		return UndoLastTransaction();
	}

	bool EditorUI::Redo()
	{
		return RedoLastTransaction();
	}

	size_t EditorUI::GetUndoCount()
	{
		return _transactions.UndoCount();
	}

	size_t EditorUI::GetRedoCount()
	{
		return _transactions.RedoCount();
	}

	void EditorUI::RecordEntityTransformChange(HexEngine::Entity* entity, const math::Vector3& positionBefore, const math::Quaternion& rotationBefore, const math::Vector3& scaleBefore)
	{
		if (entity == nullptr || entity->IsPendingDeletion())
			return;

		// Each Record* also drives prefab-instance override tracking.
		if (entity->GetPosition() != positionBefore)
			RecordEntityPositionChange(entity, positionBefore, entity->GetPosition());
		if (entity->GetRotation() != rotationBefore)
			RecordEntityRotationChange(entity, rotationBefore, entity->GetRotation());
		if (entity->GetScale() != scaleBefore)
			RecordEntityScaleChange(entity, scaleBefore, entity->GetScale());
	}

	void EditorUI::BeginComponentEdit(HexEngine::Entity* entity)
	{
		Detail::EntityComponentStateSnapshot snapshot;
		if (Detail::CaptureEntityComponentState(entity, snapshot))
			_programmaticComponentEdits[entity] = std::move(snapshot);
	}

	void EditorUI::EndComponentEdit(HexEngine::Entity* entity)
	{
		auto it = _programmaticComponentEdits.find(entity);
		if (it == _programmaticComponentEdits.end())
			return;

		Detail::EntityComponentStateSnapshot before = std::move(it->second);
		_programmaticComponentEdits.erase(it);

		Detail::EntityComponentStateSnapshot after;
		if (Detail::CaptureEntityComponentState(entity, after))
			RecordComponentPropertyStateChange(entity, before, after);
	}

	bool EditorUI::SaveAll(std::string& error)
	{
		if (_projectFile == nullptr)
		{
			error = "no project is open";
			return false;
		}
		OnSaveAction();
		return true;
	}

	json EditorUI::DescribePrefabInstance(HexEngine::Entity* entity)
	{
		json j = json::object();
		if (entity == nullptr)
			return j;

		const bool isInstance = IsPrefabInstanceEntity(entity);
		j["isInstance"] = isInstance;
		if (!isInstance)
			return j;

		j["isRoot"] = IsPrefabInstanceRootEntity(entity);
		j["sourcePath"] = entity->GetPrefabSourcePath().string();
		j["rootName"] = entity->GetPrefabRootEntityName();

		// Out-of-date / override state is tracked on the instance ROOT.
		HexEngine::Entity* root = entity;
		while (root != nullptr && !root->IsPrefabInstanceRoot() && root->GetParent() != nullptr)
			root = root->GetParent();
		j["outOfDate"] = root != nullptr && root->IsPrefabInstanceRoot() && IsPrefabInstanceOutOfDate(root);

		std::vector<PrefabController::PrefabPropertyOverride> overrides;
		json list = json::array();
		if (GetPrefabInstancePropertyOverrides(entity, overrides))
		{
			for (const auto& o : overrides)
				list.push_back(json{ {"component", o.componentName}, {"path", o.path}, {"op", o.op} });
		}
		j["overrides"] = list;
		return j;
	}

	bool EditorUI::ApplyPrefabInstance(HexEngine::Entity* entity, std::string& error)
	{
		if (entity == nullptr || !IsPrefabInstanceEntity(entity))
		{
			error = "entity is not a prefab instance";
			return false;
		}
		if (!ApplyPrefabInstanceToPrefabAsset(entity))
		{
			error = "applying the instance to its prefab failed (see engine logs)";
			return false;
		}
		return true;
	}

	HexEngine::Entity* EditorUI::RevertPrefabInstanceToAsset(HexEngine::Entity* entity, std::string& error)
	{
		if (entity == nullptr || !IsPrefabInstanceEntity(entity))
		{
			error = "entity is not a prefab instance";
			return nullptr;
		}
		HexEngine::Entity* reverted = RevertPrefabInstance(entity);
		if (reverted == nullptr)
			error = "reverting the instance failed (see engine logs)";
		return reverted;
	}

	bool EditorUI::UpdatePrefabInstance(HexEngine::Entity* entity, std::string& error)
	{
		if (entity == nullptr || !IsPrefabInstanceEntity(entity))
		{
			error = "entity is not a prefab instance";
			return false;
		}
		if (!UpdatePrefabInstanceFromAsset(entity))
		{
			error = "updating the instance from its prefab failed (see engine logs)";
			return false;
		}
		return true;
	}

	HexEngine::Entity* EditorUI::CreatePrefabFromEntity(HexEngine::Entity* entity, const fs::path& prefabPath, bool replaceWithInstance, std::string& error)
	{
		if (IsPrefabStageActive())
		{
			error = "close the prefab stage before creating prefabs";
			return nullptr;
		}
		if (!_prefabController.WritePrefabAsset(entity, prefabPath, error))
			return nullptr;
		if (!replaceWithInstance)
			return entity;

		auto scene = HexEngine::g_pEnv->_sceneManager->GetCurrentScene();
		if (scene == nullptr || entity->GetScene() != scene.get())
		{
			error = "prefab written, but the entity is not in the active scene so it was not replaced with an instance";
			return entity;
		}

		// Swap the source hierarchy for a linked instance of the new prefab.
		HexEngine::Entity* parent = entity->GetParent();
		const math::Vector3 position = entity->GetPosition();
		const math::Quaternion rotation = entity->GetRotation();
		const math::Vector3 scale = entity->GetScale();
		const std::string name = entity->GetName();

		for (auto* s = GetSelectedEntity(); s != nullptr; s = s->GetParent())
		{
			if (s == entity)
			{
				SetSelectedEntity(nullptr);
				break;
			}
		}

		RecordEntityDeleted(entity);
		scene->DestroyEntity(entity);

		auto spawned = HexEngine::g_pEnv->_prefabLoader->LoadPrefab(scene, prefabPath);
		HexEngine::Entity* root = nullptr;
		for (auto* e : spawned)
		{
			if (e != nullptr && e->GetParent() == nullptr)
			{
				root = e;
				break;
			}
		}
		if (root == nullptr)
		{
			error = "prefab written, but instantiating it failed; the original was removed (undo restores it)";
			return nullptr;
		}

		if (parent != nullptr)
			root->SetParent(parent, false);
		root->SetPosition(position);
		root->SetRotation(rotation);
		root->SetScale(scale);
		std::string finalName;
		scene->RenameEntity(root, name, &finalName);

		RecordEntityCreated(root);
		_entityListRefreshPending = true;
		scene->ForceRebuildPVS();
		return root;
	}

	bool EditorUI::OpenPrefabEditStage(const fs::path& prefabPath, std::string& error)
	{
		if (!OpenPrefabStage(prefabPath))
		{
			error = "could not open the prefab stage for '" + prefabPath.string() + "' (see engine logs)";
			return false;
		}
		return true;
	}

	bool EditorUI::SavePrefabEditStage(std::string& error)
	{
		if (!IsPrefabStageActive())
		{
			error = "no prefab stage is open";
			return false;
		}
		if (!SavePrefabStage())
		{
			error = "saving the prefab stage failed (see engine logs)";
			return false;
		}
		return true;
	}

	bool EditorUI::ClosePrefabEditStage(bool saveChanges, std::string& error)
	{
		if (!IsPrefabStageActive())
		{
			error = "no prefab stage is open";
			return false;
		}
		if (!ClosePrefabStage(saveChanges))
		{
			error = "closing the prefab stage failed (see engine logs)";
			return false;
		}
		return true;
	}

	bool EditorUI::IsPrefabEditStageOpen()
	{
		return IsPrefabStageActive();
	}

	bool EditorUI::StartPlayMode(std::string& error)
	{
		if (IsPrefabStageActive())
		{
			error = "close the prefab stage before playing";
			return false;
		}
		if (_integrator.GetState() == GameTestState::Started)
			return true;
		if (!_integrator.RunGame())
		{
			error = "the game did not start: the project's Build/Game.dll is missing or failed to load (see engine logs)";
			return false;
		}
		return true;
	}

	bool EditorUI::StopPlayMode(std::string& error)
	{
		if (_integrator.GetState() != GameTestState::Started)
		{
			error = "the game is not running";
			return false;
		}
		if (!_integrator.StopGame())
		{
			error = "stopping the game failed (see engine logs)";
			return false;
		}
		return true;
	}

	bool EditorUI::IsPlayModeActive()
	{
		return _integrator.GetState() == GameTestState::Started;
	}

	bool EditorUI::BuildGameModule(std::string& error)
	{
		if (GetProjectFolderPath().empty())
		{
			error = "no project is open";
			return false;
		}
		return _integrator.BuildGameExclusive(error);
	}

	bool EditorUI::ReloadGameModule(std::string& error)
	{
		if (IsPrefabStageActive())
		{
			error = "close the prefab stage before reloading the game";
			return false;
		}
		return _integrator.ReloadBuiltGame(error);
	}

	bool EditorUI::IsGameModuleLoaded()
	{
		return _integrator.IsGameLoaded();
	}

	void EditorUI::SetGameAutoReload(bool enabled)
	{
		_integrator.SetAutoReloadEnabled(enabled);
	}

	bool EditorUI::GetGameAutoReload()
	{
		return _integrator.IsAutoReloadEnabled();
	}

	json EditorUI::ListProjectScenes()
	{
		json out = json::array();
		if (_projectFolderPath.empty())
			return out;

		auto current = HexEngine::g_pEnv->_sceneManager->GetCurrentScene();
		std::set<std::wstring> loadedPaths;
		std::error_code ec;
		for (auto* ssf : _sceneFiles)
		{
			if (ssf == nullptr)
				continue;
			auto scene = ssf->GetScene();
			const fs::path abs = fs::weakly_canonical(ssf->GetAbsolutePath(), ec);
			std::wstring key = abs.wstring();
			std::transform(key.begin(), key.end(), key.begin(), ::towlower);
			loadedPaths.insert(key);
			out.push_back(json{
				{"name", scene ? ws2s(scene->GetName()) : abs.stem().string()},
				{"path", fs::relative(abs, _projectFolderPath, ec).generic_string()},
				{"loaded", scene != nullptr},
				{"active", scene != nullptr && scene == current},
				{"entityCount", scene ? scene->GetTotalNumberOfEntities() : 0u},
			});
		}

		// .hscene files in the project's Data folder that aren't loaded.
		const fs::path dataDir = _projectFolderPath / L"Data";
		for (fs::recursive_directory_iterator it(dataDir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
		{
			if (!it->is_regular_file(ec) || _wcsicmp(it->path().extension().c_str(), L".hscene") != 0)
				continue;
			std::wstring key = fs::weakly_canonical(it->path(), ec).wstring();
			std::transform(key.begin(), key.end(), key.begin(), ::towlower);
			if (loadedPaths.count(key))
				continue;
			out.push_back(json{
				{"name", it->path().stem().string()},
				{"path", fs::relative(it->path(), _projectFolderPath, ec).generic_string()},
				{"loaded", false},
				{"active", false},
			});
		}
		return out;
	}

	void EditorUI::SwitchToScene(const std::shared_ptr<HexEngine::Scene>& scene)
	{
		auto* sceneManager = HexEngine::g_pEnv->_sceneManager;
		auto current = sceneManager->GetCurrentScene();
		if (current != nullptr && current != scene)
			current->SetFlags(HexEngine::SceneFlags::Disabled);
		scene->SetFlags(HexEngine::SceneFlags::Renderable | HexEngine::SceneFlags::Updateable | HexEngine::SceneFlags::PostProcessingEnabled);
		sceneManager->SetActiveScene(scene);
		SetSelectedEntity(nullptr);
		// Undo/redo resolve entities by name in the active scene; history from
		// another scene would act on the wrong entities.
		_transactions.Clear();
		_entityListRefreshPending = true;
		scene->ForceRebuildPVS();
	}

	bool EditorUI::CreateProjectScene(const std::string& name, bool withSun, bool activate, std::string& error)
	{
		if (_projectFolderPath.empty() || _projectFile == nullptr) { error = "no project is open"; return false; }
		if (_projectLoading) { error = "the project is still loading its scenes - retry shortly"; return false; }
		if (IsPrefabStageActive()) { error = "close the prefab stage first"; return false; }
		if (IsPlayModeActive()) { error = "stop play mode first"; return false; }
		if (name.empty() || name.size() > 100 || name.find_first_of("\\/:*?\"<>|") != std::string::npos || name.front() == '.' || name.front() == ' ' || name.back() == ' ' || name.back() == '.')
		{
			error = "invalid scene name '" + name + "' (it becomes a file name)";
			return false;
		}

		const std::wstring wname(name.begin(), name.end());
		const fs::path scenePath = _projectFolderPath / L"Data" / L"Scenes" / (wname + L".hscene");
		std::error_code ec;
		if (fs::exists(scenePath, ec)) { error = "'" + fs::relative(scenePath, _projectFolderPath, ec).generic_string() + "' already exists (open_scene it)"; return false; }
		for (auto* ssf : _sceneFiles)
			if (ssf && ssf->GetScene() && ssf->GetScene()->GetName() == wname) { error = "a scene named '" + name + "' is already loaded"; return false; }

		auto scene = HexEngine::g_pEnv->_sceneManager->CreateEmptyScene(true, this, true);
		if (auto mainCamera = scene->CreateEntity("MainCamera"); mainCamera != nullptr)
			mainCamera->AddComponent<HexEngine::Camera>();
		if (withSun)
			scene->CreateDefaultSunLight();
		scene->SetName(wname);

		fs::create_directories(scenePath.parent_path(), ec);
		auto* ssf = new HexEngine::SceneSaveFile(scenePath, std::ios::out, scene);
		_projectFile->_scenes.push_back(ssf);
		_sceneFiles.push_back(ssf);

		if (activate)
			SwitchToScene(scene);
		else
			scene->SetFlags(HexEngine::SceneFlags::Disabled);

		// Persist right away: the new file + the project's scene list.
		ssf->Save();
		_projectFile->Save();
		_entityListRefreshPending = true;
		return true;
	}

	// The .hscene a scene reference means, canonical: a bare name is
	// Data/Scenes/<name>.hscene; "Scenes/..." is under Data; "Data/..." under the
	// project. Empty when it would leave the project.
	static fs::path CanonicalSceneCandidate(const fs::path& projectFolder, const std::string& nameOrPath)
	{
		std::error_code ec;
		fs::path candidate = fs::path(std::wstring(nameOrPath.begin(), nameOrPath.end()));
		if (!candidate.has_extension())
			candidate = fs::path(L"Data") / L"Scenes" / (candidate.wstring() + L".hscene");
		if (!candidate.is_absolute())
		{
			const auto first = candidate.begin() != candidate.end() ? candidate.begin()->wstring() : std::wstring();
			candidate = (_wcsicmp(first.c_str(), L"Data") == 0) ? projectFolder / candidate : projectFolder / L"Data" / candidate;
		}
		candidate = fs::weakly_canonical(candidate, ec);
		const std::wstring root = fs::weakly_canonical(projectFolder, ec).wstring();
		if (candidate.wstring().size() <= root.size() || _wcsnicmp(candidate.wstring().c_str(), root.c_str(), root.size()) != 0)
			return {};
		return candidate;
	}

	bool EditorUI::RemoveProjectScene(const std::string& nameOrPath, bool deleteFile, std::string& error)
	{
		if (_projectFolderPath.empty() || _projectFile == nullptr) { error = "no project is open"; return false; }
		if (IsPrefabStageActive()) { error = "close the prefab stage first"; return false; }
		if (IsPlayModeActive()) { error = "stop play mode first"; return false; }

		std::error_code ec;
		auto lower = [](std::wstring s) { std::transform(s.begin(), s.end(), s.begin(), ::towlower); return s; };
		const std::wstring wanted = lower(std::wstring(nameOrPath.begin(), nameOrPath.end()));
		const fs::path candidate = CanonicalSceneCandidate(_projectFolderPath, nameOrPath);
		auto current = HexEngine::g_pEnv->_sceneManager->GetCurrentScene();

		for (auto it = _sceneFiles.begin(); it != _sceneFiles.end(); ++it)
		{
			HexEngine::SceneSaveFile* ssf = *it;
			auto scene = ssf ? ssf->GetScene() : nullptr;
			if (!ssf)
				continue;
			const fs::path abs = fs::weakly_canonical(ssf->GetAbsolutePath(), ec);
			const bool match = (scene && lower(scene->GetName()) == wanted) || lower(abs.wstring()) == lower(candidate.wstring());
			if (!match)
				continue;
			if (scene != nullptr && scene == current)
			{
				error = "that is the active scene: open_scene another one first";
				return false;
			}

			_sceneFiles.erase(it);
			auto& projectScenes = _projectFile->_scenes;
			projectScenes.erase(std::remove(projectScenes.begin(), projectScenes.end(), ssf), projectScenes.end());
			if (scene != nullptr)
				HexEngine::g_pEnv->_sceneManager->UnloadScene(scene.get());
			_projectFile->Save();
			if (deleteFile)
				fs::remove(abs, ec);
			_entityListRefreshPending = true;
			return true;
		}
		error = "no loaded project scene '" + nameOrPath + "' (see list_scenes)";
		return false;
	}

	bool EditorUI::ActivateProjectScene(const std::string& nameOrPath, std::string& error)
	{
		if (_projectFolderPath.empty() || _projectFile == nullptr) { error = "no project is open"; return false; }
		if (_projectLoading) { error = "the project is still loading its scenes - retry once it has finished"; return false; }
		if (IsPrefabStageActive()) { error = "close the prefab stage first"; return false; }
		if (IsPlayModeActive()) { error = "stop play mode first"; return false; }

		std::error_code ec;
		auto lower = [](std::wstring s) { std::transform(s.begin(), s.end(), s.begin(), ::towlower); return s; };
		const std::wstring wanted = lower(std::wstring(nameOrPath.begin(), nameOrPath.end()));
		const fs::path candidate = CanonicalSceneCandidate(_projectFolderPath, nameOrPath);

		// Already loaded: by scene name, or by its file - however the path was
		// written (a loaded scene must never be loaded a second time).
		for (auto* ssf : _sceneFiles)
		{
			auto scene = ssf ? ssf->GetScene() : nullptr;
			if (!scene)
				continue;
			const fs::path abs = fs::weakly_canonical(ssf->GetAbsolutePath(), ec);
			if (lower(scene->GetName()) == wanted || (!candidate.empty() && lower(abs.wstring()) == lower(candidate.wstring())))
			{
				SwitchToScene(scene);
				return true;
			}
		}

		// A .hscene in the project that isn't loaded yet.
		if (candidate.empty() || !fs::exists(candidate, ec) || _wcsicmp(candidate.extension().c_str(), L".hscene") != 0)
		{
			error = "no scene '" + nameOrPath + "' in the project (see list_scenes)";
			return false;
		}

		auto* sceneManager = HexEngine::g_pEnv->_sceneManager;
		auto previous = sceneManager->GetCurrentScene();
		auto scene = sceneManager->CreateEmptyScene(false, this, true);
		// Same order as project load: the scene being filled is the active one.
		sceneManager->SetActiveScene(scene);
		HexEngine::SceneSaveFile loader(candidate, std::ios::in, scene);
		if (!loader.Load())
		{
			sceneManager->SetActiveScene(previous);
			sceneManager->UnloadScene(scene.get());
			error = "loading '" + candidate.string() + "' failed (see engine logs)";
			return false;
		}
		if (scene->GetName().empty())
			scene->SetName(candidate.stem().wstring());

		auto* ssf = new HexEngine::SceneSaveFile(candidate, std::ios::out, scene);
		_projectFile->_scenes.push_back(ssf);
		_sceneFiles.push_back(ssf);
		_projectFile->Save();

		sceneManager->SetActiveScene(previous);
		SwitchToScene(scene);
		return true;
	}
}
