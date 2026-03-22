
#include "EditorUI.hpp"
#include "../Editor.hpp"
#include "Actions\ProjectManager.hpp"
#include "Actions\ProjectGenerator.hpp"
#include "Actions\Settings.hpp"
#include "Actions\Terrain.hpp"
#include <HexEngine.Core\FileSystem\SceneSaveFile.hpp>
#include <HexEngine.Core\Scene\SceneFramingUtils.hpp>

#include "Gadgets\ScaleGadget.hpp"
#include "Gadgets\PositionGadget.hpp"
#include "Gadgets\DuplicateGadget.hpp"

namespace HexEditor
{
	namespace
	{
		json BuildPrefabEntitySnapshotRecursive(HexEngine::Entity* entity, HexEngine::JsonFile& serializer, bool canonicalRootName)
		{
			if (entity == nullptr)
				return json::object();

			json serializedEntities = json::object();
			entity->Serialize(serializedEntities, &serializer);

			json entityData = json::object();
			if (serializedEntities.find(entity->GetName()) != serializedEntities.end())
			{
				entityData = serializedEntities[entity->GetName()];
			}

			entityData.erase("prefab");
			entityData["entityName"] = canonicalRootName ? "__PREFAB_ROOT__" : entity->GetName();

			if (entityData.find("flags") != entityData.end())
			{
				uint64_t flags = entityData["flags"].get<uint64_t>();
				flags &= ~static_cast<uint64_t>(HexEngine::EntityFlags::SelectedInEditor);
				entityData["flags"] = flags;
			}

			if (entityData.find("components") != entityData.end() && entityData["components"].is_array())
			{
				auto& components = entityData["components"];
				std::sort(components.begin(), components.end(),
					[](const json& a, const json& b)
					{
						return a.value("name", std::string()) < b.value("name", std::string());
					});
			}

			std::vector<HexEngine::Entity*> children = entity->GetChildren();
			std::sort(children.begin(), children.end(),
				[](const HexEngine::Entity* a, const HexEngine::Entity* b)
				{
					if (a == nullptr || b == nullptr)
						return a < b;
					return a->GetName() < b->GetName();
				});

			auto& childSnapshots = entityData["children"];
			childSnapshots = json::array();
			for (auto* child : children)
			{
				childSnapshots.push_back(BuildPrefabEntitySnapshotRecursive(child, serializer, false));
			}

			return entityData;
		}

		constexpr const char* kPrefabOverrideTransformPosition = "transform.position";
		constexpr const char* kPrefabOverrideTransformRotation = "transform.rotation";
		constexpr const char* kPrefabOverrideTransformScale = "transform.scale";
		constexpr const char* kPrefabOverrideStaticMeshMesh = "staticMesh.mesh";
		constexpr const char* kPrefabOverrideStaticMeshMaterial = "staticMesh.material";
		constexpr const char* kPrefabOverrideStaticMeshUVScale = "staticMesh.uvScale";
		constexpr const char* kPrefabOverrideStaticMeshShadowCullMode = "staticMesh.shadowCullMode";
		constexpr const char* kPrefabOverrideStaticMeshOffsetPosition = "staticMesh.offsetPosition";

		struct PrefabEntityOverrideState
		{
			std::unordered_set<std::string> overridePaths;
			math::Vector3 position = math::Vector3::Zero;
			math::Quaternion rotation = math::Quaternion::Identity;
			math::Vector3 scale = math::Vector3(1.0f);
			fs::path meshPath;
			bool hasMeshPath = false;
			fs::path materialPath;
			bool hasMaterialPath = false;
			math::Vector2 uvScale = math::Vector2(1.0f, 1.0f);
			HexEngine::CullingMode shadowCullMode = HexEngine::CullingMode::FrontFace;
			math::Vector3 offsetPosition = math::Vector3::Zero;
		};

		void CollectPrefabOverrideStateRecursive(
			HexEngine::Entity* entity,
			const std::string& pathKey,
			std::unordered_map<std::string, PrefabEntityOverrideState>& outStates)
		{
			if (entity == nullptr)
				return;

			const auto& entityOverrides = entity->GetPrefabPropertyOverrides();
			if (!entityOverrides.empty())
			{
				PrefabEntityOverrideState state;
				state.overridePaths = entityOverrides;
				state.position = entity->GetPosition();
				state.rotation = entity->GetRotation();
				state.scale = entity->GetScale();

				if (auto* staticMesh = entity->GetComponent<HexEngine::StaticMeshComponent>(); staticMesh != nullptr)
				{
					if (auto mesh = staticMesh->GetMesh(); mesh != nullptr)
					{
						state.meshPath = mesh->GetFileSystemPath();
						state.hasMeshPath = !state.meshPath.empty();
					}

					if (auto material = staticMesh->GetMaterial(); material != nullptr)
					{
						state.materialPath = material->GetFileSystemPath();
						state.hasMaterialPath = !state.materialPath.empty();
					}

					state.uvScale = staticMesh->GetUVScale();
					state.shadowCullMode = staticMesh->GetShadowCullMode();
					state.offsetPosition = staticMesh->GetOffsetPosition();
				}

				outStates[pathKey] = std::move(state);
			}

			std::unordered_map<std::string, int32_t> siblingNameCount;
			for (auto* child : entity->GetChildren())
			{
				if (child == nullptr)
					continue;

				const int32_t siblingIndex = siblingNameCount[child->GetName()]++;
				const std::string childPath = pathKey + "/" + child->GetName() + "#" + std::to_string(siblingIndex);
				CollectPrefabOverrideStateRecursive(child, childPath, outStates);
			}
		}

