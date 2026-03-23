
#pragma once

#include <HexEngine.Core\HexEngine.hpp>
#include "Elements\EntityList.hpp"
#include "Actions\Inspector.hpp"
#include "Actions\Explorer.hpp"
#include "Actions\ProjectManager.hpp"
#include "Actions/SceneView.hpp"
#include "PrefabController.hpp"
#include "EditorTransactions.hpp"
#include "../GameIntegrator.hpp"

namespace HexEditor
{
	class Gadget;

	class EditorUI : public HexEngine::UIManager, public HexEngine::IEntityListener
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
		
		virtual bool OnInputEvent(HexEngine::InputEvent event, HexEngine::InputData* data) override;

		Explorer* GetExplorer() { return _lowerDock; }

		virtual void Render() override;

		virtual void Update(float frameTime) override;

		void ShowSettingsDialog();

		EntityList* GetEntityTreeList() const { return _entityList; }
		Inspector* GetInspector() const { return _rightDock; }
		SceneView* GetSceneView() const { return _sceneView; }

		HexEngine::RayHit RayCastWorld(const std::vector<HexEngine::Entity*>& entsToIgnore = {});

		void RecordEntityPositionChange(HexEngine::Entity* entity, const math::Vector3& before, const math::Vector3& after);
		void RecordEntityScaleChange(HexEngine::Entity* entity, const math::Vector3& before, const math::Vector3& after);
		void RecordStaticMeshMaterialChange(HexEngine::Entity* entity, const fs::path& before, const fs::path& after);
		void RecordEntityRename(HexEngine::Entity* entity, const std::string& beforeName, const std::string& afterName);
		void RecordEntityParentChange(HexEngine::Entity* entity, HexEngine::Entity* beforeParent, HexEngine::Entity* afterParent);
		void RecordEntityVisibilityChange(HexEngine::Entity* entity, bool beforeHidden, bool afterHidden);
		void RecordComponentAdded(HexEngine::BaseComponent* component);
		void RecordComponentDeleted(HexEngine::BaseComponent* component);
		void RecordComponentPropertyStateChange(
			HexEngine::Entity* entity,
			const Detail::EntityComponentStateSnapshot& before,
			const Detail::EntityComponentStateSnapshot& after);
		void RecordEntityCreated(HexEngine::Entity* entity);
		void RecordEntityDeleted(HexEngine::Entity* entity);
		bool UndoLastTransaction();
		bool RedoLastTransaction();
		bool OpenPrefabStage(const fs::path& prefabPath);
		bool SavePrefabStage();
		bool ClosePrefabStage(bool saveChanges);
		bool IsPrefabStageActive() const;
		bool IsPrefabInstanceEntity(HexEngine::Entity* entity) const;
		bool IsPrefabInstanceRootEntity(HexEngine::Entity* entity) const;
		bool HasPrefabInstanceOverrides(HexEngine::Entity* entity) const;
		bool GetPrefabInstancePropertyOverrides(HexEngine::Entity* entity, std::vector<PrefabController::PrefabPropertyOverride>& outOverrides) const;
		bool RevertPrefabInstancePropertyOverride(HexEngine::Entity* entity, const std::string& componentName, const std::string& propertyPath);
		bool RevertPrefabInstanceComponentOverrides(HexEngine::Entity* entity, const std::string& componentName);
		bool ApplySelectedPrefabInstanceOverridesToAsset(HexEngine::Entity* entity, const std::vector<PrefabController::PrefabPropertyOverride>& selectedOverrides);
		HexEngine::Entity* RevertPrefabInstance(HexEngine::Entity* entity);
		bool ApplyPrefabInstanceToPrefabAsset(HexEngine::Entity* entity);
		bool IsVariantStageEntity(HexEngine::Entity* entity) const;
		bool GetVariantStageEntityOverrideComponents(HexEngine::Entity* entity, std::unordered_set<std::string>& outComponentNames) const;
		bool RevertVariantStageComponentToBase(HexEngine::Entity* entity, const std::string& componentName);

		const std::vector<Gadget*> GetAllGadgets() const { return _gadgets; }

	private:		
		void CreateMenuBar();
		void CreateDocks(uint32_t width, uint32_t height);
		void CreateEntityList();

		void ForEachElementImpl(HexEngine::Element* element, std::function<void(HexEngine::Element*)> doAction);

		void HandleDeletions();
		void HandleDeletetionImpl(HexEngine::Element* element);

		void CheckCentralDockRoamState();
		void TryBeginPendingComponentEdit(HexEngine::InputEvent event, HexEngine::InputData* data);
		void TryCommitPendingComponentEdit(HexEngine::InputEvent event, HexEngine::InputData* data);
		bool IsFocusedElementWithinInspector() const;
		static HexEngine::Element* FindFocusedElement(HexEngine::Element* root);
		static bool IsDescendantOf(const HexEngine::Element* element, const HexEngine::Element* ancestor);
		static bool HasMatchingComponentLayout(const json& before, const json& after);

		void CreateLineEditDialog(const std::wstring& label, std::function<void(EditorUI*, const std::wstring&)> callback);

		void OnAddLight();
		void OnAddSpotLight();
		void OnSaveAction();
		void OnExportAction();
		void OnAddBillboard();
		void OnGenerateHLOD();
		void OnProjectManagerCompleted(const fs::path& projectFolder, const std::string& projectName, bool didLoadExisting, const std::wstring& namespaceName, HexEngine::LoadingDialog* loadingDlg);
		void OnAddPrimitive(PrimitiveType type);

		void RunGame();
		void StopGame();

			

	private:
		// IEntityListener overrides
		virtual void OnAddEntity(HexEngine::Entity* entity) override;

		virtual void OnRemoveEntity(HexEngine::Entity* entity) override;

		virtual void OnAddComponent(HexEngine::Entity* entity, HexEngine::BaseComponent* component) override;

		virtual void OnRemoveComponent(HexEngine::Entity* entity, HexEngine::BaseComponent* component) override;

	private:
		// menu bar actions
		void OnCreateNewSceneAction(const std::wstring& sceneName);
		void OnDeleteSceneAction();
		void OnStartPaintTreeDialog();


	private:
		// docks
		HexEngine::Dock* _leftDock = nullptr;
		Inspector* _rightDock = nullptr;
		SceneView* _sceneView = nullptr;
		Explorer* _lowerDock = nullptr;
		EntityList* _entityList = nullptr;

		math::Vector3 _freeLookDir;
		float _freeLookMultiplier = 1.0f;

		ProjectManager* _projectManager = nullptr;
		fs::path _projectFolderPath;
		fs::path _projectFilePath;

		HexEngine::ProjectFile* _projectFile = nullptr;
		std::vector<HexEngine::SceneSaveFile*> _sceneFiles;

		std::vector<Gadget*> _gadgets;
		GameIntegrator _integrator;
		PrefabController _prefabController;
		HexEngine::MenuBar* _mainMenu = nullptr;
		EditorTransactionStack _transactions;

		enum class PendingComponentEditSource
		{
			None,
			Mouse,
			Keyboard
		};

		bool _pendingComponentEditActive = false;
		PendingComponentEditSource _pendingComponentEditSource = PendingComponentEditSource::None;
		Detail::EntityComponentStateSnapshot _pendingComponentEditBefore;
		
	};

	inline EditorUI* g_pUIManager = nullptr;
}
