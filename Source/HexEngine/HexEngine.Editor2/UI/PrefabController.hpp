#pragma once

#include <HexEngine.Core\HexEngine.hpp>
#include <chrono>

namespace HexEditor
{
	class GameIntegrator;
	class Inspector;
	class EntityList;
	class Explorer;
	class EditorTransactionStack;

	class PrefabController
	{
	public:
		struct PrefabPropertyOverride
		{
			std::string componentName;
			std::string path;
			std::string op;
		};

		// What happens to instances when their prefab asset changes.
		enum class InstanceSyncPolicy : int32_t
		{
			Ask = 0,		// dialog listing the out-of-date instances (default)
			Always = 1,		// update them straight away
			Never = 2,		// leave them; they show as out of date until updated by hand
		};

		void SetDependencies(
			HexEngine::IEntityListener* stageEntityListener,
			GameIntegrator* integrator,
			Inspector* inspector,
			EntityList* entityList,
			Explorer* explorer);

		void HandleComponentPropertyEdit(HexEngine::Entity* entity, const json& beforeComponents, const json& afterComponents);
		void HandleTransformPositionEdit(HexEngine::Entity* entity, const math::Vector3& before, const math::Vector3& after);
		void HandleTransformRotationEdit(HexEngine::Entity* entity, const math::Quaternion& before, const math::Quaternion& after);
		void HandleTransformScaleEdit(HexEngine::Entity* entity, const math::Vector3& before, const math::Vector3& after);
		void HandleStaticMeshMaterialEdit(HexEngine::Entity* entity, const fs::path& before, const fs::path& after);

		bool OpenPrefabStage(const fs::path& prefabPath);
		bool SavePrefabStage();
		bool RefreshPrefabInstancesFromAsset(const fs::path& prefabPath);

		void SetTransactionStack(EditorTransactionStack* transactions) { _transactions = transactions; }

		// A prefab asset changed (stage save, apply, file edited on disk). Instances
		// built from it - or from a variant of it - are now out of date and are
		// updated per the sync policy. `alreadyInSync` is an instance the change came
		// from (apply): it is only re-stamped.
		void OnPrefabAssetChanged(const fs::path& prefabPath, HexEngine::Entity* alreadyInSync = nullptr);
		// After scenes load: stamp instances saved before revisions existed, then
		// offer to update the out-of-date ones per the policy.
		void CheckLoadedScenesForOutOfDateInstances();
		// The instance (any entity within it) was built from an older version of its prefab.
		bool IsPrefabInstanceOutOfDate(HexEngine::Entity* entity) const;
		// In-place updates - entities keep their identity, scene-added children and
		// components survive, overrides are re-applied. One undo step each call.
		bool UpdatePrefabInstanceFromAsset(HexEngine::Entity* entity);
		size_t UpdateAllOutOfDatePrefabInstances();
		InstanceSyncPolicy GetInstanceSyncPolicy() const { return _syncPolicy; }
		void SetInstanceSyncPolicy(InstanceSyncPolicy policy);
		bool ClosePrefabStage(bool saveChanges);
		bool IsPrefabStageActive() const;
		bool IsPrefabInstanceEntity(HexEngine::Entity* entity) const;
		bool IsPrefabInstanceRootEntity(HexEngine::Entity* entity) const;
		bool HasPrefabInstanceOverrides(HexEngine::Entity* entity) const;
		bool GetPrefabInstancePropertyOverrides(HexEngine::Entity* entity, std::vector<PrefabPropertyOverride>& outOverrides) const;
		bool RevertPrefabInstancePropertyOverride(HexEngine::Entity* entity, const std::string& componentName, const std::string& propertyPath);
		bool RevertPrefabInstanceComponentOverrides(HexEngine::Entity* entity, const std::string& componentName);
		bool ApplySelectedPrefabInstanceOverridesToAsset(HexEngine::Entity* entity, const std::vector<PrefabPropertyOverride>& selectedOverrides);
		HexEngine::Entity* RevertPrefabInstance(HexEngine::Entity* entity);
		bool ApplyPrefabInstanceToPrefabAsset(HexEngine::Entity* entity);
		bool IsVariantStageEntity(HexEngine::Entity* entity) const;
		bool GetVariantStageEntityOverrideComponents(HexEngine::Entity* entity, std::unordered_set<std::string>& outComponentNames) const;
		bool RevertVariantStageComponentToBase(HexEngine::Entity* entity, const std::string& componentName);

