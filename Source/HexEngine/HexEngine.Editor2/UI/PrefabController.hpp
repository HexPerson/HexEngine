#pragma once

#include <HexEngine.Core\HexEngine.hpp>

namespace HexEditor
{
	class GameIntegrator;
	class Inspector;
	class EntityList;
	class Explorer;

	class PrefabController
	{
	public:
		void SetDependencies(
			HexEngine::IEntityListener* stageEntityListener,
			GameIntegrator* integrator,
			Inspector* inspector,
			EntityList* entityList,
			Explorer* explorer);

		void HandleComponentPropertyEdit(HexEngine::Entity* entity, const json& beforeComponents, const json& afterComponents);
		void HandleTransformPositionEdit(HexEngine::Entity* entity, const math::Vector3& before, const math::Vector3& after);
		void HandleTransformScaleEdit(HexEngine::Entity* entity, const math::Vector3& before, const math::Vector3& after);
		void HandleStaticMeshMaterialEdit(HexEngine::Entity* entity, const fs::path& before, const fs::path& after);

		bool OpenPrefabStage(const fs::path& prefabPath);
		bool SavePrefabStage();
		bool RefreshPrefabInstancesFromAsset(const fs::path& prefabPath);
		bool ClosePrefabStage(bool saveChanges);
		bool IsPrefabStageActive() const;
		bool IsPrefabInstanceEntity(HexEngine::Entity* entity) const;
		bool IsPrefabInstanceRootEntity(HexEngine::Entity* entity) const;
		bool HasPrefabInstanceOverrides(HexEngine::Entity* entity) const;
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
		bool PropagateAppliedPrefabToInstances(const fs::path& prefabPath, HexEngine::Entity* appliedSourceInstance, HexEngine::Entity** outReplacementForAppliedInstance = nullptr);

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
	};
}