		void ApplyPrefabOverrideStateRecursive(
			HexEngine::Entity* entity,
			const std::string& pathKey,
			const std::unordered_map<std::string, PrefabEntityOverrideState>& overrideStates)
		{
			if (entity == nullptr)
				return;

			entity->ClearPrefabPropertyOverrides();

			const auto stateIt = overrideStates.find(pathKey);
			if (stateIt != overrideStates.end())
			{
				const auto& state = stateIt->second;
				for (const auto& overridePath : state.overridePaths)
				{
					entity->MarkPrefabPropertyOverride(overridePath);
				}

				if (state.overridePaths.find(kPrefabOverrideTransformPosition) != state.overridePaths.end())
					entity->SetPosition(state.position);

				if (state.overridePaths.find(kPrefabOverrideTransformRotation) != state.overridePaths.end())
					entity->SetRotation(state.rotation);

				if (state.overridePaths.find(kPrefabOverrideTransformScale) != state.overridePaths.end())
					entity->SetScale(state.scale);

				if (state.overridePaths.find(kPrefabOverrideStaticMeshMesh) != state.overridePaths.end())
				{
					if (auto* staticMesh = entity->GetComponent<HexEngine::StaticMeshComponent>(); staticMesh != nullptr)
					{
						if (state.hasMeshPath && !state.meshPath.empty())
						{
							if (auto mesh = HexEngine::Mesh::Create(state.meshPath); mesh != nullptr)
							{
								staticMesh->SetMesh(mesh);
							}
						}
					}
				}

				if (state.overridePaths.find(kPrefabOverrideStaticMeshMaterial) != state.overridePaths.end())
				{
					if (auto* staticMesh = entity->GetComponent<HexEngine::StaticMeshComponent>(); staticMesh != nullptr)
					{
						if (state.hasMaterialPath && !state.materialPath.empty())
						{
							if (auto material = HexEngine::Material::Create(state.materialPath); material != nullptr)
							{
								staticMesh->SetMaterial(material);
							}
						}
					}
				}

				if (state.overridePaths.find(kPrefabOverrideStaticMeshUVScale) != state.overridePaths.end())
				{
					if (auto* staticMesh = entity->GetComponent<HexEngine::StaticMeshComponent>(); staticMesh != nullptr)
					{
						staticMesh->SetUVScale(state.uvScale);
					}
				}

				if (state.overridePaths.find(kPrefabOverrideStaticMeshShadowCullMode) != state.overridePaths.end())
				{
					if (auto* staticMesh = entity->GetComponent<HexEngine::StaticMeshComponent>(); staticMesh != nullptr)
					{
						staticMesh->SetShadowCullMode(state.shadowCullMode);
					}
				}

				if (state.overridePaths.find(kPrefabOverrideStaticMeshOffsetPosition) != state.overridePaths.end())
				{
					if (auto* staticMesh = entity->GetComponent<HexEngine::StaticMeshComponent>(); staticMesh != nullptr)
					{
						staticMesh->SetOffsetPosition(state.offsetPosition);
					}
				}
			}

			std::unordered_map<std::string, int32_t> siblingNameCount;
			for (auto* child : entity->GetChildren())
			{
				if (child == nullptr)
					continue;

				const int32_t siblingIndex = siblingNameCount[child->GetName()]++;
				const std::string childPath = pathKey + "/" + child->GetName() + "#" + std::to_string(siblingIndex);
				ApplyPrefabOverrideStateRecursive(child, childPath, overrideStates);
			}
		}

		bool ComponentEntryChanged(const json& beforeComponents, const json& afterComponents, const std::string& componentName)
		{
			if (!beforeComponents.is_array() || !afterComponents.is_array())
				return false;

			auto findByName = [&componentName](const json& components) -> const json*
			{
				for (const auto& component : components)
				{
					if (component.value("name", std::string()) == componentName)
						return &component;
				}
				return nullptr;
			};

			const json* before = findByName(beforeComponents);
			const json* after = findByName(afterComponents);
			if (before == nullptr || after == nullptr)
				return false;

			return *before != *after;
		}

		const json* FindComponentEntryByName(const json& components, const std::string& componentName)
		{
			if (!components.is_array())
				return nullptr;

			for (const auto& component : components)
			{
				if (component.value("name", std::string()) == componentName)
					return &component;
			}

			return nullptr;
		}

