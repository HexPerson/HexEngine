#include "PrefabLoader.hpp"

#include "DiskFile.hpp"
#include "SceneSaveFile.hpp"
#include "../Environment/LogFile.hpp"
#include "../Scene/Prefab.hpp"
#include "../Scene/SceneManager.hpp"
#include "FileSystem.hpp"

namespace
{
	constexpr const char* kVariantPatchComponentArrayTarget = "__components__";

	struct PrefabVariantPatch
	{
		std::string nodeId;
		std::string componentName;
		std::string path;
		std::string op;
		json value = nullptr;
	};

	struct PrefabVariantData
	{
		fs::path basePrefabPath;
		std::vector<PrefabVariantPatch> patches;
	};

	std::wstring BuildComparablePath(const fs::path& inputPath)
	{
		if (inputPath.empty())
			return std::wstring();

		fs::path normalized = inputPath;
		try
		{
			if (fs::exists(normalized))
				normalized = fs::weakly_canonical(normalized);
			else
				normalized = normalized.lexically_normal();
		}
		catch (...)
		{
			normalized = normalized.lexically_normal();
		}

		auto lowered = normalized.make_preferred().wstring();
		std::transform(lowered.begin(), lowered.end(), lowered.begin(),
			[](wchar_t c)
			{
				return static_cast<wchar_t>(::towlower(c));
			});
		return lowered;
	}

	bool TryResolvePrefabPathToAbsolute(const fs::path& inputPath, fs::path& outAbsolutePath)
	{
		outAbsolutePath.clear();
		if (inputPath.empty())
			return false;

		std::error_code ec;
		if (inputPath.is_absolute())
		{
			if (fs::exists(inputPath, ec) && !ec)
			{
				outAbsolutePath = inputPath.lexically_normal();
				return true;
			}
		}
		else
		{
			const fs::path absoluteFromCwd = fs::absolute(inputPath, ec);
			if (!ec && fs::exists(absoluteFromCwd, ec) && !ec)
			{
				outAbsolutePath = absoluteFromCwd.lexically_normal();
				return true;
			}
		}

		auto& resourceSystem = HexEngine::g_pEnv->GetResourceSystem();

		if (auto* owningFileSystem = resourceSystem.FindFileSystemByPath(inputPath); owningFileSystem != nullptr)
		{
			const fs::path resolved = owningFileSystem->GetLocalAbsoluteDataPath(inputPath);
			if (fs::exists(resolved, ec) && !ec)
			{
				outAbsolutePath = resolved.lexically_normal();
				return true;
			}
		}

		for (auto* fileSystem : resourceSystem.GetFileSystems())
		{
			if (fileSystem == nullptr)
				continue;

			const fs::path resolved = fileSystem->GetLocalAbsoluteDataPath(inputPath);
			if (fs::exists(resolved, ec) && !ec)
			{
				outAbsolutePath = resolved.lexically_normal();
				return true;
			}
		}

		return false;
	}

	bool CaptureEntityComponentsSnapshot(HexEngine::Entity* entity, json& outComponents)
	{
		if (entity == nullptr || entity->IsPendingDeletion())
			return false;

		json serializedEntities = json::object();
		HexEngine::JsonFile serializer(fs::path(), std::ios::in);
		entity->Serialize(serializedEntities, &serializer);

		const auto entityIt = serializedEntities.find(entity->GetName());
		if (entityIt == serializedEntities.end() || !entityIt->is_object())
			return false;

		const auto componentsIt = entityIt->find("components");
		if (componentsIt == entityIt->end() || !componentsIt->is_array())
		{
			outComponents = json::array();
			return true;
		}

		outComponents = *componentsIt;
		return true;
	}