	private:
		void EnsurePrefabStageCameraAndLighting(const std::shared_ptr<HexEngine::Scene>& scene);
		void FramePrefabStageCamera(const std::shared_ptr<HexEngine::Scene>& scene);
		HexEngine::Entity* CloneEntityHierarchyToScene(
			HexEngine::Scene* targetScene,
			HexEngine::Entity* sourceEntity,
			HexEngine::Entity* targetParent,
			const fs::path& prefabSourcePath,
			const std::string& prefabRootName,
			bool isRootInstance);
		void CollectEntityHierarchy(HexEngine::Entity* root, std::vector<HexEngine::Entity*>& outEntities) const;
		HexEngine::Entity* FindPrefabRootInScene(
			const std::shared_ptr<HexEngine::Scene>& scene,
			const std::string& preferredName,
			const std::string& preferredNodeId = std::string()) const;
		HexEngine::Entity* FindPrefabInstanceRoot(HexEngine::Entity* entity) const;
		void RefreshInspectorForPrefabInstance(HexEngine::Entity* changedEntity);
		void RefreshPrefabAssetPreview(const fs::path& prefabPath);
		// Instance sync (see OnPrefabAssetChanged).
		struct InstanceRef
		{
			std::wstring sceneName;
			std::string entityName;
		};
		// Instance roots in loaded scenes built from `prefabPath` or a variant of it
		// (empty path = any prefab), optionally only the out-of-date ones.
		std::vector<HexEngine::Entity*> CollectPrefabInstanceRoots(const fs::path& prefabPath, bool outOfDateOnly, bool unknownCountsAsOutOfDate) const;
		size_t SyncPrefabInstances(const std::vector<HexEngine::Entity*>& instanceRoots);
		void SyncEntityFromPrefab(HexEngine::Entity* instance, HexEngine::Entity* source, bool isRoot, const fs::path& prefabPath, const std::string& rootName);
		HexEngine::Entity* ClonePrefabSubtreeInto(HexEngine::Scene* scene, HexEngine::Entity* source, HexEngine::Entity* parent, const fs::path& prefabPath, const std::string& rootName);
		void RequestInstanceSync(const std::vector<HexEngine::Entity*>& instanceRoots, const std::wstring& reason, const std::string& skipKey);
		void ShowInstanceSyncDialog(const std::wstring& reason);
		void OnInstanceSyncDialogClosed(int32_t action, const std::vector<InstanceRef>& chosen, bool remember);
		std::string GetCurrentPrefabRevision(const fs::path& prefabPath) const;
		bool IsSceneEligibleForInstanceSync(const HexEngine::Scene* scene) const;
		void RefreshViewsAfterInstanceChange();
		void LoadEditorPrefs();
		void SaveEditorPrefs() const;

		struct PrefabStageState
		{
			bool active = false;
			bool isVariantAsset = false;
			fs::path prefabPath;
			std::shared_ptr<HexEngine::Scene> stageScene;
			std::shared_ptr<HexEngine::Scene> previousActiveScene;
			std::vector<std::pair<std::shared_ptr<HexEngine::Scene>, HexEngine::SceneFlags>> previousSceneFlags;
		} _prefabStage;

		HexEngine::IEntityListener* _stageEntityListener = nullptr;
		GameIntegrator* _integrator = nullptr;
		Inspector* _inspector = nullptr;
		EntityList* _entityList = nullptr;
		Explorer* _explorer = nullptr;
		EditorTransactionStack* _transactions = nullptr;

		InstanceSyncPolicy _syncPolicy = InstanceSyncPolicy::Ask;
		// prefab path + revision pairs the user chose to skip: the same change isn't
		// offered twice (a stage save also arrives as a file-change notification).
		std::unordered_set<std::string> _skippedSyncKeys;
		// Open sync dialog and what it offers; a change while it's open merges in.
		HexEngine::Dialog* _syncDialog = nullptr;
		std::vector<InstanceRef> _syncDialogRefs;
		std::vector<std::string> _syncDialogSkipKeys;
		// Short-lived memo of asset revisions: the entity list asks per row.
		mutable std::unordered_map<std::wstring, std::pair<std::chrono::steady_clock::time_point, std::string>> _revisionMemo;
	};
}