		std::string ExtractStaticMeshPath(const json& staticMeshComponent)
		{
			auto meshIt = staticMeshComponent.find("mesh");
			if (meshIt == staticMeshComponent.end() || !meshIt->is_object() || meshIt->empty())
				return {};

			return meshIt->items().begin().key();
		}

		std::string ExtractStaticMeshMaterialPath(const json& staticMeshComponent)
		{
			auto meshIt = staticMeshComponent.find("mesh");
			if (meshIt == staticMeshComponent.end() || !meshIt->is_object() || meshIt->empty())
				return {};

			const auto& meshEntry = meshIt->items().begin().value();
			auto materialsIt = meshEntry.find("materials");
			if (materialsIt == meshEntry.end() || !materialsIt->is_array() || materialsIt->empty())
				return {};

			if (!(*materialsIt)[0].is_string())
				return {};

			return (*materialsIt)[0].get<std::string>();
		}

		struct StaticMeshComponentDiff
		{
			bool meshChanged = false;
			bool materialChanged = false;
			bool uvScaleChanged = false;
			bool shadowCullModeChanged = false;
			bool offsetPositionChanged = false;

			bool HasAnyChange() const
			{
				return meshChanged || materialChanged || uvScaleChanged || shadowCullModeChanged || offsetPositionChanged;
			}
		};