	bool ApplySerializedComponentArrayToEntity(
		HexEngine::Entity* entity,
		const json& desiredComponents,
		HexEngine::JsonFile& serializer)
	{
		if (entity == nullptr || !desiredComponents.is_array())
			return false;

		std::vector<std::string> desiredOrder;
		std::unordered_map<std::string, json> desiredByName;
		for (const auto& componentData : desiredComponents)
		{
			if (!componentData.is_object())
				continue;

			const auto componentName = componentData.value("name", std::string());
			if (componentName.empty())
				continue;

			if (desiredByName.find(componentName) == desiredByName.end())
				desiredOrder.push_back(componentName);

			desiredByName[componentName] = componentData;
		}

		auto existingComponents = entity->GetAllComponents();
		for (auto* component : existingComponents)
		{
			if (component == nullptr)
				continue;

			const auto& componentName = component->GetComponentName();
			if (desiredByName.find(componentName) != desiredByName.end())
				continue;

			if (component->GetComponentId() == HexEngine::Transform::_GetComponentId())
				continue;

			entity->RemoveComponent(component);
		}

		for (const auto& componentName : desiredOrder)
		{
			auto* component = entity->GetComponentByClassName(componentName);
			if (component != nullptr)
				continue;

			auto* cls = HexEngine::g_pEnv->_classRegistry->Find(componentName);
			if (cls == nullptr)
			{
				LOG_WARN("Prefab variant could not resolve component class '%s' on '%s'.",
					componentName.c_str(), entity->GetName().c_str());
				continue;
			}

			component = cls->newInstanceFn(entity);
			if (component == nullptr)
			{
				LOG_WARN("Prefab variant failed to instantiate component class '%s' on '%s'.",
					componentName.c_str(), entity->GetName().c_str());
				continue;
			}

			entity->AddComponent(component);
		}

		for (const auto& componentName : desiredOrder)
		{
			auto* component = entity->GetComponentByClassName(componentName);
			if (component == nullptr || component->GetComponentId() != HexEngine::Transform::_GetComponentId())
				continue;

			json componentData = desiredByName[componentName];
			component->Deserialize(componentData, &serializer);
		}

		for (const auto& componentName : desiredOrder)
		{
			auto* component = entity->GetComponentByClassName(componentName);
			if (component == nullptr || component->GetComponentId() == HexEngine::Transform::_GetComponentId())
				continue;

			json componentData = desiredByName[componentName];
			component->Deserialize(componentData, &serializer);
		}

		return true;
	}

	bool ApplyVariantPatchesToEntity(HexEngine::Entity* entity, const std::vector<PrefabVariantPatch>& patches)
	{
		if (entity == nullptr || patches.empty())
			return false;

		json serializedEntities = json::object();
		HexEngine::JsonFile serializer(fs::path(), std::ios::in);
		entity->Serialize(serializedEntities, &serializer);

		auto entityIt = serializedEntities.find(entity->GetName());
		if (entityIt == serializedEntities.end() || !entityIt->is_object())
			return false;

		auto componentsIt = entityIt->find("components");
		if (componentsIt == entityIt->end() || !componentsIt->is_array())
			return false;

		auto rebuildComponentPointers = [&]() -> std::unordered_map<std::string, json*>
		{
			std::unordered_map<std::string, json*> componentsByName;
			for (auto& componentJson : *componentsIt)
			{
				if (!componentJson.is_object())
					continue;

				const auto componentName = componentJson.value("name", std::string());
				if (componentName.empty())
					continue;

				if (componentsByName.find(componentName) == componentsByName.end())
				{
					componentsByName[componentName] = &componentJson;
				}
			}
			return componentsByName;
		};

		auto componentsByName = rebuildComponentPointers();
		json componentArrayPatchDoc = json::array();
		std::unordered_map<std::string, json> patchDocsByComponent;
		for (const auto& patch : patches)
		{
			if (patch.componentName.empty() || patch.path.empty() || patch.op.empty())
				continue;

			if (patch.componentName == kVariantPatchComponentArrayTarget)
			{
				json op = json::object();
				op["op"] = patch.op;
				op["path"] = patch.path;
				if (patch.op != "remove")
					op["value"] = patch.value;

				componentArrayPatchDoc.push_back(std::move(op));
				continue;
			}

			auto componentIt = componentsByName.find(patch.componentName);
			if (componentIt == componentsByName.end())
				continue;

			auto& patchDoc = patchDocsByComponent[patch.componentName];
			if (!patchDoc.is_array())
				patchDoc = json::array();

			json op = json::object();
			op["op"] = patch.op;
			op["path"] = patch.path;
			if (patch.op != "remove")
				op["value"] = patch.value;
			patchDoc.push_back(std::move(op));
		}

		bool appliedAny = false;

		if (!componentArrayPatchDoc.empty())
		{
			try
			{
				json patchedComponents = componentsIt->patch(componentArrayPatchDoc);
				if (patchedComponents.is_array())
				{
					if (ApplySerializedComponentArrayToEntity(entity, patchedComponents, serializer))
					{
						appliedAny = true;
						if (CaptureEntityComponentsSnapshot(entity, *componentsIt))
						{
							componentsByName = rebuildComponentPointers();
						}
					}
				}
			}
			catch (const std::exception& ex)
			{
				LOG_WARN("Failed to apply variant component-array patches on '%s': %s",
					entity->GetName().c_str(), ex.what());
			}
		}

		for (const auto& [componentName, patchDoc] : patchDocsByComponent)
		{
			auto componentIt = componentsByName.find(componentName);
			if (componentIt == componentsByName.end() || componentIt->second == nullptr)
				continue;

			auto* componentJson = componentIt->second;
			auto* component = entity->GetComponentByClassName(componentName);
			if (component == nullptr)
				continue;

			try
			{
				json patched = componentJson->patch(patchDoc);
				component->Deserialize(patched, &serializer);
				*componentJson = std::move(patched);
				appliedAny = true;
			}
			catch (const std::exception& ex)
			{
				LOG_WARN("Failed to apply variant patch on '%s::%s': %s",
					entity->GetName().c_str(), componentName.c_str(), ex.what());
			}
		}

		return appliedAny;
	}

