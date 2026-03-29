

#include "SceneManager.hpp"
#include "../HexEngine.hpp"
#include "../FileSystem/DiskFile.hpp"
#include "../FileSystem/SceneSaveFile.hpp"
#include <algorithm>
#include <cwctype>
#include <exception>
#include <unordered_map>
#include <unordered_set>

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
				return static_cast<wchar_t>(std::towlower(c));
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
}

namespace HexEngine
{
	SceneManager::SceneManager()
	{
		g_pEnv->GetResourceSystem().RegisterResourceLoader(this);
	}

	void SceneManager::Destroy()
	{
		std::unique_lock lock(_mutex);

		while(_scenes.size() > 0)
		{
			UnloadScene(_scenes[0].get());
		}

		_scenes.clear();

		g_pEnv->GetResourceSystem().UnregisterResourceLoader(this);
	}

	std::shared_ptr<Scene> SceneManager::LoadScene(const fs::path& path)
	{
		auto scene = CreateEmptyScene(false, nullptr, true);

		SceneSaveFile file(path, std::ios::in, scene);

		if (!file.Load())
		{
			scene.reset();
			LOG_CRIT("Failed to load scene");
			return nullptr;
		}

		file.Close();

		return scene;
	}

	const std::vector<std::shared_ptr<Scene>>& SceneManager::GetAllScenes() const
	{
		return _scenes;
	}

	void SceneManager::UnloadScene(Scene* scene)
	{
		std::unique_lock lock(_mutex);

		scene->Destroy();

		_scenes.erase(std::remove_if(_scenes.begin(), _scenes.end(),
			[scene](std::shared_ptr<Scene> sp) {
				return sp.get() == scene;
			}));

		delete scene;					

		if (scene == _currentScene.get())
			_currentScene = nullptr;
	}

	std::vector<Entity*> SceneManager::LoadPrefab(std::shared_ptr<Scene> scene, const fs::path& path)
	{
		std::vector<Entity*> ents;

		auto prefabScene = CreateEmptyScene(false);

		if (!LoadPrefabAssetToScene(path, prefabScene))
		{
			LOG_CRIT("Failed to load prefab '%s'", path.string().c_str());
			return {};
		}

		std::vector<std::pair<Entity*, Entity*>> sourceToMerged;
		ents = scene->MergeFrom(prefabScene.get(), &sourceToMerged);

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

		return ents;
	}

	bool SceneManager::LoadPrefabAssetToScene(const fs::path& path, const std::shared_ptr<Scene>& targetScene)
	{
		if (path.empty() || targetScene == nullptr)
			return false;

		std::unordered_set<std::wstring> recursionGuard;
		return LoadPrefabAssetToSceneRecursive(path, targetScene, recursionGuard);
	}

	std::shared_ptr<Scene> SceneManager::CreateEmptyScene(bool createSkySphere, IEntityListener* listener, bool registerScene)
	{
		std::unique_lock lock(_mutex);

		std::shared_ptr<Scene> scene = std::shared_ptr<Scene>(new Scene, ResourceDeleter());

		if (_currentScene == nullptr && registerScene == true)
			_currentScene = scene;

		scene->CreateEmpty(createSkySphere, listener);

		if(registerScene)
			_scenes.push_back(scene);		

		return scene;
	}

	std::shared_ptr<Scene> SceneManager::GetCurrentScene()
	{
		return _currentScene;
	}

	void SceneManager::Update(float frameTime)
	{
		std::unique_lock lock(_mutex);

		for (auto&& scene : _scenes)
		{
			if (HEX_HASFLAG(scene->GetFlags(), SceneFlags::Updateable))
			{
				_currentScene = scene;
				_currentScene->Update(frameTime);
			}
		}
	}

	void SceneManager::FixedUpdate(float frameTime)
	{
		std::unique_lock lock(_mutex);

		for (auto&& scene : _scenes)
		{
			if (HEX_HASFLAG(scene->GetFlags(), SceneFlags::Updateable))
			{
				_currentScene = scene;
				scene->FixedUpdate(frameTime);
			}
		}
	}

	void SceneManager::LateUpdate(float frameTime)
	{
		std::unique_lock lock(_mutex);

		for (auto&& scene : _scenes)
		{
			if (HEX_HASFLAG(scene->GetFlags(), SceneFlags::Updateable))
			{
				_currentScene = scene;
				scene->LateUpdate(frameTime);
			}
		}
	}

	void SceneManager::Render()
	{
		std::unique_lock lock(_mutex);

		for (auto&& scene : _scenes)
		{
			if (HEX_HASFLAG(scene->GetFlags(), SceneFlags::Renderable))
			{
				GFX_PERF_BEGIN(0xffffffff, std::format(L"Rendering scene '{}'", scene->GetName()).c_str());

				_currentScene = scene;
				g_pEnv->_sceneRenderer->RenderScene(scene.get(), scene->GetMainCamera(), scene->GetFlags());

				GFX_PERF_END();
			}
		}
	}

	void SceneManager::SetActiveScene(const std::shared_ptr<Scene>& scene)
	{
		_currentScene = scene;
	}

	std::shared_ptr<IResource> SceneManager::LoadResourceFromFile(const fs::path& absolutePath, FileSystem* fileSystem, const ResourceLoadOptions* options)
	{
		if (absolutePath.extension() == ".hprefab")
		{			
			LoadPrefab(GetCurrentScene(), absolutePath);
			return nullptr;
		}
		else
		{
			std::shared_ptr<Scene> scene = LoadScene(absolutePath);
			if (scene == nullptr)
			{
				LOG_CRIT("Failed to load scene %s", absolutePath.string().c_str());
				return nullptr;
			}

			return scene;
		}

		return nullptr;
	}

	std::shared_ptr<IResource> SceneManager::LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options)
	{
		return nullptr;
	}

	void SceneManager::UnloadResource(IResource* resource)
	{
		UnloadScene(reinterpret_cast<Scene*>(resource));
	}

	std::vector<std::string> SceneManager::GetSupportedResourceExtensions()
	{
		return { ".hscene" };
	}

	std::wstring SceneManager::GetResourceDirectory() const
	{
		return L"Scenes";
	}
}