		StaticMeshComponentDiff BuildStaticMeshComponentDiff(const json& beforeComponents, const json& afterComponents)
		{
			StaticMeshComponentDiff diff;

			const auto* before = FindComponentEntryByName(beforeComponents, "StaticMeshComponent");
			const auto* after = FindComponentEntryByName(afterComponents, "StaticMeshComponent");
			if (before == nullptr || after == nullptr)
				return diff;

			diff.meshChanged = ExtractStaticMeshPath(*before) != ExtractStaticMeshPath(*after);
			diff.materialChanged = ExtractStaticMeshMaterialPath(*before) != ExtractStaticMeshMaterialPath(*after);

			auto getFieldOrNull = [](const json& component, const char* key) -> json
			{
				auto it = component.find(key);
				return it != component.end() ? *it : json();
			};

			diff.uvScaleChanged = getFieldOrNull(*before, "_uvScale") != getFieldOrNull(*after, "_uvScale");
			diff.shadowCullModeChanged = getFieldOrNull(*before, "_shadowCullingMode") != getFieldOrNull(*after, "_shadowCullingMode");
			diff.offsetPositionChanged = getFieldOrNull(*before, "_offsetPosition") != getFieldOrNull(*after, "_offsetPosition");
			return diff;
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

	void EditorUI::EnsurePrefabStageCameraAndLighting(const std::shared_ptr<HexEngine::Scene>& scene)
	{
		if (scene == nullptr)
			return;

		if (scene->GetMainCamera() == nullptr)
		{
			auto* cameraEntity = scene->CreateEntity("__PrefabEditorCamera", math::Vector3(0.0f, 2.0f, -8.0f));
			if (cameraEntity != nullptr)
			{
				cameraEntity->SetLayer(HexEngine::Layer::Camera);
				cameraEntity->SetFlag(HexEngine::EntityFlags::DoNotSave);
				auto* camera = cameraEntity->AddComponent<HexEngine::Camera>();
				scene->SetMainCamera(camera);
			}
		}

		if (scene->GetSunLight() == nullptr)
		{
			scene->CreateDefaultSunLight();
			if (auto* sun = scene->GetSunLight(); sun != nullptr && sun->GetEntity() != nullptr)
			{
				sun->GetEntity()->SetFlag(HexEngine::EntityFlags::DoNotSave);
			}
		}
	}

	void EditorUI::FramePrefabStageCamera(const std::shared_ptr<HexEngine::Scene>& scene)
	{
		if (scene == nullptr)
			return;

		auto* camera = scene->GetMainCamera();
		if (camera == nullptr)
			return;

		if (!HexEngine::SceneFramingUtils::FrameCameraToSceneBounds(scene.get(), camera, true))
			return;

		if (auto* pvs = camera->GetPVS(); pvs != nullptr)
		{
			pvs->ForceRebuild();
		}
	}

	HexEngine::Entity* EditorUI::FindPrefabRootInScene(const std::shared_ptr<HexEngine::Scene>& scene, const std::string& preferredName) const
	{
		if (scene == nullptr)
			return nullptr;

		if (!preferredName.empty())
		{
			if (auto* preferred = scene->GetEntityByName(preferredName); preferred != nullptr && preferred->GetParent() == nullptr)
			{
				return preferred;
			}
		}

		for (const auto& bySignature : scene->GetEntities())
		{
			for (auto* entity : bySignature.second)
			{
				if (entity != nullptr && entity->GetParent() == nullptr && !entity->HasFlag(HexEngine::EntityFlags::DoNotSave))
				{
					return entity;
				}
			}
		}

		return nullptr;
	}

	void EditorUI::CollectEntityHierarchy(HexEngine::Entity* root, std::vector<HexEngine::Entity*>& outEntities) const
	{
		if (root == nullptr || root->IsPendingDeletion())
			return;

		outEntities.push_back(root);

		for (auto* child : root->GetChildren())
		{
			CollectEntityHierarchy(child, outEntities);
		}
	}

	HexEngine::Entity* EditorUI::CloneEntityHierarchyToScene(
		HexEngine::Scene* targetScene,
		HexEngine::Entity* sourceEntity,
		HexEngine::Entity* targetParent,
		const fs::path& prefabSourcePath,
		const std::string& prefabRootName,
		bool isRootInstance)
	{
		if (targetScene == nullptr || sourceEntity == nullptr)
			return nullptr;

		auto* clonedEntity = targetScene->CloneEntity(sourceEntity, false);
		if (clonedEntity == nullptr)
			return nullptr;

		if (targetParent != nullptr)
		{
			clonedEntity->SetParent(targetParent);
		}

		if (!prefabSourcePath.empty())
		{
			clonedEntity->SetPrefabSource(prefabSourcePath, prefabRootName, isRootInstance);
		}
		else
		{
			clonedEntity->ClearPrefabSource();
		}

		for (auto* child : sourceEntity->GetChildren())
		{
			CloneEntityHierarchyToScene(targetScene, child, clonedEntity, prefabSourcePath, prefabRootName, false);
		}

		return clonedEntity;
	}

	HexEngine::Entity* EditorUI::FindPrefabInstanceRoot(HexEngine::Entity* entity) const
	{
		if (entity == nullptr || !entity->IsPrefabInstance())
			return nullptr;

		for (auto* current = entity; current != nullptr; current = current->GetParent())
		{
			if (current->IsPrefabInstanceRoot())
				return current;
		}

		return nullptr;
	}

	void EditorUI::RefreshInspectorForPrefabInstance(HexEngine::Entity* changedEntity)
	{
		if (changedEntity == nullptr || _rightDock == nullptr)
			return;

		auto* inspecting = _rightDock->GetInspectingEntity();
		if (inspecting == nullptr)
			return;

		auto* changedRoot = FindPrefabInstanceRoot(changedEntity);
		auto* inspectingRoot = FindPrefabInstanceRoot(inspecting);
		if (changedRoot != nullptr && changedRoot == inspectingRoot)
		{
			_rightDock->InspectEntity(inspecting);
		}
	}

	void EditorUI::MarkPrefabOverride(HexEngine::Entity* entity, const std::string& propertyPath)
	{
		if (entity == nullptr || propertyPath.empty() || !entity->IsPrefabInstance())
			return;

		entity->MarkPrefabPropertyOverride(propertyPath);
		RefreshInspectorForPrefabInstance(entity);
	}

	bool EditorUI::PropagateAppliedPrefabToInstances(
		const fs::path& prefabPath,
		HexEngine::Entity* appliedSourceInstance,
		HexEngine::Entity** outReplacementForAppliedInstance)
	{
		if (outReplacementForAppliedInstance != nullptr)
			*outReplacementForAppliedInstance = nullptr;

		if (prefabPath.empty())
			return false;

		auto* sceneManager = HexEngine::g_pEnv->_sceneManager;
		if (sceneManager == nullptr)
			return false;

		auto prefabScene = sceneManager->CreateEmptyScene(false, nullptr, false);
		HexEngine::SceneSaveFile loadFile(prefabPath, std::ios::in, prefabScene, HexEngine::SceneFileFlags::IsPrefab);
		if (!loadFile.Load(prefabScene))
		{
			LOG_WARN("Failed to reload prefab '%s' for instance propagation.", prefabPath.string().c_str());
			return false;
		}

		struct InstanceRefreshTarget
		{
			HexEngine::Entity* instanceRoot = nullptr;
			math::Vector3 rootPosition = math::Vector3::Zero;
			math::Quaternion rootRotation = math::Quaternion::Identity;
			math::Vector3 rootScale = math::Vector3(1.0f);
		};

		std::vector<InstanceRefreshTarget> targets;
		for (const auto& scene : sceneManager->GetAllScenes())
		{
			if (scene == nullptr)
				continue;

			for (const auto& bySignature : scene->GetEntities())
			{
				for (auto* entity : bySignature.second)
				{
					if (entity == nullptr || entity->IsPendingDeletion())
						continue;

					if (!entity->IsPrefabInstanceRoot() || entity->GetPrefabSourcePath() != prefabPath)
						continue;

					const bool isAppliedSource = (entity == appliedSourceInstance);
					if (isAppliedSource)
					{
						if (outReplacementForAppliedInstance != nullptr)
							*outReplacementForAppliedInstance = entity;
						continue;
					}

					InstanceRefreshTarget target;
					target.instanceRoot = entity;
					target.rootPosition = entity->GetPosition();
					target.rootRotation = entity->GetRotation();
					target.rootScale = entity->GetScale();
					targets.push_back(target);
				}
			}
		}

		bool replacedAny = false;
		for (const auto& target : targets)
		{
			auto* instanceRoot = target.instanceRoot;
			if (instanceRoot == nullptr || instanceRoot->IsPendingDeletion())
				continue;

			auto* sourceRoot = FindPrefabRootInScene(prefabScene, instanceRoot->GetPrefabRootEntityName());
			if (sourceRoot == nullptr)
			{
				LOG_WARN("Could not find source root '%s' while propagating prefab '%s'.",
					instanceRoot->GetPrefabRootEntityName().c_str(), prefabPath.string().c_str());
				continue;
			}

			auto* targetScene = instanceRoot->GetScene();
			if (targetScene == nullptr)
				continue;

			const std::string desiredName = instanceRoot->GetName();
			auto* parent = instanceRoot->GetParent();

			std::unordered_map<std::string, PrefabEntityOverrideState> overrideStates;
			CollectPrefabOverrideStateRecursive(instanceRoot, "__root__", overrideStates);

			const bool wasInspected = (_rightDock != nullptr && _rightDock->GetInspectingEntity() == instanceRoot);
			targetScene->DestroyEntity(instanceRoot);

			auto* newRoot = CloneEntityHierarchyToScene(targetScene, sourceRoot, parent, prefabPath, sourceRoot->GetName(), true);
			if (newRoot == nullptr)
			{
				LOG_WARN("Failed to rebuild prefab instance while propagating '%s'.", prefabPath.string().c_str());
				continue;
			}

			if (!desiredName.empty() && desiredName != newRoot->GetName())
			{
				std::string finalName;
				targetScene->RenameEntity(newRoot, desiredName, &finalName);
			}

			ApplyPrefabOverrideStateRecursive(newRoot, "__root__", overrideStates);
			newRoot->SetPosition(target.rootPosition);
			newRoot->SetRotation(target.rootRotation);
			newRoot->SetScale(target.rootScale);

			targetScene->ForceRebuildPVS();
			replacedAny = true;

			if (wasInspected && _rightDock != nullptr)
			{
				_rightDock->InspectEntity(newRoot);
			}

		}

		if (replacedAny && _entityList != nullptr)
		{
			_entityList->RefreshList();
		}

		return replacedAny;
	}

	bool EditorUI::IsPrefabInstanceEntity(HexEngine::Entity* entity) const
	{
		return entity != nullptr && entity->IsPrefabInstance();
	}

	bool EditorUI::IsPrefabInstanceRootEntity(HexEngine::Entity* entity) const
	{
		return entity != nullptr && entity->IsPrefabInstanceRoot();
	}

	bool EditorUI::HasPrefabInstanceOverrides(HexEngine::Entity* entity) const
	{
		if (entity == nullptr || !entity->IsPrefabInstance())
			return false;

		auto* root = entity;
		while (root != nullptr && !root->IsPrefabInstanceRoot())
		{
			root = root->GetParent();
		}

		if (root == nullptr || !root->IsPrefabInstanceRoot())
			return false;

		const fs::path prefabPath = root->GetPrefabSourcePath();
		if (prefabPath.empty())
			return false;

		auto* sceneManager = HexEngine::g_pEnv->_sceneManager;
		if (sceneManager == nullptr)
			return false;

		auto prefabScene = sceneManager->CreateEmptyScene(false, nullptr, false);
		HexEngine::SceneSaveFile loadFile(prefabPath, std::ios::in, prefabScene, HexEngine::SceneFileFlags::IsPrefab);
		if (!loadFile.Load(prefabScene))
		{
			LOG_WARN("Failed to load prefab '%s' while checking instance overrides.", prefabPath.string().c_str());
			return false;
		}

		auto* sourceRoot = FindPrefabRootInScene(prefabScene, root->GetPrefabRootEntityName());
		if (sourceRoot == nullptr)
			return false;

		HexEngine::JsonFile serializer(fs::path("temp_prefab_compare.json"), std::ios::out);
		const json currentSnapshot = BuildPrefabEntitySnapshotRecursive(root, serializer, true);
		const json sourceSnapshot = BuildPrefabEntitySnapshotRecursive(sourceRoot, serializer, true);
		return currentSnapshot != sourceSnapshot;
	}

	HexEngine::Entity* EditorUI::RevertPrefabInstance(HexEngine::Entity* entity)
	{
		if (entity == nullptr || !entity->IsPrefabInstanceRoot())
			return nullptr;

		if (IsPrefabStageActive())
		{
			LOG_WARN("Cannot revert prefab instance while prefab stage is active.");
			return nullptr;
		}

		const fs::path prefabPath = entity->GetPrefabSourcePath();
		if (prefabPath.empty())
		{
			LOG_WARN("Cannot revert prefab instance '%s' because it has no source path.", entity->GetName().c_str());
			return nullptr;
		}

		auto* sceneManager = HexEngine::g_pEnv->_sceneManager;
		if (sceneManager == nullptr)
			return nullptr;

		auto prefabScene = sceneManager->CreateEmptyScene(false);
		HexEngine::SceneSaveFile loadFile(prefabPath, std::ios::in, prefabScene, HexEngine::SceneFileFlags::IsPrefab);
		if (!loadFile.Load(prefabScene))
		{
			LOG_WARN("Failed to load prefab '%s' while reverting instance '%s'.", prefabPath.string().c_str(), entity->GetName().c_str());
			return nullptr;
		}

		const std::string prefabRootName = entity->GetPrefabRootEntityName();
		auto* sourceRoot = FindPrefabRootInScene(prefabScene, prefabRootName);
		if (sourceRoot == nullptr)
		{
			LOG_WARN("Failed to find prefab root '%s' in '%s'.", prefabRootName.c_str(), prefabPath.string().c_str());
			return nullptr;
		}

		auto* targetScene = entity->GetScene();
		auto* parent = entity->GetParent();
		const std::string desiredInstanceName = entity->GetName();

		targetScene->DestroyEntity(entity);

		auto* newRoot = CloneEntityHierarchyToScene(targetScene, sourceRoot, parent, prefabPath, sourceRoot->GetName(), true);
		if (newRoot == nullptr)
		{
			LOG_WARN("Failed to recreate prefab instance from '%s'.", prefabPath.string().c_str());
			return nullptr;
		}

		if (!desiredInstanceName.empty() && desiredInstanceName != newRoot->GetName())
		{
			std::string finalName;
			targetScene->RenameEntity(newRoot, desiredInstanceName, &finalName);
		}

		if (_entityList != nullptr)
		{
			_entityList->RefreshList();
		}

		targetScene->ForceRebuildPVS();
		LOG_INFO("Reverted prefab instance '%s' from '%s'.", newRoot->GetName().c_str(), prefabPath.string().c_str());
		return newRoot;
	}

	bool EditorUI::ApplyPrefabInstanceToPrefabAsset(HexEngine::Entity* entity)
	{
		if (entity == nullptr || !entity->IsPrefabInstanceRoot())
			return false;

		if (IsPrefabStageActive())
		{
			LOG_WARN("Cannot apply prefab instance while prefab stage is active.");
			return false;
		}

		const fs::path prefabPath = entity->GetPrefabSourcePath();
		if (prefabPath.empty())
		{
			LOG_WARN("Cannot apply prefab instance '%s' because it has no source path.", entity->GetName().c_str());
			return false;
		}

		auto* sceneManager = HexEngine::g_pEnv->_sceneManager;
		if (sceneManager == nullptr)
			return false;

		auto tempScene = sceneManager->CreateEmptyScene(false);
		auto* tempRoot = CloneEntityHierarchyToScene(tempScene.get(), entity, nullptr, fs::path(), std::string(), true);
		if (tempRoot == nullptr)
		{
			LOG_WARN("Failed to clone prefab instance '%s' for apply operation.", entity->GetName().c_str());
			return false;
		}

		const std::string desiredRootName = entity->GetPrefabRootEntityName().empty() ? tempRoot->GetName() : entity->GetPrefabRootEntityName();
		if (!desiredRootName.empty() && tempRoot->GetName() != desiredRootName)
		{
			std::string finalName;
			tempScene->RenameEntity(tempRoot, desiredRootName, &finalName);
		}

		std::vector<HexEngine::Entity*> entitiesToSave;
		CollectEntityHierarchy(tempRoot, entitiesToSave);

		HexEngine::SceneSaveFile saveFile(prefabPath, std::ios::out | std::ios::trunc, tempScene, HexEngine::SceneFileFlags::IsPrefab);
		if (!saveFile.Save(entitiesToSave))
		{
			LOG_WARN("Failed to save prefab '%s' from instance '%s'.", prefabPath.string().c_str(), entity->GetName().c_str());
			return false;
		}

		// Keep explicit per-instance overrides sticky across apply operations.
		// This preserves user-authored local values when other instances later apply different prefab changes.
		RefreshInspectorForPrefabInstance(entity);

		PropagateAppliedPrefabToInstances(prefabPath, entity, nullptr);

		LOG_INFO("Applied prefab instance '%s' to asset '%s'.", entity->GetName().c_str(), prefabPath.string().c_str());
		return true;
	}

	bool EditorUI::OpenPrefabStage(const fs::path& prefabPath)
	{
		if (prefabPath.empty() || prefabPath.extension() != ".hprefab")
			return false;

		if (_integrator.GetState() == GameTestState::Started)
		{
			LOG_WARN("Cannot open prefab stage while game is running.");
			return false;
		}

		if (_prefabStage.active && _prefabStage.prefabPath == prefabPath)
			return true;

		if (_prefabStage.active)
		{
			ClosePrefabStage(true);
		}

		auto* sceneManager = HexEngine::g_pEnv->_sceneManager;
		if (sceneManager == nullptr)
			return false;

		auto activeScene = sceneManager->GetCurrentScene();
		if (activeScene == nullptr)
		{
			LOG_WARN("Cannot open prefab stage because there is no active scene.");
			return false;
		}

		if (_prefabStage.stageScene == nullptr)
		{
			_prefabStage.stageScene = sceneManager->CreateEmptyScene(false, this, true);
			if (_prefabStage.stageScene == nullptr)
				return false;

			_prefabStage.stageScene->SetFlags(HexEngine::SceneFlags::Disabled | HexEngine::SceneFlags::Utility);
		}
		else
		{
			_prefabStage.stageScene->Destroy();
			_prefabStage.stageScene->CreateEmpty(false);
			_prefabStage.stageScene->SetFlags(HexEngine::SceneFlags::Disabled | HexEngine::SceneFlags::Utility);
		}

		_prefabStage.prefabPath = prefabPath;
		_prefabStage.previousActiveScene = activeScene;
		_prefabStage.previousSceneFlags.clear();

		HexEngine::SceneSaveFile loadFile(prefabPath, std::ios::in, _prefabStage.stageScene, HexEngine::SceneFileFlags::IsPrefab);
		if (!loadFile.Load(_prefabStage.stageScene))
		{
			LOG_WARN("Failed to open prefab stage for '%s'", prefabPath.string().c_str());
			_prefabStage.prefabPath.clear();
			_prefabStage.previousActiveScene.reset();
			return false;
		}

		_prefabStage.stageScene->SetName(L"Prefab: " + prefabPath.stem().wstring());

		const auto& allScenes = sceneManager->GetAllScenes();
		for (const auto& scene : allScenes)
		{
			if (!scene || scene == _prefabStage.stageScene)
				continue;

			_prefabStage.previousSceneFlags.emplace_back(scene, scene->GetFlags());
			scene->SetFlags(HexEngine::SceneFlags::Disabled);
		}

		_prefabStage.stageScene->SetFlags(HexEngine::SceneFlags::Updateable | HexEngine::SceneFlags::Renderable | HexEngine::SceneFlags::PostProcessingEnabled);
		sceneManager->SetActiveScene(_prefabStage.stageScene);

		EnsurePrefabStageCameraAndLighting(_prefabStage.stageScene);
		FramePrefabStageCamera(_prefabStage.stageScene);

		_prefabStage.active = true;

		if (_rightDock != nullptr)
		{
			_rightDock->InspectEntity(nullptr);
		}

		if (_entityList != nullptr)
		{
			_entityList->RefreshList();
		}

		LOG_INFO("Opened prefab stage: %s", prefabPath.string().c_str());
		return true;
	}

	bool EditorUI::SavePrefabStage()
	{
		if (!_prefabStage.active || _prefabStage.stageScene == nullptr || _prefabStage.prefabPath.empty())
			return false;

		std::vector<HexEngine::Entity*> entitiesToSave;
		for (const auto& bySignature : _prefabStage.stageScene->GetEntities())
		{
			for (auto* entity : bySignature.second)
			{
				if (entity != nullptr && !entity->HasFlag(HexEngine::EntityFlags::DoNotSave))
				{
					entitiesToSave.push_back(entity);
				}
			}
		}

		HexEngine::SceneSaveFile saveFile(_prefabStage.prefabPath, std::ios::out | std::ios::trunc, _prefabStage.stageScene, HexEngine::SceneFileFlags::IsPrefab);
		if (!saveFile.Save(entitiesToSave))
		{
			LOG_WARN("Failed to save prefab stage: %s", _prefabStage.prefabPath.string().c_str());
			return false;
		}

		LOG_INFO("Saved prefab: %s", _prefabStage.prefabPath.string().c_str());
		return true;
	}

	bool EditorUI::ClosePrefabStage(bool saveChanges)
	{
		if (!_prefabStage.active)
			return false;

		if (saveChanges)
		{
			SavePrefabStage();
		}

		auto* sceneManager = HexEngine::g_pEnv->_sceneManager;
		if (sceneManager != nullptr)
		{
			for (auto& [scene, flags] : _prefabStage.previousSceneFlags)
			{
				if (scene != nullptr)
				{
					scene->SetFlags(flags);
				}
			}

			if (_prefabStage.stageScene != nullptr)
			{
				_prefabStage.stageScene->SetFlags(HexEngine::SceneFlags::Disabled | HexEngine::SceneFlags::Utility);
			}

			if (_prefabStage.previousActiveScene != nullptr)
			{
				sceneManager->SetActiveScene(_prefabStage.previousActiveScene);
			}
			else
			{
				const auto& scenes = sceneManager->GetAllScenes();
				for (const auto& scene : scenes)
				{
					if (scene != nullptr && scene != _prefabStage.stageScene && !HEX_HASFLAG(scene->GetFlags(), HexEngine::SceneFlags::Utility))
					{
						sceneManager->SetActiveScene(scene);
						break;
					}
				}
			}
		}

		_prefabStage.active = false;
		_prefabStage.prefabPath.clear();
		_prefabStage.previousActiveScene.reset();
		_prefabStage.previousSceneFlags.clear();

		if (_rightDock != nullptr)
		{
			_rightDock->InspectEntity(nullptr);
		}

		if (_entityList != nullptr)
		{
			_entityList->RefreshList();
		}

		LOG_INFO("Exited prefab stage");
		return true;
	}

	bool EditorUI::IsPrefabStageActive() const
	{
		return _prefabStage.active;
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
		HexEngine::Entity* light = HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->CreateEntity("PointLight");

		auto pointLight = light->AddComponent<HexEngine::PointLight>();
		pointLight->SetRadius(100.0f);
		pointLight->SetLightStength(4.0f);

		RecordEntityCreated(light);
	}

	void EditorUI::OnAddSpotLight()
	{
		HexEngine::Entity* light = HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->CreateEntity("SpotLight");

		auto pointLight = light->AddComponent<HexEngine::SpotLight>();
		pointLight->SetLightStength(4.0f);

		RecordEntityCreated(light);
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

		TryBeginPendingComponentEdit(event, data);

		const bool uiHandled = UIManager::OnInputEvent(event, data) == false;

		TryCommitPendingComponentEdit(event, data);

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
				if (HasMatchingComponentLayout(_pendingComponentEditBefore.components, afterSnapshot.components) &&
					_pendingComponentEditBefore.components != afterSnapshot.components)
				{
					_transactions.Push(std::make_unique<ComponentPropertyTransaction>(_pendingComponentEditBefore, afterSnapshot));

					if (ComponentEntryChanged(_pendingComponentEditBefore.components, afterSnapshot.components, "Transform"))
					{
						MarkPrefabOverride(entity, kPrefabOverrideTransformPosition);
						MarkPrefabOverride(entity, kPrefabOverrideTransformRotation);
						MarkPrefabOverride(entity, kPrefabOverrideTransformScale);
					}

					if (ComponentEntryChanged(_pendingComponentEditBefore.components, afterSnapshot.components, "StaticMeshComponent"))
					{
						const auto staticMeshDiff = BuildStaticMeshComponentDiff(_pendingComponentEditBefore.components, afterSnapshot.components);
						if (staticMeshDiff.meshChanged)
							MarkPrefabOverride(entity, kPrefabOverrideStaticMeshMesh);
						if (staticMeshDiff.materialChanged)
							MarkPrefabOverride(entity, kPrefabOverrideStaticMeshMaterial);
						if (staticMeshDiff.uvScaleChanged)
							MarkPrefabOverride(entity, kPrefabOverrideStaticMeshUVScale);
						if (staticMeshDiff.shadowCullModeChanged)
							MarkPrefabOverride(entity, kPrefabOverrideStaticMeshShadowCullMode);
						if (staticMeshDiff.offsetPositionChanged)
							MarkPrefabOverride(entity, kPrefabOverrideStaticMeshOffsetPosition);

						// Defensive fallback if serialization layout changes and per-field extraction misses it.
						if (!staticMeshDiff.HasAnyChange())
						{
							MarkPrefabOverride(entity, kPrefabOverrideStaticMeshMesh);
							MarkPrefabOverride(entity, kPrefabOverrideStaticMeshMaterial);
							MarkPrefabOverride(entity, kPrefabOverrideStaticMeshUVScale);
							MarkPrefabOverride(entity, kPrefabOverrideStaticMeshShadowCullMode);
							MarkPrefabOverride(entity, kPrefabOverrideStaticMeshOffsetPosition);
						}
					}
				}
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

	void EditorUI::RecordEntityPositionChange(HexEngine::Entity* entity, const math::Vector3& before, const math::Vector3& after)
	{
		if (entity == nullptr || before == after)
			return;

		_transactions.Push(std::make_unique<PositionTransaction>(entity->GetName(), before, after));
		MarkPrefabOverride(entity, kPrefabOverrideTransformPosition);
	}

	void EditorUI::RecordEntityScaleChange(HexEngine::Entity* entity, const math::Vector3& before, const math::Vector3& after)
	{
		if (entity == nullptr || before == after)
			return;

		_transactions.Push(std::make_unique<ScaleTransaction>(entity->GetName(), before, after));
		MarkPrefabOverride(entity, kPrefabOverrideTransformScale);
	}

	void EditorUI::RecordStaticMeshMaterialChange(HexEngine::Entity* entity, const fs::path& before, const fs::path& after)
	{
		if (entity == nullptr || before == after)
			return;

		_transactions.Push(std::make_unique<MaterialAssignmentTransaction>(entity->GetName(), before, after));
		MarkPrefabOverride(entity, kPrefabOverrideStaticMeshMaterial);
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
	}

	void EditorUI::RecordComponentDeleted(HexEngine::BaseComponent* component)
	{
		auto transaction = ComponentLifecycleTransaction::CreateForRemovedComponent(component);
		if (transaction)
		{
			_transactions.Push(std::move(transaction));
		}
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