	bool ApplyVariantPatchesToScene(HexEngine::Scene* scene, const std::vector<PrefabVariantPatch>& patches)
	{
		if (scene == nullptr || patches.empty())
			return false;

		std::unordered_map<std::string, std::vector<PrefabVariantPatch>> patchesByNodeId;
		for (const auto& patch : patches)
		{
			if (!patch.nodeId.empty())
			{
				patchesByNodeId[patch.nodeId].push_back(patch);
			}
		}

		if (patchesByNodeId.empty())
			return false;

		std::unordered_map<std::string, HexEngine::Entity*> entitiesByNodeId;
		for (const auto& bySignature : scene->GetEntities())
		{
			for (auto* entity : bySignature.second)
			{
				if (entity == nullptr)
					continue;

				const auto nodeId = entity->EnsurePrefabNodeId();
				if (!nodeId.empty())
					entitiesByNodeId[nodeId] = entity;
			}
		}

		bool appliedAny = false;
		for (const auto& [nodeId, nodePatches] : patchesByNodeId)
		{
			auto nodeIt = entitiesByNodeId.find(nodeId);
			if (nodeIt == entitiesByNodeId.end() || nodeIt->second == nullptr)
			{
				LOG_WARN("Prefab variant patch targets unknown node id '%s'.", nodeId.c_str());
				continue;
			}

			if (ApplyVariantPatchesToEntity(nodeIt->second, nodePatches))
				appliedAny = true;
		}

		return appliedAny;
	}

	bool TryReadPrefabVariantData(const fs::path& prefabAssetPath, PrefabVariantData& outData)
	{
		outData = {};
		HexEngine::DiskFile file(prefabAssetPath, std::ios::in | std::ios::binary);
		if (!file.Open())
			return false;

		std::string rawJson;
		file.ReadAll(rawJson);
		file.Close();
		if (rawJson.empty())
			return false;

		json rootJson;
		try
		{
			rootJson = json::parse(rawJson);
		}
		catch (const std::exception&)
		{
			return false;
		}

		const auto variantIt = rootJson.find("variant");
		if (variantIt == rootJson.end() || !variantIt->is_object())
			return false;

		const auto basePathStr = variantIt->value("basePrefab", std::string());
		if (basePathStr.empty())
			return false;

		fs::path basePath = fs::path(basePathStr);
		if (basePath.is_relative())
			basePath = (prefabAssetPath.parent_path() / basePath).lexically_normal();
		outData.basePrefabPath = basePath;

		const auto patchArrayIt = variantIt->find("patches");
		if (patchArrayIt != variantIt->end() && patchArrayIt->is_array())
		{
			for (const auto& item : *patchArrayIt)
			{
				if (!item.is_object())
					continue;

				PrefabVariantPatch patch;
				patch.nodeId = item.value("nodeId", std::string());
				patch.componentName = item.value("component", std::string());
				patch.path = item.value("path", std::string());
				patch.op = item.value("op", std::string());
				if (patch.nodeId.empty() || patch.componentName.empty() || patch.path.empty() || patch.op.empty())
					continue;

				const auto valueIt = item.find("value");
				patch.value = valueIt != item.end() ? *valueIt : json();
				outData.patches.push_back(std::move(patch));
			}
		}

		return true;
	}

