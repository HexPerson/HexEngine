
#pragma once

#include <HexEngine.Core\HexEngine.hpp>
#include "Elements\EntityList.hpp"
#include "Actions\Inspector.hpp"
#include "Actions\Explorer.hpp"
#include "Actions\ProjectManager.hpp"
#include "Actions/SceneView.hpp"
#include "../GameIntegrator.hpp"

namespace HexEditor
{
	class Gadget;

	class EditorUI : public UIManager, public IEntityListener
	{
	public:
		enum PrimitiveType
		{
			Plane,
			Cube,
			Sphere,
			Terrain,
			Ocean
		};

		friend class EditorExtension;

		EditorUI();
		~EditorUI();

		virtual void Create(uint32_t width, uint32_t height) override;
		
		virtual bool OnInputEvent(InputEvent event, InputData* data) override;

		Explorer* GetExplorer() { return _lowerDock; }

		virtual void Render() override;

		virtual void Update(float frameTime) override;

		void ShowSettingsDialog();

		EntityList* GetEntityTreeList() const { return _entityList; }
		Inspector* GetInspector() const { return _rightDock; }
		SceneView* GetSceneView() const { return _sceneView; }

		RayHit RayCastWorld(const std::vector<Entity*>& entsToIgnore = {});

	private:		
		void CreateMenuBar();
		void CreateDocks(uint32_t width, uint32_t height);
		void CreateEntityList();

		void ForEachElementImpl(Element* element, std::function<void(Element*)> doAction);

		void HandleDeletions();
		void HandleDeletetionImpl(Element* element);

		void CheckCentralDockRoamState();

		void CreateLineEditDialog(const std::wstring& label, std::function<void(EditorUI*, const std::wstring&)> callback);

		void OnAddLight();
		void OnAddSpotLight();
		void OnSaveAction();
		void OnExportAction();
		void OnAddBillboard();
		void OnProjectManagerCompleted(const fs::path& projectFolder, const std::string& projectName, bool didLoadExisting, const std::wstring& namespaceName, LoadingDialog* loadingDlg);
		void OnAddPrimitive(PrimitiveType type);

		void RunGame();
		void StopGame();
		

	private:
		// IEntityListener overrides
		virtual void OnAddEntity(Entity* entity) override;

		virtual void OnRemoveEntity(Entity* entity) override;

		virtual void OnAddComponent(Entity* entity, BaseComponent* component) override;

		virtual void OnRemoveComponent(Entity* entity, BaseComponent* component) override;

	private:
		// menu bar actions
		void OnCreateNewSceneAction(const std::wstring& sceneName);
		void OnDeleteSceneAction();
		void OnStartPaintTreeDialog();


	private:
		// docks
		Dock* _leftDock = nullptr;
		Inspector* _rightDock = nullptr;
		SceneView* _sceneView = nullptr;
		Explorer* _lowerDock = nullptr;
		EntityList* _entityList = nullptr;

		math::Vector3 _freeLookDir;
		float _freeLookMultiplier = 1.0f;

		ProjectManager* _projectManager = nullptr;
		fs::path _projectFolderPath;
		fs::path _projectFilePath;

		ProjectFile* _projectFile = nullptr;
		std::vector<SceneSaveFile*> _sceneFiles;

		std::vector<Gadget*> _gadgets;
		GameIntegrator _integrator;
		MenuBar* _mainMenu = nullptr;
		
	};

	inline EditorUI* g_pUIManager = nullptr;
}