	bool LoadPrefabAssetToSceneRecursive(
		const fs::path& prefabAssetPath,
		const std::shared_ptr<HexEngine::Scene>& targetScene,
		std::unordered_set<std::wstring>& recursionGuard)
	{
		if (targetScene == nullptr)
			return false;

		fs::path resolvedPrefabPath;
		if (!TryResolvePrefabPathToAbsolute(prefabAssetPath, resolvedPrefabPath))
		{
			LOG_CRIT("Failed to resolve prefab path '%s' to an absolute path.", prefabAssetPath.string().c_str());
			return false;
		}

		const std::wstring recursionKey = BuildComparablePath(resolvedPrefabPath);
		if (recursionKey.empty())
			return false;

		if (!recursionGuard.insert(recursionKey).second)
		{
			LOG_CRIT("Detected prefab variant recursion while resolving '%s'.", resolvedPrefabPath.string().c_str());
			return false;
		}

		PrefabVariantData variantData;
		const bool isVariant = TryReadPrefabVariantData(resolvedPrefabPath, variantData);
		bool loaded = false;

		if (isVariant)
		{
			loaded = LoadPrefabAssetToSceneRecursive(variantData.basePrefabPath, targetScene, recursionGuard);
			if (loaded)
			{
				ApplyVariantPatchesToScene(targetScene.get(), variantData.patches);
			}
		}
		else
		{
			HexEngine::SceneSaveFile file(resolvedPrefabPath, std::ios::in, targetScene, HexEngine::SceneFileFlags::IsPrefab);
			loaded = file.Load(targetScene);
		}

		recursionGuard.erase(recursionKey);
		return loaded;
	}

	std::vector<HexEngine::Entity*> CollectSavableEntities(const std::shared_ptr<HexEngine::Scene>& scene)
	{
		std::vector<HexEngine::Entity*> entities;
		if (scene == nullptr)
			return entities;

		for (const auto& bySignature : scene->GetEntities())
		{
			for (auto* entity : bySignature.second)
			{
				if (entity != nullptr && !entity->HasFlag(HexEngine::EntityFlags::DoNotSave))
				{
					entities.push_back(entity);
				}
			}
		}

		return entities;
	}
}

namespace HexEngine
{
	PrefabLoader::PrefabLoader()
	{
		g_pEnv->GetResourceSystem().RegisterResourceLoader(this);
	}

	PrefabLoader::~PrefabLoader()
	{
		g_pEnv->GetResourceSystem().UnregisterResourceLoader(this);
	}

	std::vector<Entity*> PrefabLoader::LoadPrefab(const std::shared_ptr<Scene>& scene, const fs::path& path)
	{
		if (scene == nullptr || path.empty())
			return {};

		auto prefab = Prefab::Create(path);
		if (prefab == nullptr || prefab->_scene == nullptr)
		{
			LOG_CRIT("Failed to load prefab '%s'", path.string().c_str());
			return {};
		}

		std::vector<std::pair<Entity*, Entity*>> sourceToMerged;
		auto mergedEntities = scene->MergeFrom(prefab->_scene.get(), &sourceToMerged);

		auto findSourceRoot = [](Entity* sourceEntity) -> Entity*
		{
			if (sourceEntity == nullptr)
				return nullptr;

			auto* root = sourceEntity;
			while (root->GetParent() != nullptr)
			{
				root = root->GetParent();
			}

			return root;
		};

		for (const auto& entry : sourceToMerged)
		{
			auto* sourceEntity = entry.first;
			auto* mergedEntity = entry.second;
			if (sourceEntity == nullptr || mergedEntity == nullptr)
				continue;

			mergedEntity->SetPrefabNodeId(sourceEntity->EnsurePrefabNodeId());

			auto* sourceRoot = findSourceRoot(sourceEntity);
			if (sourceRoot == nullptr)
				continue;

			const bool isRootInstance = sourceEntity->GetParent() == nullptr;
			mergedEntity->SetPrefabSource(path, sourceRoot->GetName(), isRootInstance);
		}

		return mergedEntities;
	}

	bool PrefabLoader::LoadPrefabAssetToScene(const fs::path& path, const std::shared_ptr<Scene>& targetScene)
	{
		if (path.empty() || targetScene == nullptr)
			return false;

		std::unordered_set<std::wstring> recursionGuard;
		return LoadPrefabAssetToSceneRecursive(path, targetScene, recursionGuard);
	}

	std::shared_ptr<IResource> PrefabLoader::LoadResourceFromFile(const fs::path& absolutePath, FileSystem* fileSystem, const ResourceLoadOptions* options)
	{
		(void)fileSystem;
		(void)options;

		if (g_pEnv == nullptr || g_pEnv->_sceneManager == nullptr)
		{
			LOG_CRIT("Cannot load prefab '%s' because SceneManager is unavailable.", absolutePath.string().c_str());
			return nullptr;
		}

		auto prefabScene = g_pEnv->_sceneManager->CreateEmptyScene(false);
		if (!LoadPrefabAssetToScene(absolutePath, prefabScene))
		{
			LOG_CRIT("Failed to load prefab file: %s", absolutePath.filename().string().c_str());
			return nullptr;
		}

		auto prefab = std::shared_ptr<Prefab>(new Prefab, ResourceDeleter());
		prefab->_scene = prefabScene;
		prefab->_entities = CollectSavableEntities(prefabScene);
		return prefab;
	}

	std::shared_ptr<IResource> PrefabLoader::LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options)
	{
		(void)data;
		(void)relativePath;
		(void)fileSystem;
		(void)options;
		return nullptr;
	}

	void PrefabLoader::OnResourceChanged(std::shared_ptr<IResource> resource)
	{
		(void)resource;
	}

	void PrefabLoader::UnloadResource(IResource* resource)
	{
		auto* prefab = dynamic_cast<Prefab*>(resource);
		if (prefab == nullptr)
			return;

		prefab->_entities.clear();
		prefab->_scene.reset();
	}

	std::vector<std::string> PrefabLoader::GetSupportedResourceExtensions()
	{
		return { ".hprefab" };
	}

	std::wstring PrefabLoader::GetResourceDirectory() const
	{
		return L"Prefabs";
	}

	void PrefabLoader::SaveResource(IResource* resource, const fs::path& path)
	{
		auto* prefab = dynamic_cast<Prefab*>(resource);
		if (prefab == nullptr)
		{
			LOG_CRIT("Failed to save prefab: resource is not a Prefab.");
			return;
		}

		if (g_pEnv == nullptr || g_pEnv->_sceneManager == nullptr)
		{
			LOG_CRIT("Cannot save prefab '%s' because SceneManager is unavailable.", path.string().c_str());
			return;
		}

		const fs::path outputPath = path.empty() ? prefab->GetAbsolutePath() : path;
		if (outputPath.empty())
		{
			LOG_CRIT("Failed to save prefab: output path is empty.");
			return;
		}

		std::shared_ptr<Scene> saveScene = prefab->_scene;
		if (saveScene == nullptr)
		{
			saveScene = g_pEnv->_sceneManager->CreateEmptyScene(false);
		}

		const auto entitiesToSave = CollectSavableEntities(saveScene);
		SceneSaveFile prefabFile(outputPath, std::ios::out | std::ios::trunc, saveScene, SceneFileFlags::IsPrefab);
		if (!prefabFile.Save(entitiesToSave))
		{
			LOG_CRIT("Failed to save prefab file: %s", outputPath.filename().string().c_str());
			return;
		}

		prefab->_scene = saveScene;
		prefab->_entities = entitiesToSave;
	}
}
