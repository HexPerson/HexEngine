#include "PrefabController.hpp"

#include "../GameIntegrator.hpp"
#include "Actions/Inspector.hpp"
#include "Actions/Explorer.hpp"
#include "Elements/EntityList.hpp"
#include "EditorTransactions.hpp"

#include <HexEngine.Core\FileSystem\DiskFile.hpp>
#include <HexEngine.Core\FileSystem\PrefabLoader.hpp>
#include <HexEngine.Core\FileSystem\SceneSaveFile.hpp>
#include <HexEngine.Core\Scene\SceneFramingUtils.hpp>
#include <algorithm>
#include <exception>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <cwctype>
#include <cctype>
#include <chrono>
#include <HexEngine.Core\GUI\Elements\Button.hpp>
#include <HexEngine.Core\GUI\Elements\Checkbox.hpp>
#include <HexEngine.Core\GUI\Elements\Dialog.hpp>
#include <HexEngine.Core\GUI\Elements\ScrollView.hpp>

namespace HexEditor
{
	namespace
	{
		constexpr const char* kPrefabOverrideTransformPosition = "transform.position";
		constexpr const char* kPrefabOverrideTransformRotation = "transform.rotation";
		constexpr const char* kPrefabOverrideTransformScale = "transform.scale";
		constexpr const char* kPrefabOverrideStaticMeshMesh = "staticMesh.mesh";
		constexpr const char* kPrefabOverrideStaticMeshMaterial = "staticMesh.material";
		constexpr const char* kPrefabOverrideStaticMeshUVScale = "staticMesh.uvScale";
		constexpr const char* kPrefabOverrideStaticMeshShadowCullMode = "staticMesh.shadowCullMode";
		constexpr const char* kPrefabOverrideStaticMeshOffsetPosition = "staticMesh.offsetPosition";
		constexpr const char* kPrefabOverrideComponentArrayPatchTarget = "__components__";

		struct VariantAssetData
		{
			json rootJson = json::object();
			fs::path basePrefabAbsolutePath;
			std::string basePrefabReference;
		};

		struct VariantPatchEntry
		{
			std::string nodeId;
			HexEngine::Entity::PrefabOverridePatch patch;
		};

		std::wstring BuildComparablePrefabPath(const fs::path& inputPath)
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

		bool ArePrefabPathsEquivalent(const fs::path& lhs, const fs::path& rhs)
		{
			if (lhs.empty() || rhs.empty())
				return false;

			return BuildComparablePrefabPath(lhs) == BuildComparablePrefabPath(rhs);
		}

		bool IsVariantPrefabAsset(const fs::path& prefabPath)
		{
			if (prefabPath.empty())
				return false;

			HexEngine::DiskFile file(prefabPath, std::ios::in | std::ios::binary);
			if (!file.Open())
				return false;

			std::string rawJson;
			file.ReadAll(rawJson);
			file.Close();
			if (rawJson.empty())
				return false;

			try
			{
				const auto rootJson = json::parse(rawJson);
				const auto variantIt = rootJson.find("variant");
				return variantIt != rootJson.end() && variantIt->is_object();
			}
			catch (const std::exception&)
			{
				return false;
			}
		}

		HexEngine::Element* FindFocusedElementRecursive(HexEngine::Element* root)
		{
			if (root == nullptr)
				return nullptr;

			if (root->IsInputFocus())
				return root;

			for (auto* child : root->GetChildren())
			{
				if (auto* focused = FindFocusedElementRecursive(child); focused != nullptr)
					return focused;
			}

			return nullptr;
		}

		bool IsDescendantOfElement(const HexEngine::Element* element, const HexEngine::Element* ancestor)
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

		bool LoadVariantAssetData(const fs::path& variantPath, VariantAssetData& outData)
		{
			outData = {};
			if (variantPath.empty())
				return false;

			HexEngine::DiskFile file(variantPath, std::ios::in | std::ios::binary);
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

			const auto basePrefabRef = variantIt->value("basePrefab", std::string());
			if (basePrefabRef.empty())
				return false;

			fs::path basePath = fs::path(basePrefabRef);
			if (basePath.is_relative())
				basePath = (variantPath.parent_path() / basePath).lexically_normal();

			outData.rootJson = std::move(rootJson);
			outData.basePrefabAbsolutePath = std::move(basePath);
			outData.basePrefabReference = basePrefabRef;
			return true;
		}

		bool WriteJsonAssetFile(const fs::path& filePath, const json& content)
		{
			std::ofstream output(filePath, std::ios::out | std::ios::trunc | std::ios::binary);
			if (!output.is_open())
				return false;

			output << content.dump(2);
			output.close();
			return true;
		}

		bool ApplyGenericPrefabOverridePatchesToEntity(
			HexEngine::Entity* entity,
			const std::vector<HexEngine::Entity::PrefabOverridePatch>& patches);

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

		struct PrefabEntityOverrideState
		{
			std::unordered_set<std::string> overridePaths;
			std::vector<HexEngine::Entity::PrefabOverridePatch> overridePatches;
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

		struct PrefabOverrideStateSet
		{
			std::unordered_map<std::string, PrefabEntityOverrideState> byNodeId;
			std::unordered_map<std::string, PrefabEntityOverrideState> byPath;
		};

		void CollectPrefabOverrideStateRecursive(
			HexEngine::Entity* entity,
			const std::string& pathKey,
			PrefabOverrideStateSet& outStates)
		{
			if (entity == nullptr)
				return;

			const auto& entityOverrides = entity->GetPrefabPropertyOverrides();
			const auto& entityOverridePatches = entity->GetPrefabOverridePatches();
			if (!entityOverrides.empty() || !entityOverridePatches.empty())
			{
				PrefabEntityOverrideState state;
				state.overridePaths = entityOverrides;
				state.overridePatches = entityOverridePatches;
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

				if (!pathKey.empty())
				{
					outStates.byPath[pathKey] = state;
				}

				const std::string nodeId = entity->EnsurePrefabNodeId();
				if (!nodeId.empty())
				{
					outStates.byNodeId[nodeId] = std::move(state);
				}
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

		const PrefabEntityOverrideState* ResolvePrefabOverrideState(
			HexEngine::Entity* entity,
			const std::string& pathKey,
			const PrefabOverrideStateSet& overrideStates)
		{
			if (entity != nullptr)
			{
				const std::string& nodeId = entity->GetPrefabNodeId();
				if (!nodeId.empty())
				{
					if (auto nodeIt = overrideStates.byNodeId.find(nodeId); nodeIt != overrideStates.byNodeId.end())
					{
						return &nodeIt->second;
					}
				}
			}

			if (!pathKey.empty())
			{
				if (auto pathIt = overrideStates.byPath.find(pathKey); pathIt != overrideStates.byPath.end())
				{
					return &pathIt->second;
				}
			}

			return nullptr;
		}

		void ApplyPrefabOverrideStateRecursive(
			HexEngine::Entity* entity,
			const std::string& pathKey,
			const PrefabOverrideStateSet& overrideStates)
		{
			if (entity == nullptr)
				return;

			entity->ClearPrefabPropertyOverrides();
			entity->ClearPrefabOverridePatches();

			if (const auto* state = ResolvePrefabOverrideState(entity, pathKey, overrideStates); state != nullptr)
			{
				for (const auto& overridePath : state->overridePaths)
				{
					entity->MarkPrefabPropertyOverride(overridePath);
				}
				entity->SetPrefabOverridePatches(state->overridePatches);

				const bool hasGenericPatches = !state->overridePatches.empty();
				if (!hasGenericPatches)
				{
					// Legacy fallback for older instances that only track coarse override keys.
					if (state->overridePaths.find(kPrefabOverrideTransformPosition) != state->overridePaths.end())
						entity->SetPosition(state->position);

					if (state->overridePaths.find(kPrefabOverrideTransformRotation) != state->overridePaths.end())
						entity->SetRotation(state->rotation);

					if (state->overridePaths.find(kPrefabOverrideTransformScale) != state->overridePaths.end())
						entity->SetScale(state->scale);

					if (state->overridePaths.find(kPrefabOverrideStaticMeshMesh) != state->overridePaths.end())
					{
						if (auto* staticMesh = entity->GetComponent<HexEngine::StaticMeshComponent>(); staticMesh != nullptr)
						{
							if (state->hasMeshPath && !state->meshPath.empty())
							{
								if (auto mesh = HexEngine::Mesh::Create(state->meshPath); mesh != nullptr)
								{
									staticMesh->SetMesh(mesh);
								}
							}
						}
					}

					if (state->overridePaths.find(kPrefabOverrideStaticMeshMaterial) != state->overridePaths.end())
					{
						if (auto* staticMesh = entity->GetComponent<HexEngine::StaticMeshComponent>(); staticMesh != nullptr)
						{
							if (state->hasMaterialPath && !state->materialPath.empty())
							{
								if (auto material = HexEngine::Material::Create(state->materialPath); material != nullptr)
								{
									staticMesh->SetMaterial(material);
								}
							}
						}
					}

					if (state->overridePaths.find(kPrefabOverrideStaticMeshUVScale) != state->overridePaths.end())
					{
						if (auto* staticMesh = entity->GetComponent<HexEngine::StaticMeshComponent>(); staticMesh != nullptr)
						{
							staticMesh->SetUVScale(state->uvScale);
						}
					}

					if (state->overridePaths.find(kPrefabOverrideStaticMeshShadowCullMode) != state->overridePaths.end())
					{
						if (auto* staticMesh = entity->GetComponent<HexEngine::StaticMeshComponent>(); staticMesh != nullptr)
						{
							staticMesh->SetShadowCullMode(state->shadowCullMode);
						}
					}

					if (state->overridePaths.find(kPrefabOverrideStaticMeshOffsetPosition) != state->overridePaths.end())
					{
						if (auto* staticMesh = entity->GetComponent<HexEngine::StaticMeshComponent>(); staticMesh != nullptr)
						{
							staticMesh->SetOffsetPosition(state->offsetPosition);
						}
					}
				}

				ApplyGenericPrefabOverridePatchesToEntity(entity, state->overridePatches);
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

		json* FindMutableComponentEntryByName(json& components, const std::string& componentName)
		{
			if (!components.is_array() || componentName.empty())
				return nullptr;

			for (auto& component : components)
			{
				if (component.is_object() && component.value("name", std::string()) == componentName)
					return &component;
			}

			return nullptr;
		}

		template<typename TValue>
		json SerializeComponentFieldValue(const char* fieldKey, const TValue& value)
		{
			json temp = json::object();
			HexEngine::JsonFile serializer(fs::path(), std::ios::in);
			serializer.Serialize(temp, fieldKey, value);

			const auto it = temp.find(fieldKey);
			return it != temp.end() ? *it : json();
		}

		bool SetStaticMeshMaterialPathInSnapshot(json& staticMeshComponent, const fs::path& materialPath)
		{
			if (!staticMeshComponent.is_object())
				return false;

			auto meshIt = staticMeshComponent.find("mesh");
			if (meshIt == staticMeshComponent.end() || !meshIt->is_object() || meshIt->empty())
				return false;

			auto meshEntryIt = meshIt->begin();
			if (!meshEntryIt.value().is_object())
				return false;

			auto& meshEntry = meshEntryIt.value();
			auto materialsIt = meshEntry.find("materials");
			if (materialsIt == meshEntry.end() || !materialsIt->is_array() || materialsIt->empty())
				return false;

			(*materialsIt)[0] = materialPath.string();
			return true;
		}

		bool HasMatchingComponentLayout(const json& beforeComponents, const json& afterComponents)
		{
			if (!beforeComponents.is_array() || !afterComponents.is_array() || beforeComponents.size() != afterComponents.size())
				return false;

			for (size_t i = 0; i < beforeComponents.size(); ++i)
			{
				if (!beforeComponents[i].is_object() || !afterComponents[i].is_object())
					return false;

				const auto beforeName = beforeComponents[i].value("name", std::string());
				const auto afterName = afterComponents[i].value("name", std::string());
				if (beforeName.empty() || afterName.empty() || beforeName != afterName)
					return false;
			}

			return true;
		}

		std::vector<HexEngine::Entity::PrefabOverridePatch> BuildGenericPrefabOverridePatches(
			const json& beforeComponents,
			const json& afterComponents)
		{
			std::vector<HexEngine::Entity::PrefabOverridePatch> patches;
			if (!beforeComponents.is_array() || !afterComponents.is_array())
				return patches;

			if (!HasMatchingComponentLayout(beforeComponents, afterComponents))
			{
				json diffOps = json::diff(beforeComponents, afterComponents);
				if (!diffOps.is_array())
					return patches;

				for (const auto& diffOp : diffOps)
				{
					if (!diffOp.is_object())
						continue;

					const auto op = diffOp.value("op", std::string());
					const auto path = diffOp.value("path", std::string());
					if (op.empty() || path.empty())
						continue;

					HexEngine::Entity::PrefabOverridePatch patch;
					patch.componentName = kPrefabOverrideComponentArrayPatchTarget;
					patch.path = path;
					patch.op = op;

					const auto valueIt = diffOp.find("value");
					patch.value = valueIt != diffOp.end() ? *valueIt : json();
					patches.push_back(std::move(patch));
				}

				return patches;
			}

			for (const auto& afterComponent : afterComponents)
			{
				if (!afterComponent.is_object())
					continue;

				const auto componentName = afterComponent.value("name", std::string());
				if (componentName.empty())
					continue;

				const auto* beforeComponent = FindComponentEntryByName(beforeComponents, componentName);
				if (beforeComponent == nullptr || !beforeComponent->is_object())
					continue;

				if (*beforeComponent == afterComponent)
					continue;

				json diffOps = json::diff(*beforeComponent, afterComponent);
				if (!diffOps.is_array())
					continue;

				for (const auto& diffOp : diffOps)
				{
					if (!diffOp.is_object())
						continue;

					const auto op = diffOp.value("op", std::string());
					const auto path = diffOp.value("path", std::string());
					if (op.empty() || path.empty())
						continue;

					if (path == "/name" || path.rfind("/name/", 0) == 0)
						continue;

					HexEngine::Entity::PrefabOverridePatch patch;
					patch.componentName = componentName;
					patch.path = path;
					patch.op = op;

					const auto valueIt = diffOp.find("value");
					patch.value = valueIt != diffOp.end() ? *valueIt : json();
					patches.push_back(std::move(patch));
				}
			}

			return patches;
		}

		HexEngine::Entity* FindEntityByPrefabNodeIdInScene(const std::shared_ptr<HexEngine::Scene>& scene, const std::string& nodeId)
		{
			if (scene == nullptr || nodeId.empty())
				return nullptr;

			for (const auto& bySignature : scene->GetEntities())
			{
				for (auto* entity : bySignature.second)
				{
					if (entity != nullptr && entity->GetPrefabNodeId() == nodeId)
						return entity;
				}
			}

			return nullptr;
		}

		HexEngine::Entity* ResolvePrefabInstanceRootEntity(HexEngine::Entity* entity)
		{
			auto* root = entity;
			while (root != nullptr && !root->IsPrefabInstanceRoot())
			{
				root = root->GetParent();
			}
			return root;
		}

		HexEngine::Entity* FindPrefabSourceRootInScene(
			const std::shared_ptr<HexEngine::Scene>& prefabScene,
			const std::string& preferredName,
			const std::string& preferredNodeId)
		{
			if (prefabScene == nullptr)
				return nullptr;

			if (!preferredNodeId.empty())
			{
				if (auto* byNodeId = FindEntityByPrefabNodeIdInScene(prefabScene, preferredNodeId); byNodeId != nullptr)
				{
					return byNodeId;
				}
			}

			if (!preferredName.empty())
			{
				for (const auto& bySignature : prefabScene->GetEntities())
				{
					for (auto* candidate : bySignature.second)
					{
						if (candidate != nullptr && candidate->GetName() == preferredName)
							return candidate;
					}
				}
			}

			for (const auto& bySignature : prefabScene->GetEntities())
			{
				for (auto* candidate : bySignature.second)
				{
					if (candidate != nullptr && candidate->GetParent() == nullptr)
						return candidate;
				}
			}

			return nullptr;
		}

		bool ResolvePrefabSourceEntityAndSnapshots(
			HexEngine::Entity* entity,
			std::shared_ptr<HexEngine::Scene>& outPrefabScene,
			HexEngine::Entity*& outSourceEntity,
			HexEngine::Entity*& outInstanceRoot,
			json& outBaseComponents,
			json& outEditedComponents,
			fs::path* outPrefabPath = nullptr)
		{
			outPrefabScene.reset();
			outSourceEntity = nullptr;
			outInstanceRoot = nullptr;
			outBaseComponents = json::array();
			outEditedComponents = json::array();

			if (entity == nullptr || !entity->IsPrefabInstance())
				return false;

			auto* instanceRoot = ResolvePrefabInstanceRootEntity(entity);
			if (instanceRoot == nullptr)
				return false;

			const fs::path prefabPath = instanceRoot->GetPrefabSourcePath();
			if (prefabPath.empty())
				return false;

			auto* sceneManager = HexEngine::g_pEnv->_sceneManager;
			if (sceneManager == nullptr)
				return false;

			auto prefabScene = sceneManager->CreateEmptyScene(false, nullptr, false);
			if (!HexEngine::g_pEnv->_prefabLoader->LoadPrefabAssetToScene(prefabPath, prefabScene))
				return false;

			auto* sourceRoot = FindPrefabSourceRootInScene(
				prefabScene,
				instanceRoot->GetPrefabRootEntityName(),
				instanceRoot->GetPrefabNodeId());
			if (sourceRoot == nullptr)
				return false;

			HexEngine::Entity* sourceEntity = nullptr;
			const std::string nodeId = entity->GetPrefabNodeId();
			if (!nodeId.empty())
			{
				sourceEntity = FindEntityByPrefabNodeIdInScene(prefabScene, nodeId);
			}

			if (sourceEntity == nullptr && entity == instanceRoot)
			{
				sourceEntity = sourceRoot;
			}

			if (sourceEntity == nullptr)
				return false;

			if (!CaptureEntityComponentsSnapshot(sourceEntity, outBaseComponents) ||
				!CaptureEntityComponentsSnapshot(entity, outEditedComponents))
			{
				return false;
			}

			outPrefabScene = prefabScene;
			outSourceEntity = sourceEntity;
			outInstanceRoot = instanceRoot;
			if (outPrefabPath != nullptr)
			{
				*outPrefabPath = prefabPath;
			}
			return true;
		}

		std::string BuildPrefabOverrideSelectionKey(const std::string& componentName, const std::string& path)
		{
			return componentName + "\n" + path;
		}

		bool IsPatchPathMatchingSelection(const std::string& patchPath, const std::string& selectedPath)
		{
			if (patchPath == selectedPath)
				return true;

			if (selectedPath.empty())
				return false;

			const std::string selectedPrefix = selectedPath + "/";
			if (patchPath.rfind(selectedPrefix, 0) == 0)
				return true;

			const std::string patchPrefix = patchPath + "/";
			return selectedPath.rfind(patchPrefix, 0) == 0;
		}

		void FilterOverridePatchesBySelection(
			const std::vector<HexEngine::Entity::PrefabOverridePatch>& patches,
			const std::unordered_set<std::string>& selectedKeys,
			bool keepSelected,
			std::vector<HexEngine::Entity::PrefabOverridePatch>& outPatches)
		{
			outPatches.clear();
			for (const auto& patch : patches)
			{
				const bool selected = selectedKeys.find(BuildPrefabOverrideSelectionKey(patch.componentName, patch.path)) != selectedKeys.end();
				if ((keepSelected && selected) || (!keepSelected && !selected))
				{
					outPatches.push_back(patch);
				}
			}
		}

		bool ApplyOverridePatchesToComponentSnapshot(
			json& componentSnapshot,
			const std::vector<HexEngine::Entity::PrefabOverridePatch>& patches)
		{
			if (!componentSnapshot.is_array())
				return false;

			if (patches.empty())
				return true;

			std::unordered_map<std::string, json> patchDocsByComponent;
			json componentArrayPatchDoc = json::array();

			for (const auto& patch : patches)
			{
				if (patch.componentName.empty() || patch.path.empty() || patch.op.empty())
					continue;

				json op = json::object();
				op["op"] = patch.op;
				op["path"] = patch.path;
				if (patch.op != "remove")
					op["value"] = patch.value;

				if (patch.componentName == kPrefabOverrideComponentArrayPatchTarget)
				{
					componentArrayPatchDoc.push_back(std::move(op));
					continue;
				}

				auto& patchDoc = patchDocsByComponent[patch.componentName];
				if (!patchDoc.is_array())
					patchDoc = json::array();
				patchDoc.push_back(std::move(op));
			}

			if (componentArrayPatchDoc.is_array() && !componentArrayPatchDoc.empty())
			{
				try
				{
					componentSnapshot = componentSnapshot.patch(componentArrayPatchDoc);
				}
				catch (const std::exception&)
				{
					return false;
				}
			}

			std::unordered_map<std::string, json*> componentsByName;
			for (auto& component : componentSnapshot)
			{
				if (!component.is_object())
					continue;

				const auto componentName = component.value("name", std::string());
				if (componentName.empty())
					continue;

				componentsByName[componentName] = &component;
			}

			for (auto& [componentName, patchDoc] : patchDocsByComponent)
			{
				auto it = componentsByName.find(componentName);
				if (it == componentsByName.end() || it->second == nullptr)
					continue;

				try
				{
					*it->second = it->second->patch(patchDoc);
				}
				catch (const std::exception&)
				{
					return false;
				}
			}

			return true;
		}

		void CollectOverriddenComponentNamesFromSnapshots(
			const json& baseComponents,
			const json& editedComponents,
			std::unordered_set<std::string>& outComponentNames)
		{
			outComponentNames.clear();

			const auto patches = BuildGenericPrefabOverridePatches(baseComponents, editedComponents);
			bool hasArrayLevelPatch = false;
			for (const auto& patch : patches)
			{
				if (patch.componentName == kPrefabOverrideComponentArrayPatchTarget)
				{
					hasArrayLevelPatch = true;
					continue;
				}

				if (!patch.componentName.empty())
					outComponentNames.insert(patch.componentName);
			}

			if (!hasArrayLevelPatch)
				return;

			if (!baseComponents.is_array() || !editedComponents.is_array())
				return;

			for (const auto& editedComponent : editedComponents)
			{
				if (!editedComponent.is_object())
					continue;

				const auto componentName = editedComponent.value("name", std::string());
				if (componentName.empty())
					continue;

				const auto* baseComponent = FindComponentEntryByName(baseComponents, componentName);
				if (baseComponent == nullptr || *baseComponent != editedComponent)
				{
					outComponentNames.insert(componentName);
				}
			}
		}

		void CollectSceneEntitiesByNodeId(HexEngine::Scene* scene, std::unordered_map<std::string, HexEngine::Entity*>& outByNodeId)
		{
			outByNodeId.clear();
			if (scene == nullptr)
				return;

			for (const auto& bySignature : scene->GetEntities())
			{
				for (auto* entity : bySignature.second)
				{
					if (entity == nullptr || entity->HasFlag(HexEngine::EntityFlags::DoNotSave))
						continue;

					const auto nodeId = entity->EnsurePrefabNodeId();
					if (nodeId.empty())
						continue;

					outByNodeId[nodeId] = entity;
				}
			}
		}

		std::vector<VariantPatchEntry> BuildVariantPatchesFromScenes(
			HexEngine::Scene* baseScene,
			HexEngine::Scene* editedScene)
		{
			std::vector<VariantPatchEntry> result;
			if (baseScene == nullptr || editedScene == nullptr)
				return result;

			std::unordered_map<std::string, HexEngine::Entity*> baseByNodeId;
			std::unordered_map<std::string, HexEngine::Entity*> editedByNodeId;
			CollectSceneEntitiesByNodeId(baseScene, baseByNodeId);
			CollectSceneEntitiesByNodeId(editedScene, editedByNodeId);

			for (const auto& [nodeId, editedEntity] : editedByNodeId)
			{
				if (editedEntity == nullptr)
					continue;

				auto baseIt = baseByNodeId.find(nodeId);
				if (baseIt == baseByNodeId.end() || baseIt->second == nullptr)
				{
					LOG_WARN("Variant save skipped node '%s' because base prefab has no matching node id.", nodeId.c_str());
					continue;
				}

				json baseComponents = json::array();
				json editedComponents = json::array();
				if (!CaptureEntityComponentsSnapshot(baseIt->second, baseComponents) ||
					!CaptureEntityComponentsSnapshot(editedEntity, editedComponents))
				{
					continue;
				}

				auto patches = BuildGenericPrefabOverridePatches(baseComponents, editedComponents);
				for (auto& patch : patches)
				{
					VariantPatchEntry entry;
					entry.nodeId = nodeId;
					entry.patch = std::move(patch);
					result.push_back(std::move(entry));
				}
			}

			std::sort(result.begin(), result.end(),
				[](const VariantPatchEntry& a, const VariantPatchEntry& b)
				{
					if (a.nodeId != b.nodeId)
						return a.nodeId < b.nodeId;
					if (a.patch.componentName != b.patch.componentName)
						return a.patch.componentName < b.patch.componentName;
					if (a.patch.path != b.patch.path)
						return a.patch.path < b.patch.path;
					return a.patch.op < b.patch.op;
				});

			return result;
		}

		bool SaveVariantAssetFromEditedScene(
			const fs::path& variantPrefabPath,
			HexEngine::Scene* editedScene,
			HexEngine::SceneManager* sceneManager,
			size_t* outPatchCount = nullptr)
		{
			if (outPatchCount != nullptr)
				*outPatchCount = 0;

			if (variantPrefabPath.empty() || editedScene == nullptr || sceneManager == nullptr)
				return false;

			VariantAssetData variantData;
			if (!LoadVariantAssetData(variantPrefabPath, variantData))
				return false;

			auto baseScene = sceneManager->CreateEmptyScene(false, nullptr, false);
			if (!HexEngine::g_pEnv->_prefabLoader->LoadPrefabAssetToScene(variantData.basePrefabAbsolutePath, baseScene))
				return false;

			auto patches = BuildVariantPatchesFromScenes(baseScene.get(), editedScene);
			auto& variantObject = variantData.rootJson["variant"];
			if (!variantObject.is_object())
				variantObject = json::object();

			if (variantData.basePrefabReference.empty())
			{
				std::error_code relError;
				fs::path relativeBase = fs::relative(variantData.basePrefabAbsolutePath, variantPrefabPath.parent_path(), relError);
				variantData.basePrefabReference = (!relError && !relativeBase.empty())
					? relativeBase.generic_string()
					: variantData.basePrefabAbsolutePath.filename().generic_string();
			}

			variantObject["basePrefab"] = variantData.basePrefabReference;
			variantObject["patches"] = json::array();

			for (const auto& patchEntry : patches)
			{
				const auto& patch = patchEntry.patch;
				if (patchEntry.nodeId.empty() || patch.componentName.empty() || patch.path.empty() || patch.op.empty())
					continue;

				json patchJson = json::object();
				patchJson["nodeId"] = patchEntry.nodeId;
				patchJson["component"] = patch.componentName;
				patchJson["path"] = patch.path;
				patchJson["op"] = patch.op;
				if (patch.op != "remove")
					patchJson["value"] = patch.value;

				variantObject["patches"].push_back(std::move(patchJson));
			}

			variantData.rootJson["header"]["version"] = 2;
			if (!WriteJsonAssetFile(variantPrefabPath, variantData.rootJson))
				return false;

			if (outPatchCount != nullptr)
				*outPatchCount = patches.size();

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
					LOG_WARN("Prefab patch could not resolve component class '%s' on '%s'.",
						componentName.c_str(), entity->GetName().c_str());
					continue;
				}

				component = cls->newInstanceFn(entity);
				if (component == nullptr)
				{
					LOG_WARN("Prefab patch failed to instantiate component class '%s' on '%s'.",
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

		bool ApplyGenericPrefabOverridePatchesToEntity(
			HexEngine::Entity* entity,
			const std::vector<HexEngine::Entity::PrefabOverridePatch>& patches)
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

			std::unordered_map<std::string, json> patchDocsByComponent;
			json componentArrayPatchDoc = json::array();
			for (const auto& patch : patches)
			{
				if (patch.componentName.empty() || patch.path.empty() || patch.op.empty())
					continue;

				if (patch.componentName == kPrefabOverrideComponentArrayPatchTarget)
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

			bool appliedComponentArrayPatch = false;
			if (componentArrayPatchDoc.is_array() && !componentArrayPatchDoc.empty())
			{
				try
				{
					json patchedComponents = componentsIt->patch(componentArrayPatchDoc);
					*componentsIt = std::move(patchedComponents);
					appliedComponentArrayPatch = true;
				}
				catch (const std::exception& e)
				{
					LOG_WARN("Failed to apply prefab component-array patches on '%s': %s",
						entity->GetName().c_str(), e.what());
				}
			}

			if (appliedComponentArrayPatch && !patchDocsByComponent.empty())
			{
				componentsByName.clear();
				for (auto& componentJson : *componentsIt)
				{
					if (!componentJson.is_object())
						continue;

					const auto componentName = componentJson.value("name", std::string());
					if (componentName.empty())
						continue;

					componentsByName[componentName] = &componentJson;
				}
			}

			std::unordered_set<std::string> patchedComponents;
			for (auto& [componentName, patchDoc] : patchDocsByComponent)
			{
				auto componentIt = componentsByName.find(componentName);
				if (componentIt == componentsByName.end() || componentIt->second == nullptr)
					continue;

				try
				{
					json patchedComponent = componentIt->second->patch(patchDoc);
					*componentIt->second = std::move(patchedComponent);
					patchedComponents.insert(componentName);
				}
				catch (const std::exception& e)
				{
					LOG_WARN("Failed to apply prefab override patch on '%s::%s': %s",
						entity->GetName().c_str(), componentName.c_str(), e.what());
				}
			}

			if (appliedComponentArrayPatch)
			{
				ApplySerializedComponentArrayToEntity(entity, *componentsIt, serializer);
			}

			for (const auto& componentName : patchedComponents)
			{
				auto componentIt = componentsByName.find(componentName);
				if (componentIt == componentsByName.end() || componentIt->second == nullptr)
					continue;

				if (auto* component = entity->GetComponentByClassName(componentName); component != nullptr)
				{
					json componentData = *componentIt->second;
					component->Deserialize(componentData, &serializer);
				}
			}

			return appliedComponentArrayPatch || !patchedComponents.empty();
		}
	}

	void PrefabController::SetDependencies(
		HexEngine::IEntityListener* stageEntityListener,
		GameIntegrator* integrator,
		Inspector* inspector,
		EntityList* entityList,
		Explorer* explorer)
	{
		_stageEntityListener = stageEntityListener;
		_integrator = integrator;
		_inspector = inspector;
		_entityList = entityList;
		_explorer = explorer;
		LoadEditorPrefs();
	}

	void PrefabController::RefreshPrefabAssetPreview(const fs::path& prefabPath)
	{
		if (prefabPath.empty() || prefabPath.extension() != ".hprefab")
			return;

		if (_explorer != nullptr)
		{
			_explorer->InvalidateAssetPreview(prefabPath);
		}
		else if (HexEngine::g_pEnv != nullptr && HexEngine::g_pEnv->_iconService != nullptr)
		{
			HexEngine::g_pEnv->_iconService->RemoveIcon(prefabPath);
			HexEngine::g_pEnv->_iconService->PushFilePathForIconGeneration(prefabPath);
		}
	}

	void PrefabController::EnsurePrefabStageCameraAndLighting(const std::shared_ptr<HexEngine::Scene>& scene)
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

	void PrefabController::FramePrefabStageCamera(const std::shared_ptr<HexEngine::Scene>& scene)
	{
		if (scene == nullptr)
			return;

		auto* camera = scene->GetMainCamera();
		if (camera == nullptr)
			return;

		if (!HexEngine::SceneFramingUtils::FrameCameraToSceneBounds(scene.get(), camera, true) &&
			!HexEngine::SceneFramingUtils::FrameCameraToSceneBounds(scene.get(), camera, false))
			return;

		if (auto* pvs = camera->GetPVS(); pvs != nullptr)
		{
			pvs->ForceRebuild();
		}
	}

	HexEngine::Entity* PrefabController::FindPrefabRootInScene(
		const std::shared_ptr<HexEngine::Scene>& scene,
		const std::string& preferredName,
		const std::string& preferredNodeId) const
	{
		if (scene == nullptr)
			return nullptr;

		if (!preferredNodeId.empty())
		{
			for (const auto& bySignature : scene->GetEntities())
			{
				for (auto* entity : bySignature.second)
				{
					if (entity != nullptr &&
						entity->GetParent() == nullptr &&
						entity->GetPrefabNodeId() == preferredNodeId)
					{
						return entity;
					}
				}
			}
		}

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

	void PrefabController::CollectEntityHierarchy(HexEngine::Entity* root, std::vector<HexEngine::Entity*>& outEntities) const
	{
		if (root == nullptr || root->IsPendingDeletion())
			return;

		outEntities.push_back(root);

		for (auto* child : root->GetChildren())
		{
			CollectEntityHierarchy(child, outEntities);
		}
	}

	HexEngine::Entity* PrefabController::CloneEntityHierarchyToScene(
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

		clonedEntity->SetPrefabNodeId(sourceEntity->EnsurePrefabNodeId());
		if (isRootInstance && !prefabSourcePath.empty())
			clonedEntity->SetPrefabRevision(HexEngine::PrefabLoader::ComputePrefabRevision(prefabSourcePath));

		for (auto* child : sourceEntity->GetChildren())
		{
			CloneEntityHierarchyToScene(targetScene, child, clonedEntity, prefabSourcePath, prefabRootName, false);
		}

		return clonedEntity;
	}

	HexEngine::Entity* PrefabController::FindPrefabInstanceRoot(HexEngine::Entity* entity) const
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

	void PrefabController::RefreshInspectorForPrefabInstance(HexEngine::Entity* changedEntity)
	{
		if (changedEntity == nullptr || _inspector == nullptr)
			return;

		auto* inspecting = _inspector->GetInspectingEntity();
		if (inspecting == nullptr)
			return;

		auto* changedRoot = FindPrefabInstanceRoot(changedEntity);
		auto* inspectingRoot = FindPrefabInstanceRoot(inspecting);
		if (changedRoot != nullptr && changedRoot == inspectingRoot)
		{
			// Let Inspector decide when to safely apply the refresh (for example after popup dialogs close).
			_inspector->RequestForcedRefresh(inspecting);
		}
	}

	// =====================================================================
	// Prefab instance sync
	//
	// When a prefab asset changes, instances are brought up to date IN PLACE:
	// every instance entity is matched to its prefab entity (prefab node id,
	// then name with the scene's numeric de-dup suffix ignored), its components
	// are rebuilt from the prefab's plus the instance's recorded override
	// patches, and only components whose data actually changed are
	// re-deserialized. Children the prefab added are cloned in, children it
	// removed are destroyed; children and components added in the scene (not
	// from the prefab) are left alone. Entities keep their identity, so
	// references, selection and undo history stay valid. The previous
	// implementation destroyed and re-cloned each instance.
	//
	// Instance roots carry the asset revision they were built from
	// (Entity::GetPrefabRevision, PrefabLoader::ComputePrefabRevision);
	// a mismatch marks them out of date.
	// =====================================================================
	namespace
	{
		constexpr const char* kSyncPolicyPrefKey = "prefabInstanceSync";

		fs::path GetEditorPrefsPath()
		{
			wchar_t* localAppData = nullptr;
			size_t length = 0;
			fs::path result;
			if (_wdupenv_s(&localAppData, &length, L"LOCALAPPDATA") == 0 && localAppData != nullptr)
				result = fs::path(localAppData) / L"HexEngine" / L"EditorPrefs.json";
			free(localAppData);
			return result;
		}

		json ReadEditorPrefsJson()
		{
			json prefs = json::object();
			const fs::path path = GetEditorPrefsPath();
			if (path.empty())
				return prefs;

			std::ifstream input(path, std::ios::binary);
			if (!input)
				return prefs;

			try
			{
				input >> prefs;
			}
			catch (const std::exception&)
			{
				prefs = json::object();
			}
			return prefs.is_object() ? prefs : json::object();
		}

		std::string StripTrailingDigits(const std::string& name)
		{
			size_t end = name.size();
			while (end > 0 && std::isdigit(static_cast<unsigned char>(name[end - 1])))
				--end;
			return end == 0 ? name : name.substr(0, end);
		}

		HexEngine::Scene* FindLoadedSceneByName(const std::wstring& name)
		{
			auto* sceneManager = HexEngine::g_pEnv->_sceneManager;
			if (sceneManager == nullptr)
				return nullptr;

			for (const auto& scene : sceneManager->GetAllScenes())
			{
				if (scene != nullptr && scene->GetName() == name)
					return scene.get();
			}
			return nullptr;
		}

		// One JSON-patch op, tolerant of a prefab that changed underneath it: a
		// replace of a field the prefab no longer writes becomes an add.
		bool ApplySinglePatchOp(json& target, const std::string& op, const std::string& path, const json& value)
		{
			json patchOp = json::object();
			patchOp["op"] = op;
			patchOp["path"] = path;
			if (op != "remove")
				patchOp["value"] = value;

			try
			{
				target = target.patch(json::array({ patchOp }));
				return true;
			}
			catch (const std::exception&)
			{
			}

			if (op == "replace")
			{
				patchOp["op"] = "add";
				try
				{
					target = target.patch(json::array({ patchOp }));
					return true;
				}
				catch (const std::exception&)
				{
				}
			}
			return false;
		}

		// The prefab's component array with this instance's overrides re-applied.
		//
		// Per-component patches apply by component name. Component-array patches
		// ("__components__", recorded when the instance added/removed components)
		// are index based, and the prefab may have reordered its components since,
		// so indices are mapped to names through the instance's own array (the
		// order the patch was recorded against): a whole-component add/replace is
		// upserted by its "name", a sub-path op is redirected to the named
		// component, and a removal keeps absent whichever prefab components the
		// instance doesn't have.
		json BuildDesiredInstanceComponents(const json& sourceComponents, const json& currentComponents, HexEngine::Entity* instance, bool isRoot)
		{
			json desired = sourceComponents.is_array() ? sourceComponents : json::array();
			const auto& patches = instance->GetPrefabOverridePatches();
			bool instanceRemovedComponents = false;
			size_t skipped = 0;

			for (const auto& patch : patches)
			{
				if (patch.componentName.empty() || patch.path.empty() || patch.op.empty())
					continue;

				if (patch.componentName != kPrefabOverrideComponentArrayPatchTarget)
				{
					auto* target = FindMutableComponentEntryByName(desired, patch.componentName);
					if (target == nullptr || !ApplySinglePatchOp(*target, patch.op, patch.path, patch.value))
						++skipped;
					continue;
				}

				const size_t slash = patch.path.find('/', 1);
				const std::string indexText = patch.path.substr(1, slash == std::string::npos ? std::string::npos : slash - 1);
				const std::string subPath = slash == std::string::npos ? std::string() : patch.path.substr(slash);

				if (subPath.empty())
				{
					if (patch.op == "remove")
					{
						instanceRemovedComponents = true;
						continue;
					}

					const std::string name = patch.value.is_object() ? patch.value.value("name", std::string()) : std::string();
					if (name.empty())
					{
						++skipped;
						continue;
					}

					if (auto* existing = FindMutableComponentEntryByName(desired, name); existing != nullptr)
						*existing = patch.value;
					else
						desired.push_back(patch.value);
					continue;
				}

				const bool numeric = !indexText.empty() &&
					std::all_of(indexText.begin(), indexText.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
				std::string name;
				if (numeric && currentComponents.is_array())
				{
					const size_t index = static_cast<size_t>(std::stoull(indexText));
					if (index < currentComponents.size() && currentComponents[index].is_object())
						name = currentComponents[index].value("name", std::string());
				}

				auto* target = name.empty() ? nullptr : FindMutableComponentEntryByName(desired, name);
				if (target == nullptr || !ApplySinglePatchOp(*target, patch.op, subPath, patch.value))
					++skipped;
			}

			if (instanceRemovedComponents)
			{
				json kept = json::array();
				for (const auto& component : desired)
				{
					const std::string name = component.is_object() ? component.value("name", std::string()) : std::string();
					if (name == HexEngine::Transform::_GetComponentName() || FindComponentEntryByName(currentComponents, name) != nullptr)
						kept.push_back(component);
				}
				desired = std::move(kept);
			}

			// Legacy instances that only carry coarse override keys: keep their
			// current values for exactly those properties.
			const auto& coarse = instance->GetPrefabPropertyOverrides();
			if (patches.empty() && !coarse.empty())
			{
				auto* desiredTransform = FindMutableComponentEntryByName(desired, HexEngine::Transform::_GetComponentName());
				const auto* currentTransform = FindComponentEntryByName(currentComponents, HexEngine::Transform::_GetComponentName());
				const std::pair<const char*, const char*> transformKeys[] = {
					{ kPrefabOverrideTransformPosition, "_position" },
					{ kPrefabOverrideTransformRotation, "_rotation" },
					{ kPrefabOverrideTransformScale, "_scale" } };
				if (desiredTransform != nullptr && currentTransform != nullptr)
				{
					for (const auto& [overrideKey, field] : transformKeys)
					{
						if (coarse.count(overrideKey) != 0 && currentTransform->contains(field))
							(*desiredTransform)[field] = (*currentTransform)[field];
					}
				}

				const bool staticMeshOverridden = std::any_of(coarse.begin(), coarse.end(),
					[](const std::string& key) { return key.rfind("staticMesh.", 0) == 0; });
				if (staticMeshOverridden)
				{
					const char* meshName = HexEngine::StaticMeshComponent::_GetComponentName();
					auto* desiredMesh = FindMutableComponentEntryByName(desired, meshName);
					const auto* currentMesh = FindComponentEntryByName(currentComponents, meshName);
					if (desiredMesh != nullptr && currentMesh != nullptr)
						*desiredMesh = *currentMesh;
				}
			}

			// The root's placement belongs to the instance, not the prefab.
			if (isRoot)
			{
				auto* desiredTransform = FindMutableComponentEntryByName(desired, HexEngine::Transform::_GetComponentName());
				const auto* currentTransform = FindComponentEntryByName(currentComponents, HexEngine::Transform::_GetComponentName());
				if (desiredTransform != nullptr && currentTransform != nullptr)
				{
					for (const char* field : { "_position", "_rotation", "_scale" })
					{
						if (currentTransform->contains(field))
							(*desiredTransform)[field] = (*currentTransform)[field];
					}
				}
			}

			if (skipped > 0)
			{
				LOG_WARN("Prefab update: %zu override(s) on '%s' no longer match the prefab and were dropped.",
					skipped, instance->GetName().c_str());
			}
			return desired;
		}

		// Brings an entity's components to `desired`: removes the ones it lists no
		// longer (never the Transform), adds new ones, and deserializes only the
		// components that are new or whose data differs from `current`.
		void MergeComponentsIntoEntity(HexEngine::Entity* entity, const json& desired, const json& current)
		{
			HexEngine::JsonFile serializer(fs::path(), std::ios::in);

			std::unordered_set<std::string> desiredNames;
			for (const auto& component : desired)
			{
				if (component.is_object())
					desiredNames.insert(component.value("name", std::string()));
			}

			const auto existing = entity->GetAllComponents();
			for (auto* component : existing)
			{
				if (component == nullptr || component->GetComponentId() == HexEngine::Transform::_GetComponentId())
					continue;
				if (desiredNames.count(component->GetComponentName()) == 0)
					entity->RemoveComponent(component);
			}

			std::unordered_set<std::string> added;
			for (const auto& component : desired)
			{
				const std::string name = component.is_object() ? component.value("name", std::string()) : std::string();
				if (name.empty() || entity->GetComponentByClassName(name) != nullptr)
					continue;

				auto* cls = HexEngine::g_pEnv->_classRegistry->Find(name);
				if (cls == nullptr)
				{
					LOG_WARN("Prefab update could not resolve component class '%s' on '%s'.", name.c_str(), entity->GetName().c_str());
					continue;
				}

				auto* created = cls->newInstanceFn(entity);
				if (created == nullptr)
					continue;
				entity->AddComponent(created);
				added.insert(name);
			}

			const auto needsDeserialize = [&](const json& component)
			{
				const std::string name = component.value("name", std::string());
				if (added.count(name) != 0)
					return true;
				const auto* currentComponent = FindComponentEntryByName(current, name);
				return currentComponent == nullptr || *currentComponent != component;
			};

			// Transform first so dependent components see the final placement.
			for (int pass = 0; pass < 2; ++pass)
			{
				for (const auto& component : desired)
				{
					if (!component.is_object())
						continue;

					auto* target = entity->GetComponentByClassName(component.value("name", std::string()));
					if (target == nullptr)
						continue;

					const bool isTransform = target->GetComponentId() == HexEngine::Transform::_GetComponentId();
					if ((pass == 0) != isTransform || !needsDeserialize(component))
						continue;

					json data = component;
					target->Deserialize(data, &serializer);
				}
			}
		}

		bool CaptureInstanceSnapshot(HexEngine::Entity* root, Detail::EntityHierarchySnapshot& out)
		{
			if (root == nullptr || root->GetScene() == nullptr)
				return false;
			out = {};
			out.sceneName = root->GetScene()->GetName();
			out.rootEntityName = root->GetName();
			return Detail::CaptureEntityHierarchyRecursive(root, out);
		}

		// Puts an instance hierarchy back to `target` without recreating entities
		// that still exist: `other` is the state being left, whose extra entities
		// (children the sync added) are destroyed.
		bool ApplyInstanceSnapshotInPlace(const Detail::EntityHierarchySnapshot& target, const Detail::EntityHierarchySnapshot& other)
		{
			auto* scene = FindLoadedSceneByName(target.sceneName);
			if (scene == nullptr)
				return false;

			std::unordered_set<std::string> targetNames;
			for (const auto& snapshot : target.entities)
				targetNames.insert(snapshot.entityName);

			for (auto it = other.entities.rbegin(); it != other.entities.rend(); ++it)
			{
				if (targetNames.count(it->entityName) != 0)
					continue;
				if (auto* entity = scene->GetEntityByName(it->entityName); entity != nullptr && !entity->IsPendingDeletion())
					scene->DestroyEntity(entity);
			}

			HexEngine::JsonFile serializer(fs::path(), std::ios::in);
			for (const auto& snapshot : target.entities)
			{
				if (scene->GetEntityByName(snapshot.entityName) != nullptr)
					continue;
				json data = snapshot.entityData;
				if (HexEngine::Entity::LoadFromFile(data, snapshot.entityName, scene, &serializer) == nullptr)
					return false;
			}

			for (const auto& snapshot : target.entities)
			{
				if (snapshot.parentEntityName.empty())
					continue;
				auto* entity = scene->GetEntityByName(snapshot.entityName);
				auto* parent = scene->GetEntityByName(snapshot.parentEntityName);
				if (entity != nullptr && parent != nullptr && entity->GetParent() != parent)
					entity->SetParent(parent, false);
			}

			const uint32_t transformMask = 1u << HexEngine::Transform::_GetComponentId();
			for (const auto& snapshot : target.entities)
			{
				if (auto* entity = scene->GetEntityByName(snapshot.entityName); entity != nullptr)
				{
					json data = snapshot.entityData;
					entity->Deserialize(data, &serializer, transformMask);
				}
			}

			for (const auto& snapshot : target.entities)
			{
				auto* entity = scene->GetEntityByName(snapshot.entityName);
				if (entity == nullptr)
					continue;

				json data = snapshot.entityData;
				entity->Deserialize(data, &serializer);

				std::unordered_set<std::string> wanted;
				if (const auto it = snapshot.entityData.find("components"); it != snapshot.entityData.end() && it->is_array())
				{
					for (const auto& component : *it)
					{
						if (component.is_object())
							wanted.insert(component.value("name", std::string()));
					}
				}
				const auto existing = entity->GetAllComponents();
				for (auto* component : existing)
				{
					if (component != nullptr && component->GetComponentId() != HexEngine::Transform::_GetComponentId() &&
						wanted.count(component->GetComponentName()) == 0)
					{
						entity->RemoveComponent(component);
					}
				}
			}

			scene->ForceRebuildPVS();
			return true;
		}

		class PrefabInstanceSyncTransaction final : public IEditorTransaction
		{
		public:
			struct Item
			{
				Detail::EntityHierarchySnapshot before;
				Detail::EntityHierarchySnapshot after;
			};

			void Add(Item&& item) { _items.push_back(std::move(item)); }
			bool Empty() const { return _items.empty(); }

			virtual bool Undo() override
			{
				bool ok = true;
				for (auto it = _items.rbegin(); it != _items.rend(); ++it)
					ok = ApplyInstanceSnapshotInPlace(it->before, it->after) && ok;
				return ok;
			}

			virtual bool Redo() override
			{
				bool ok = true;
				for (auto& item : _items)
					ok = ApplyInstanceSnapshotInPlace(item.after, item.before) && ok;
				return ok;
			}

			virtual const char* GetLabel() const override { return "Update Prefab Instances"; }

		private:
			std::vector<Item> _items;
		};

		// "N prefab instances are out of date" with a checklist.
		class PrefabInstanceSyncDialog final : public HexEngine::Dialog
		{
		public:
			struct Row
			{
				std::wstring sceneName;
				std::string entityName;
				std::wstring label;
			};

			enum Action : int32_t { Skip = 0, UpdateAll = 1, UpdateSelected = 2 };
			// `dialog` identifies which dialog closed (a replaced one never reports).
			using OnClosed = std::function<void(const void* dialog, int32_t action, const std::vector<Row>& chosen, bool remember)>;

			static constexpr int32_t kWidth = 600;
			static constexpr int32_t kHeight = 440;

			PrefabInstanceSyncDialog(const std::wstring& message, std::vector<Row> rows, OnClosed onClosed) :
				Dialog(HexEngine::g_pEnv->GetUIManager().GetRootElement(),
					HexEngine::Point::GetScreenCenterWithOffset(-kWidth / 2, -kHeight / 2),
					HexEngine::Point(kWidth, kHeight),
					L"Prefab Changed"),
				_message(message),
				_rows(std::move(rows)),
				_checked(new bool[std::max<size_t>(_rows.size(), 1)]),
				_onClosed(std::move(onClosed))
			{
				for (size_t i = 0; i < _rows.size(); ++i)
					_checked[i] = true;

				auto* scroll = new HexEngine::ScrollView(this, HexEngine::Point(10, 46), HexEngine::Point(kWidth - 20, kHeight - 46 - 76));
				int32_t y = 2;
				for (size_t i = 0; i < _rows.size(); ++i)
				{
					new HexEngine::Checkbox(scroll->GetContentRoot(), HexEngine::Point(4, y), HexEngine::Point(kWidth - 48, 18), _rows[i].label, &_checked[i]);
					y += 20;
				}
				scroll->SetManualContentHeight(y + 4);

				new HexEngine::Checkbox(this, HexEngine::Point(10, kHeight - 64), HexEngine::Point(330, 18),
					L"Remember my choice (Update all / Skip)", &_remember);

				new HexEngine::Button(this, HexEngine::Point(kWidth - 384, kHeight - 38), HexEngine::Point(110, 24), L"Skip",
					[this](HexEngine::Button*) { Finish(Skip); return true; });
				new HexEngine::Button(this, HexEngine::Point(kWidth - 266, kHeight - 38), HexEngine::Point(126, 24), L"Update selected",
					[this](HexEngine::Button*) { Finish(UpdateSelected); return true; });
				auto* updateAll = new HexEngine::Button(this, HexEngine::Point(kWidth - 132, kHeight - 38), HexEngine::Point(120, 24), L"Update all",
					[this](HexEngine::Button*) { Finish(UpdateAll); return true; });
				updateAll->SetHighlightOverride(math::Color(HEX_RGBA_TO_FLOAT4(30, 180, 90, 255)));
			}

			virtual bool OnInputEvent(HexEngine::InputEvent event, HexEngine::InputData* data) override
			{
				const bool handled = Dialog::OnInputEvent(event, data);
				// Closed with the title-bar X: same as Skip, without remembering.
				if (_wantsDeletion && !_finished)
				{
					_finished = true;
					if (_onClosed)
						_onClosed(this, Skip, {}, false);
				}
				return handled;
			}

			virtual void Render(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h) override
			{
				Dialog::Render(renderer, w, h);
				const auto position = GetAbsolutePosition();
				renderer->PrintText(renderer->_style.font.get(), (uint8_t)HexEngine::Style::FontSize::Tiny,
					position.x + 12, position.y + 10, renderer->_style.text_regular, HexEngine::FontAlign::None, _message);
			}

		private:
			void Finish(int32_t action)
			{
				if (_finished)
					return;
				_finished = true;

				std::vector<Row> chosen;
				for (size_t i = 0; i < _rows.size(); ++i)
				{
					if (action == UpdateAll || (action == UpdateSelected && _checked[i]))
						chosen.push_back(_rows[i]);
				}

				auto onClosed = _onClosed;
				const bool remember = _remember && action != UpdateSelected;
				const void* self = this;
				DeleteMe();	// deferred - safe to keep using locals
				if (onClosed)
					onClosed(self, action, chosen, remember);
			}

			std::wstring _message;
			std::vector<Row> _rows;
			std::unique_ptr<bool[]> _checked;
			bool _remember = false;
			bool _finished = false;
			OnClosed _onClosed;
		};
	}

	std::string PrefabController::GetCurrentPrefabRevision(const fs::path& prefabPath) const
	{
		if (prefabPath.empty())
			return std::string();

		// The entity list asks once per row on every refresh; the revision itself
		// is cached by file size + write time, this just skips the stat calls.
		const auto now = std::chrono::steady_clock::now();
		const std::wstring key = prefabPath.wstring();
		if (auto it = _revisionMemo.find(key); it != _revisionMemo.end() && now - it->second.first < std::chrono::milliseconds(500))
			return it->second.second;

		std::string revision = HexEngine::PrefabLoader::ComputePrefabRevision(prefabPath);
		_revisionMemo[key] = { now, revision };
		return revision;
	}

	bool PrefabController::IsSceneEligibleForInstanceSync(const HexEngine::Scene* scene) const
	{
		if (scene == nullptr)
			return false;
		// The prefab stage holds the prefab's own entities, and utility scenes
		// (icon renders) aren't project content.
		if (_prefabStage.active && _prefabStage.stageScene.get() == scene)
			return false;
		return !HEX_HASFLAG(const_cast<HexEngine::Scene*>(scene)->GetFlags(), HexEngine::SceneFlags::Utility);	// GetFlags isn't const
	}

	bool PrefabController::IsPrefabInstanceOutOfDate(HexEngine::Entity* entity) const
	{
		auto* root = ResolvePrefabInstanceRootEntity(entity);
		if (root == nullptr || root->GetPrefabRevision().empty() || !IsSceneEligibleForInstanceSync(root->GetScene()))
			return false;

		const std::string current = GetCurrentPrefabRevision(root->GetPrefabSourcePath());
		return !current.empty() && current != root->GetPrefabRevision();
	}

	std::vector<HexEngine::Entity*> PrefabController::CollectPrefabInstanceRoots(
		const fs::path& prefabPath, bool outOfDateOnly, bool unknownCountsAsOutOfDate) const
	{
		std::vector<HexEngine::Entity*> roots;
		auto* sceneManager = HexEngine::g_pEnv->_sceneManager;
		if (sceneManager == nullptr)
			return roots;

		// Does an instance of `instancePath` depend on `prefabPath`? Itself, or a
		// variant whose base chain reaches it. Memoised per distinct source path.
		std::unordered_map<std::wstring, bool> dependsMemo;
		const auto dependsOnChanged = [&](const fs::path& instancePath)
		{
			if (prefabPath.empty())
				return true;
			const std::wstring key = instancePath.wstring();
			if (auto it = dependsMemo.find(key); it != dependsMemo.end())
				return it->second;

			bool depends = false;
			fs::path current = instancePath;
			for (int depth = 0; depth < 16 && !current.empty() && !depends; ++depth)
			{
				if (ArePrefabPathsEquivalent(current, prefabPath))
				{
					depends = true;
					break;
				}
				VariantAssetData variant;
				if (!LoadVariantAssetData(current, variant))
					break;
				current = variant.basePrefabAbsolutePath;
			}
			dependsMemo[key] = depends;
			return depends;
		};

		for (const auto& scene : sceneManager->GetAllScenes())
		{
			if (!IsSceneEligibleForInstanceSync(scene.get()))
				continue;

			for (const auto& bySignature : scene->GetEntities())
			{
				for (auto* entity : bySignature.second)
				{
					if (entity == nullptr || entity->IsPendingDeletion() || !entity->IsPrefabInstanceRoot())
						continue;
					if (!dependsOnChanged(entity->GetPrefabSourcePath()))
						continue;

					if (outOfDateOnly)
					{
						const std::string current = GetCurrentPrefabRevision(entity->GetPrefabSourcePath());
						if (current.empty())
							continue;
						const std::string& stamped = entity->GetPrefabRevision();
						const bool outOfDate = stamped.empty() ? unknownCountsAsOutOfDate : stamped != current;
						if (!outOfDate)
							continue;
					}

					if (std::find(roots.begin(), roots.end(), entity) == roots.end())
						roots.push_back(entity);
				}
			}
		}

		return roots;
	}

	HexEngine::Entity* PrefabController::ClonePrefabSubtreeInto(
		HexEngine::Scene* scene, HexEngine::Entity* source, HexEngine::Entity* parent,
		const fs::path& prefabPath, const std::string& rootName)
	{
		// The clone is created at root with its local transform in the world
		// slot; attaching WITHOUT preserving world keeps that local, so the new
		// child sits where the prefab puts it under an already-placed parent.
		auto* clone = scene->CloneEntity(source, false);
		if (clone == nullptr)
			return nullptr;

		if (parent != nullptr)
			clone->SetParent(parent, false);
		clone->SetPrefabSource(prefabPath, rootName, false);
		clone->SetPrefabNodeId(source->EnsurePrefabNodeId());

		for (auto* child : source->GetChildren())
		{
			if (child != nullptr)
				ClonePrefabSubtreeInto(scene, child, clone, prefabPath, rootName);
		}
		return clone;
	}

	void PrefabController::SyncEntityFromPrefab(
		HexEngine::Entity* instance, HexEngine::Entity* source, bool isRoot,
		const fs::path& prefabPath, const std::string& rootName)
	{
		auto* scene = instance->GetScene();
		if (scene == nullptr)
			return;

		// A matched child that didn't carry this prefab's link (e.g. added in the
		// scene, then applied into the prefab) now belongs to it. Roots of other
		// prefabs keep their own link.
		if (!isRoot && !instance->IsPrefabInstanceRoot() &&
			(!instance->IsPrefabInstance() || !ArePrefabPathsEquivalent(instance->GetPrefabSourcePath(), prefabPath)))
		{
			const auto overrides = instance->GetPrefabPropertyOverrides();
			const auto patches = instance->GetPrefabOverridePatches();
			instance->SetPrefabSource(prefabPath, rootName, false);
			instance->SetPrefabPropertyOverrides(overrides);
			instance->SetPrefabOverridePatches(patches);
		}
		instance->SetPrefabNodeId(source->EnsurePrefabNodeId());
		if (instance->GetLayer() != source->GetLayer())
			instance->SetLayer(source->GetLayer());

		json sourceComponents = json::array();
		json currentComponents = json::array();
		if (CaptureEntityComponentsSnapshot(source, sourceComponents) && CaptureEntityComponentsSnapshot(instance, currentComponents))
		{
			const json desired = BuildDesiredInstanceComponents(sourceComponents, currentComponents, instance, isRoot);
			MergeComponentsIntoEntity(instance, desired, currentComponents);
		}

		// ---- children ----
		std::vector<HexEngine::Entity*> instanceChildren;
		for (auto* child : instance->GetChildren())
		{
			if (child != nullptr && !child->IsPendingDeletion())
				instanceChildren.push_back(child);
		}
		std::vector<HexEngine::Entity*> sourceChildren;
		for (auto* child : source->GetChildren())
		{
			if (child != nullptr)
				sourceChildren.push_back(child);
		}

		const auto belongsToPrefab = [&](HexEngine::Entity* child)
		{
			return child->IsPrefabInstance() && !child->IsPrefabInstanceRoot() &&
				ArePrefabPathsEquivalent(child->GetPrefabSourcePath(), prefabPath);
		};

		std::vector<HexEngine::Entity*> matches(sourceChildren.size(), nullptr);
		std::unordered_set<HexEngine::Entity*> used;

		// Prefab node id first - stable across renames and the scene's name de-dup.
		for (size_t i = 0; i < sourceChildren.size(); ++i)
		{
			const std::string& nodeId = sourceChildren[i]->EnsurePrefabNodeId();
			for (auto* candidate : instanceChildren)
			{
				if (used.count(candidate) == 0 && candidate->GetPrefabNodeId() == nodeId)
				{
					matches[i] = candidate;
					used.insert(candidate);
					break;
				}
			}
		}

		// Then name, ignoring the numeric suffix the scene appends to keep names
		// unique (older prefab files didn't persist node ids).
		for (size_t i = 0; i < sourceChildren.size(); ++i)
		{
			if (matches[i] != nullptr)
				continue;
			const std::string baseName = StripTrailingDigits(sourceChildren[i]->GetName());
			for (auto* candidate : instanceChildren)
			{
				if (used.count(candidate) == 0 && belongsToPrefab(candidate) &&
					StripTrailingDigits(candidate->GetName()) == baseName)
				{
					matches[i] = candidate;
					used.insert(candidate);
					break;
				}
			}
		}

		for (size_t i = 0; i < sourceChildren.size(); ++i)
		{
			if (matches[i] != nullptr)
				SyncEntityFromPrefab(matches[i], sourceChildren[i], false, prefabPath, rootName);
			else
				ClonePrefabSubtreeInto(scene, sourceChildren[i], instance, prefabPath, rootName);
		}

		// Children that came from the prefab but are gone from it. Scene-added
		// children (no link to this prefab) stay.
		for (auto* child : instanceChildren)
		{
			if (used.count(child) == 0 && belongsToPrefab(child))
				scene->DestroyEntity(child);
		}
	}

	size_t PrefabController::SyncPrefabInstances(const std::vector<HexEngine::Entity*>& instanceRoots)
	{
		auto* sceneManager = HexEngine::g_pEnv->_sceneManager;
		if (sceneManager == nullptr || instanceRoots.empty())
			return 0;

		struct LoadedPrefab
		{
			std::shared_ptr<HexEngine::Scene> scene;
			std::string revision;
			bool loaded = false;
		};
		std::unordered_map<std::wstring, LoadedPrefab> prefabs;

		auto transaction = std::make_unique<PrefabInstanceSyncTransaction>();
		std::unordered_set<HexEngine::Scene*> touchedScenes;
		size_t updated = 0;

		for (auto* root : instanceRoots)
		{
			if (root == nullptr || root->IsPendingDeletion() || !root->IsPrefabInstanceRoot() || root->GetScene() == nullptr)
				continue;

			const fs::path prefabPath = root->GetPrefabSourcePath();
			const std::wstring key = BuildComparablePrefabPath(prefabPath);
			auto it = prefabs.find(key);
			if (it == prefabs.end())
			{
				LoadedPrefab loaded;
				loaded.scene = sceneManager->CreateEmptyScene(false, nullptr, false);
				loaded.loaded = HexEngine::g_pEnv->_prefabLoader->LoadPrefabAssetToScene(prefabPath, loaded.scene);
				loaded.revision = HexEngine::PrefabLoader::ComputePrefabRevision(prefabPath);
				if (!loaded.loaded)
					LOG_WARN("Prefab update: couldn't load '%s'.", prefabPath.string().c_str());
				it = prefabs.emplace(key, std::move(loaded)).first;
			}
			if (!it->second.loaded)
				continue;

			auto* sourceRoot = FindPrefabRootInScene(it->second.scene, root->GetPrefabRootEntityName(), root->GetPrefabNodeId());
			if (sourceRoot == nullptr)
			{
				LOG_WARN("Prefab update: no root '%s' in '%s'.", root->GetPrefabRootEntityName().c_str(), prefabPath.string().c_str());
				continue;
			}

			PrefabInstanceSyncTransaction::Item item;
			const bool haveBefore = CaptureInstanceSnapshot(root, item.before);

			SyncEntityFromPrefab(root, sourceRoot, true, prefabPath, sourceRoot->GetName());
			root->SetPrefabRevision(it->second.revision);

			if (haveBefore && CaptureInstanceSnapshot(root, item.after))
				transaction->Add(std::move(item));

			touchedScenes.insert(root->GetScene());
			++updated;
		}

		for (auto* scene : touchedScenes)
			scene->ForceRebuildPVS();

		if (updated > 0)
		{
			if (_transactions != nullptr && !transaction->Empty())
				_transactions->Push(std::move(transaction));
			LOG_INFO("Updated %zu prefab instance(s) from their prefab.", updated);
		}

		RefreshViewsAfterInstanceChange();
		return updated;
	}

	void PrefabController::RefreshViewsAfterInstanceChange()
	{
		if (_entityList != nullptr)
			_entityList->RefreshList();

		if (_inspector != nullptr)
		{
			if (auto* inspecting = _inspector->GetInspectingEntity(); inspecting != nullptr && !inspecting->IsPendingDeletion())
				_inspector->RequestForcedRefresh(inspecting);
		}
	}

	void PrefabController::OnPrefabAssetChanged(const fs::path& prefabPath, HexEngine::Entity* alreadyInSync)
	{
		if (prefabPath.empty())
			return;

		_revisionMemo.clear();

		if (auto* root = ResolvePrefabInstanceRootEntity(alreadyInSync); root != nullptr)
			root->SetPrefabRevision(HexEngine::PrefabLoader::ComputePrefabRevision(root->GetPrefabSourcePath()));

		// An instance with no stamp predates revision tracking; after a change to
		// its prefab it is assumed out of date.
		const auto roots = CollectPrefabInstanceRoots(prefabPath, true, true);
		// Normalised path: the same change can arrive as a relative (stage save)
		// and an absolute (file watcher) path.
		const std::wstring comparablePath = BuildComparablePrefabPath(prefabPath);
		const std::string skipKey = comparablePath.empty()
			? std::string()
			: fs::path(comparablePath).generic_string() + "|" + GetCurrentPrefabRevision(prefabPath);

		const std::wstring reason = L"'" + prefabPath.filename().wstring() + L"' changed. " +
			std::to_wstring(roots.size()) + L" instance(s) in loaded scenes are out of date:";
		RequestInstanceSync(roots, reason, skipKey);
	}

	void PrefabController::CheckLoadedScenesForOutOfDateInstances()
	{
		// Instances saved before revisions existed: their sync state is unknown,
		// so adopt the current revision (exactly as before - nothing synced them
		// on load either). Changes from here on are tracked.
		size_t stamped = 0;
		for (auto* root : CollectPrefabInstanceRoots(fs::path(), false, false))
		{
			if (!root->GetPrefabRevision().empty())
				continue;
			const std::string revision = GetCurrentPrefabRevision(root->GetPrefabSourcePath());
			if (!revision.empty())
			{
				root->SetPrefabRevision(revision);
				++stamped;
			}
		}
		if (stamped > 0)
			LOG_INFO("Prefab tracking: stamped %zu existing instance(s) with their prefab's current revision.", stamped);

		const auto roots = CollectPrefabInstanceRoots(fs::path(), true, false);
		const std::wstring reason = std::to_wstring(roots.size()) +
			L" prefab instance(s) in the loaded scenes were built from an older version of their prefab:";
		RequestInstanceSync(roots, reason, std::string());
	}

	void PrefabController::RequestInstanceSync(const std::vector<HexEngine::Entity*>& instanceRoots, const std::wstring& reason, const std::string& skipKey)
	{
		if (instanceRoots.empty())
		{
			RefreshViewsAfterInstanceChange();
			return;
		}

		switch (_syncPolicy)
		{
		case InstanceSyncPolicy::Always:
			SyncPrefabInstances(instanceRoots);
			return;

		case InstanceSyncPolicy::Never:
			LOG_INFO("%zu prefab instance(s) are out of date (policy: never update automatically).", instanceRoots.size());
			RefreshViewsAfterInstanceChange();
			return;

		case InstanceSyncPolicy::Ask:
		default:
			break;
		}

		if (!skipKey.empty() && _skippedSyncKeys.count(skipKey) != 0)
		{
			RefreshViewsAfterInstanceChange();
			return;
		}

		// Merge into an open dialog rather than stacking a second one.
		for (auto* root : instanceRoots)
		{
			InstanceRef ref{ root->GetScene()->GetName(), root->GetName() };
			const bool known = std::any_of(_syncDialogRefs.begin(), _syncDialogRefs.end(),
				[&](const InstanceRef& r) { return r.sceneName == ref.sceneName && r.entityName == ref.entityName; });
			if (!known)
				_syncDialogRefs.push_back(std::move(ref));
		}
		if (!skipKey.empty())
			_syncDialogSkipKeys.push_back(skipKey);

		RefreshViewsAfterInstanceChange();
		ShowInstanceSyncDialog(_syncDialog != nullptr
			? std::to_wstring(_syncDialogRefs.size()) + L" prefab instance(s) are out of date:"
			: reason);
	}

	void PrefabController::ShowInstanceSyncDialog(const std::wstring& reason)
	{
		if (_syncDialog != nullptr)
		{
			// Rebuild with the merged list; the old dialog's close must not
			// report a Skip.
			auto* old = _syncDialog;
			_syncDialog = nullptr;
			static_cast<HexEngine::Element*>(old)->DeleteMe();
		}

		std::vector<PrefabInstanceSyncDialog::Row> rows;
		for (const auto& ref : _syncDialogRefs)
		{
			auto* scene = FindLoadedSceneByName(ref.sceneName);
			auto* entity = scene != nullptr ? scene->GetEntityByName(ref.entityName) : nullptr;
			if (entity == nullptr)
				continue;

			PrefabInstanceSyncDialog::Row row;
			row.sceneName = ref.sceneName;
			row.entityName = ref.entityName;
			row.label = std::wstring(ref.entityName.begin(), ref.entityName.end()) + L"   (" +
				entity->GetPrefabSourcePath().filename().wstring() + L", " + ref.sceneName + L")";
			rows.push_back(std::move(row));
		}

		auto* dialog = new PrefabInstanceSyncDialog(reason, std::move(rows),
			[this](const void* closedDialog, int32_t action, const std::vector<PrefabInstanceSyncDialog::Row>& chosen, bool remember)
			{
				if (closedDialog != _syncDialog)
					return;
				std::vector<InstanceRef> refs;
				for (const auto& row : chosen)
					refs.push_back(InstanceRef{ row.sceneName, row.entityName });
				OnInstanceSyncDialogClosed(action, refs, remember);
			});
		dialog->BringToFront();
		_syncDialog = dialog;
	}

	void PrefabController::OnInstanceSyncDialogClosed(int32_t action, const std::vector<InstanceRef>& chosen, bool remember)
	{
		_syncDialog = nullptr;

		const auto skipKeys = std::move(_syncDialogSkipKeys);
		_syncDialogSkipKeys.clear();
		_syncDialogRefs.clear();

		if (action == PrefabInstanceSyncDialog::Skip)
		{
			for (const auto& key : skipKeys)
				_skippedSyncKeys.insert(key);
			if (remember)
				SetInstanceSyncPolicy(InstanceSyncPolicy::Never);
			RefreshViewsAfterInstanceChange();
			return;
		}

		std::vector<HexEngine::Entity*> roots;
		for (const auto& ref : chosen)
		{
			auto* scene = FindLoadedSceneByName(ref.sceneName);
			if (auto* entity = scene != nullptr ? scene->GetEntityByName(ref.entityName) : nullptr; entity != nullptr && !entity->IsPendingDeletion())
				roots.push_back(entity);
		}

		if (remember && action == PrefabInstanceSyncDialog::UpdateAll)
			SetInstanceSyncPolicy(InstanceSyncPolicy::Always);

		SyncPrefabInstances(roots);
	}

	bool PrefabController::UpdatePrefabInstanceFromAsset(HexEngine::Entity* entity)
	{
		auto* root = ResolvePrefabInstanceRootEntity(entity);
		if (root == nullptr)
			return false;
		return SyncPrefabInstances({ root }) > 0;
	}

	size_t PrefabController::UpdateAllOutOfDatePrefabInstances()
	{
		_revisionMemo.clear();
		return SyncPrefabInstances(CollectPrefabInstanceRoots(fs::path(), true, false));
	}

	void PrefabController::SetInstanceSyncPolicy(InstanceSyncPolicy policy)
	{
		_syncPolicy = policy;
		SaveEditorPrefs();
		LOG_INFO("Prefab instance updates: %s", policy == InstanceSyncPolicy::Always ? "always" : (policy == InstanceSyncPolicy::Never ? "never" : "ask"));
	}

	void PrefabController::LoadEditorPrefs()
	{
		const json prefs = ReadEditorPrefsJson();
		const std::string policy = prefs.value(kSyncPolicyPrefKey, std::string("ask"));
		_syncPolicy = policy == "always" ? InstanceSyncPolicy::Always : (policy == "never" ? InstanceSyncPolicy::Never : InstanceSyncPolicy::Ask);
	}

	void PrefabController::SaveEditorPrefs() const
	{
		const fs::path path = GetEditorPrefsPath();
		if (path.empty())
			return;

		json prefs = ReadEditorPrefsJson();
		prefs[kSyncPolicyPrefKey] = _syncPolicy == InstanceSyncPolicy::Always ? "always" : (_syncPolicy == InstanceSyncPolicy::Never ? "never" : "ask");

		std::error_code ec;
		fs::create_directories(path.parent_path(), ec);
		if (!WriteJsonAssetFile(path, prefs))
			LOG_WARN("Couldn't write editor preferences to '%s'.", path.string().c_str());
	}

	void PrefabController::HandleComponentPropertyEdit(HexEngine::Entity* entity, const json& beforeComponents, const json& afterComponents)
	{
		if (entity == nullptr)
			return;

		const bool isPrefabInstance = entity->IsPrefabInstance();
		const bool isVariantStageEntity = IsVariantStageEntity(entity);
		if (!isPrefabInstance && !isVariantStageEntity)
			return;

		if (isPrefabInstance)
		{
			const auto genericPatches = BuildGenericPrefabOverridePatches(beforeComponents, afterComponents);
			bool touchesTransformSpatial = false;
			for (const auto& patch : genericPatches)
			{
				entity->UpsertPrefabOverridePatch(patch);

				if (!touchesTransformSpatial &&
					patch.componentName == "Transform" &&
					(patch.path == "/_position" || patch.path == "/_rotation" || patch.path == "/_scale"))
				{
					touchesTransformSpatial = true;
				}
			}
			if (!genericPatches.empty())
			{
				// Keep editor visibility caches coherent after transform overrides.
				// Without this, stale renderable entries can persist until camera movement forces a rebuild.
				if (touchesTransformSpatial)
				{
					if (auto* scene = entity->GetScene(); scene != nullptr)
					{
						scene->ForceRebuildPVS();
					}
				}

				RefreshInspectorForPrefabInstance(entity);
			}
		}

		if (isVariantStageEntity &&
			beforeComponents != afterComponents &&
			_inspector != nullptr &&
			_inspector->GetInspectingEntity() == entity)
		{
			_inspector->InspectEntity(entity);
		}
	}

	void PrefabController::HandleTransformPositionEdit(HexEngine::Entity* entity, const math::Vector3& before, const math::Vector3& after)
	{
		if (entity == nullptr || before == after)
			return;

		const bool isPrefabInstance = entity->IsPrefabInstance();
		const bool isVariantStageEntity = IsVariantStageEntity(entity);
		if (!isPrefabInstance && !isVariantStageEntity)
			return;

		json afterComponents = json::array();
		if (!CaptureEntityComponentsSnapshot(entity, afterComponents))
			return;

		json beforeComponents = afterComponents;
		auto* beforeTransform = FindMutableComponentEntryByName(beforeComponents, "Transform");
		auto* afterTransform = FindMutableComponentEntryByName(afterComponents, "Transform");
		if (beforeTransform == nullptr || afterTransform == nullptr)
			return;

		(*beforeTransform)["_position"] = SerializeComponentFieldValue("_position", before);
		(*afterTransform)["_position"] = SerializeComponentFieldValue("_position", after);
		HandleComponentPropertyEdit(entity, beforeComponents, afterComponents);
	}

	void PrefabController::HandleTransformRotationEdit(HexEngine::Entity* entity, const math::Quaternion& before, const math::Quaternion& after)
	{
		if (entity == nullptr || before == after)
			return;

		const bool isPrefabInstance = entity->IsPrefabInstance();
		const bool isVariantStageEntity = IsVariantStageEntity(entity);
		if (!isPrefabInstance && !isVariantStageEntity)
			return;

		json afterComponents = json::array();
		if (!CaptureEntityComponentsSnapshot(entity, afterComponents))
			return;

		json beforeComponents = afterComponents;
		auto* beforeTransform = FindMutableComponentEntryByName(beforeComponents, "Transform");
		auto* afterTransform = FindMutableComponentEntryByName(afterComponents, "Transform");
		if (beforeTransform == nullptr || afterTransform == nullptr)
			return;

		(*beforeTransform)["_rotation"] = SerializeComponentFieldValue("_rotation", before);
		(*afterTransform)["_rotation"] = SerializeComponentFieldValue("_rotation", after);
		HandleComponentPropertyEdit(entity, beforeComponents, afterComponents);
	}

	void PrefabController::HandleTransformScaleEdit(HexEngine::Entity* entity, const math::Vector3& before, const math::Vector3& after)
	{
		if (entity == nullptr || before == after)
			return;

		const bool isPrefabInstance = entity->IsPrefabInstance();
		const bool isVariantStageEntity = IsVariantStageEntity(entity);
		if (!isPrefabInstance && !isVariantStageEntity)
			return;

		json afterComponents = json::array();
		if (!CaptureEntityComponentsSnapshot(entity, afterComponents))
			return;

		json beforeComponents = afterComponents;
		auto* beforeTransform = FindMutableComponentEntryByName(beforeComponents, "Transform");
		auto* afterTransform = FindMutableComponentEntryByName(afterComponents, "Transform");
		if (beforeTransform == nullptr || afterTransform == nullptr)
			return;

		(*beforeTransform)["_scale"] = SerializeComponentFieldValue("_scale", before);
		(*afterTransform)["_scale"] = SerializeComponentFieldValue("_scale", after);
		HandleComponentPropertyEdit(entity, beforeComponents, afterComponents);
	}

	void PrefabController::HandleStaticMeshMaterialEdit(HexEngine::Entity* entity, const fs::path& before, const fs::path& after)
	{
		if (entity == nullptr || before == after)
			return;

		const bool isPrefabInstance = entity->IsPrefabInstance();
		const bool isVariantStageEntity = IsVariantStageEntity(entity);
		if (!isPrefabInstance && !isVariantStageEntity)
			return;

		json afterComponents = json::array();
		if (!CaptureEntityComponentsSnapshot(entity, afterComponents))
			return;

		json beforeComponents = afterComponents;
		auto* beforeStaticMesh = FindMutableComponentEntryByName(beforeComponents, "StaticMeshComponent");
		auto* afterStaticMesh = FindMutableComponentEntryByName(afterComponents, "StaticMeshComponent");
		if (beforeStaticMesh == nullptr || afterStaticMesh == nullptr)
			return;

		if (!SetStaticMeshMaterialPathInSnapshot(*beforeStaticMesh, before))
			return;

		if (!SetStaticMeshMaterialPathInSnapshot(*afterStaticMesh, after))
			return;

		HandleComponentPropertyEdit(entity, beforeComponents, afterComponents);
	}

	bool PrefabController::IsPrefabInstanceEntity(HexEngine::Entity* entity) const
	{
		return entity != nullptr && entity->IsPrefabInstance();
	}

	bool PrefabController::IsPrefabInstanceRootEntity(HexEngine::Entity* entity) const
	{
		return entity != nullptr && entity->IsPrefabInstanceRoot();
	}

	bool PrefabController::HasPrefabInstanceOverrides(HexEngine::Entity* entity) const
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
		if (!HexEngine::g_pEnv->_prefabLoader->LoadPrefabAssetToScene(prefabPath, prefabScene))
		{
			LOG_WARN("Failed to load prefab '%s' while checking instance overrides.", prefabPath.string().c_str());
			return false;
		}

		auto* sourceRoot = FindPrefabRootInScene(
			prefabScene,
			root->GetPrefabRootEntityName(),
			root->GetPrefabNodeId());
		if (sourceRoot == nullptr)
			return false;

		HexEngine::JsonFile serializer(fs::path("temp_prefab_compare.json"), std::ios::out);
		const json currentSnapshot = BuildPrefabEntitySnapshotRecursive(root, serializer, true);
		const json sourceSnapshot = BuildPrefabEntitySnapshotRecursive(sourceRoot, serializer, true);
		return currentSnapshot != sourceSnapshot;
	}

	bool PrefabController::GetPrefabInstancePropertyOverrides(HexEngine::Entity* entity, std::vector<PrefabPropertyOverride>& outOverrides) const
	{
		outOverrides.clear();
		if (entity == nullptr || !entity->IsPrefabInstance())
			return false;

		std::shared_ptr<HexEngine::Scene> prefabScene;
		HexEngine::Entity* sourceEntity = nullptr;
		HexEngine::Entity* instanceRoot = nullptr;
		json baseComponents = json::array();
		json editedComponents = json::array();
		if (!ResolvePrefabSourceEntityAndSnapshots(
			entity,
			prefabScene,
			sourceEntity,
			instanceRoot,
			baseComponents,
			editedComponents))
		{
			return false;
		}

		auto patches = BuildGenericPrefabOverridePatches(baseComponents, editedComponents);
		std::sort(patches.begin(), patches.end(),
			[](const HexEngine::Entity::PrefabOverridePatch& a, const HexEngine::Entity::PrefabOverridePatch& b)
			{
				if (a.componentName != b.componentName)
					return a.componentName < b.componentName;
				if (a.path != b.path)
					return a.path < b.path;
				return a.op < b.op;
			});

		for (const auto& patch : patches)
		{
			if (patch.componentName.empty() || patch.path.empty() || patch.op.empty())
				continue;

			if (patch.componentName == kPrefabOverrideComponentArrayPatchTarget)
				continue;

			PrefabPropertyOverride overrideEntry;
			overrideEntry.componentName = patch.componentName;
			overrideEntry.path = patch.path;
			overrideEntry.op = patch.op;
			outOverrides.push_back(std::move(overrideEntry));
		}

		return !outOverrides.empty();
	}

	bool PrefabController::RevertPrefabInstancePropertyOverride(HexEngine::Entity* entity, const std::string& componentName, const std::string& propertyPath)
	{
		if (entity == nullptr || !entity->IsPrefabInstance() || componentName.empty() || propertyPath.empty())
			return false;

		std::shared_ptr<HexEngine::Scene> prefabScene;
		HexEngine::Entity* sourceEntity = nullptr;
		HexEngine::Entity* instanceRoot = nullptr;
		json baseComponents = json::array();
		json editedComponents = json::array();
		if (!ResolvePrefabSourceEntityAndSnapshots(
			entity,
			prefabScene,
			sourceEntity,
			instanceRoot,
			baseComponents,
			editedComponents))
		{
			return false;
		}

		const auto allPatches = BuildGenericPrefabOverridePatches(baseComponents, editedComponents);
		std::unordered_set<std::string> selectedKeys;
		for (const auto& patch : allPatches)
		{
			if (patch.componentName != componentName)
				continue;

			if (!IsPatchPathMatchingSelection(patch.path, propertyPath))
				continue;

			selectedKeys.insert(BuildPrefabOverrideSelectionKey(patch.componentName, patch.path));
		}

		if (selectedKeys.empty())
			return false;

		std::vector<HexEngine::Entity::PrefabOverridePatch> remainingPatches;
		FilterOverridePatchesBySelection(allPatches, selectedKeys, false, remainingPatches);

		json desiredComponents = baseComponents;
		if (!ApplyOverridePatchesToComponentSnapshot(desiredComponents, remainingPatches))
			return false;

		const auto patchesToApply = BuildGenericPrefabOverridePatches(editedComponents, desiredComponents);
		if (!patchesToApply.empty() && !ApplyGenericPrefabOverridePatchesToEntity(entity, patchesToApply))
			return false;

		entity->SetPrefabOverridePatches(remainingPatches);
		entity->ClearPrefabPropertyOverrides();

		if (auto* scene = entity->GetScene(); scene != nullptr)
		{
			scene->ForceRebuildPVS();
		}
		RefreshInspectorForPrefabInstance(entity);
		return true;
	}

	bool PrefabController::RevertPrefabInstanceComponentOverrides(HexEngine::Entity* entity, const std::string& componentName)
	{
		if (entity == nullptr || !entity->IsPrefabInstance() || componentName.empty())
			return false;

		std::shared_ptr<HexEngine::Scene> prefabScene;
		HexEngine::Entity* sourceEntity = nullptr;
		HexEngine::Entity* instanceRoot = nullptr;
		json baseComponents = json::array();
		json editedComponents = json::array();
		if (!ResolvePrefabSourceEntityAndSnapshots(
			entity,
			prefabScene,
			sourceEntity,
			instanceRoot,
			baseComponents,
			editedComponents))
		{
			return false;
		}

		const auto allPatches = BuildGenericPrefabOverridePatches(baseComponents, editedComponents);
		std::unordered_set<std::string> selectedKeys;
		for (const auto& patch : allPatches)
		{
			if (patch.componentName == componentName)
			{
				selectedKeys.insert(BuildPrefabOverrideSelectionKey(patch.componentName, patch.path));
			}
		}

		if (selectedKeys.empty())
			return false;

		std::vector<HexEngine::Entity::PrefabOverridePatch> remainingPatches;
		FilterOverridePatchesBySelection(allPatches, selectedKeys, false, remainingPatches);

		json desiredComponents = baseComponents;
		if (!ApplyOverridePatchesToComponentSnapshot(desiredComponents, remainingPatches))
			return false;

		const auto patchesToApply = BuildGenericPrefabOverridePatches(editedComponents, desiredComponents);
		if (!patchesToApply.empty() && !ApplyGenericPrefabOverridePatchesToEntity(entity, patchesToApply))
			return false;

		entity->SetPrefabOverridePatches(remainingPatches);
		entity->ClearPrefabPropertyOverrides();

		if (auto* scene = entity->GetScene(); scene != nullptr)
		{
			scene->ForceRebuildPVS();
		}
		RefreshInspectorForPrefabInstance(entity);
		return true;
	}

	bool PrefabController::ApplySelectedPrefabInstanceOverridesToAsset(HexEngine::Entity* entity, const std::vector<PrefabPropertyOverride>& selectedOverrides)
	{
		if (entity == nullptr || !entity->IsPrefabInstance() || selectedOverrides.empty())
			return false;

		std::shared_ptr<HexEngine::Scene> prefabScene;
		HexEngine::Entity* sourceEntity = nullptr;
		HexEngine::Entity* instanceRoot = nullptr;
		json baseComponents = json::array();
		json editedComponents = json::array();
		fs::path prefabPath;
		if (!ResolvePrefabSourceEntityAndSnapshots(
			entity,
			prefabScene,
			sourceEntity,
			instanceRoot,
			baseComponents,
			editedComponents,
			&prefabPath))
		{
			return false;
		}

		std::unordered_set<std::string> selectedKeys;
		for (const auto& selected : selectedOverrides)
		{
			if (selected.componentName.empty() || selected.path.empty())
				continue;

			selectedKeys.insert(BuildPrefabOverrideSelectionKey(selected.componentName, selected.path));
		}

		if (selectedKeys.empty())
			return false;

		const auto allPatches = BuildGenericPrefabOverridePatches(baseComponents, editedComponents);
		std::vector<HexEngine::Entity::PrefabOverridePatch> selectedPatches;
		std::vector<HexEngine::Entity::PrefabOverridePatch> remainingPatches;
		FilterOverridePatchesBySelection(allPatches, selectedKeys, true, selectedPatches);
		FilterOverridePatchesBySelection(allPatches, selectedKeys, false, remainingPatches);
		if (selectedPatches.empty())
			return false;

		json sourceBeforeComponents = json::array();
		if (!CaptureEntityComponentsSnapshot(sourceEntity, sourceBeforeComponents))
			return false;

		json sourceDesiredComponents = sourceBeforeComponents;
		if (!ApplyOverridePatchesToComponentSnapshot(sourceDesiredComponents, selectedPatches))
			return false;

		const auto sourcePatchesToApply = BuildGenericPrefabOverridePatches(sourceBeforeComponents, sourceDesiredComponents);
		if (!sourcePatchesToApply.empty() && !ApplyGenericPrefabOverridePatchesToEntity(sourceEntity, sourcePatchesToApply))
			return false;

		auto* sceneManager = HexEngine::g_pEnv->_sceneManager;
		if (sceneManager == nullptr)
			return false;

		if (IsVariantPrefabAsset(prefabPath))
		{
			size_t patchCount = 0;
			if (!SaveVariantAssetFromEditedScene(prefabPath, prefabScene.get(), sceneManager, &patchCount))
				return false;
		}
		else
		{
			std::vector<HexEngine::Entity*> entitiesToSave;
			for (const auto& bySignature : prefabScene->GetEntities())
			{
				for (auto* prefabEntity : bySignature.second)
				{
					if (prefabEntity != nullptr && !prefabEntity->HasFlag(HexEngine::EntityFlags::DoNotSave))
					{
						entitiesToSave.push_back(prefabEntity);
					}
				}
			}

			HexEngine::SceneSaveFile saveFile(prefabPath, std::ios::out | std::ios::trunc, prefabScene, HexEngine::SceneFileFlags::IsPrefab);
			if (!saveFile.Save(entitiesToSave))
				return false;
		}

		entity->SetPrefabOverridePatches(remainingPatches);
		entity->ClearPrefabPropertyOverrides();

		RefreshPrefabAssetPreview(prefabPath);
		RefreshInspectorForPrefabInstance(entity);
		OnPrefabAssetChanged(prefabPath, instanceRoot);
		return true;
	}

	HexEngine::Entity* PrefabController::RevertPrefabInstance(HexEngine::Entity* entity)
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
		if (!HexEngine::g_pEnv->_prefabLoader->LoadPrefabAssetToScene(prefabPath, prefabScene))
		{
			LOG_WARN("Failed to load prefab '%s' while reverting instance '%s'.", prefabPath.string().c_str(), entity->GetName().c_str());
			return nullptr;
		}

		const std::string prefabRootName = entity->GetPrefabRootEntityName();
		auto* sourceRoot = FindPrefabRootInScene(
			prefabScene,
			prefabRootName,
			entity->GetPrefabNodeId());
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

	bool PrefabController::ApplyPrefabInstanceToPrefabAsset(HexEngine::Entity* entity)
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

		if (IsVariantPrefabAsset(prefabPath))
		{
			size_t patchCount = 0;
			if (!SaveVariantAssetFromEditedScene(prefabPath, tempScene.get(), sceneManager, &patchCount))
			{
				LOG_WARN("Failed to save prefab variant '%s' from instance '%s'.",
					prefabPath.string().c_str(),
					entity->GetName().c_str());
				return false;
			}

			RefreshPrefabAssetPreview(prefabPath);
			RefreshInspectorForPrefabInstance(entity);
			OnPrefabAssetChanged(prefabPath, entity);

			LOG_INFO("Applied prefab variant instance '%s' to asset '%s' (%zu patches).",
				entity->GetName().c_str(),
				prefabPath.string().c_str(),
				patchCount);
			return true;
		}

		HexEngine::SceneSaveFile saveFile(prefabPath, std::ios::out | std::ios::trunc, tempScene, HexEngine::SceneFileFlags::IsPrefab);
		if (!saveFile.Save(entitiesToSave))
		{
			LOG_WARN("Failed to save prefab '%s' from instance '%s'.", prefabPath.string().c_str(), entity->GetName().c_str());
			return false;
		}

		RefreshPrefabAssetPreview(prefabPath);
		RefreshInspectorForPrefabInstance(entity);
		OnPrefabAssetChanged(prefabPath, entity);

		LOG_INFO("Applied prefab instance '%s' to asset '%s'.", entity->GetName().c_str(), prefabPath.string().c_str());
		return true;
	}

	bool PrefabController::IsVariantStageEntity(HexEngine::Entity* entity) const
	{
		if (entity == nullptr || !_prefabStage.active || !_prefabStage.isVariantAsset || _prefabStage.stageScene == nullptr)
			return false;

		return entity->GetScene() == _prefabStage.stageScene.get();
	}

	bool PrefabController::GetVariantStageEntityOverrideComponents(HexEngine::Entity* entity, std::unordered_set<std::string>& outComponentNames) const
	{
		outComponentNames.clear();
		if (!IsVariantStageEntity(entity))
			return false;

		auto* sceneManager = HexEngine::g_pEnv->_sceneManager;
		if (sceneManager == nullptr)
			return false;

		VariantAssetData variantData;
		if (!LoadVariantAssetData(_prefabStage.prefabPath, variantData))
			return false;

		auto baseScene = sceneManager->CreateEmptyScene(false, nullptr, false);
		if (!HexEngine::g_pEnv->_prefabLoader->LoadPrefabAssetToScene(variantData.basePrefabAbsolutePath, baseScene))
			return false;

		auto* baseEntity = FindEntityByPrefabNodeIdInScene(baseScene, entity->GetPrefabNodeId());
		if (baseEntity == nullptr)
			return false;

		json baseComponents = json::array();
		json editedComponents = json::array();
		if (!CaptureEntityComponentsSnapshot(baseEntity, baseComponents) ||
			!CaptureEntityComponentsSnapshot(entity, editedComponents))
		{
			return false;
		}

		CollectOverriddenComponentNamesFromSnapshots(baseComponents, editedComponents, outComponentNames);
		return true;
	}

	bool PrefabController::RevertVariantStageComponentToBase(HexEngine::Entity* entity, const std::string& componentName)
	{
		if (!IsVariantStageEntity(entity) || componentName.empty())
			return false;

		auto* sceneManager = HexEngine::g_pEnv->_sceneManager;
		if (sceneManager == nullptr)
			return false;

		VariantAssetData variantData;
		if (!LoadVariantAssetData(_prefabStage.prefabPath, variantData))
			return false;

		auto baseScene = sceneManager->CreateEmptyScene(false, nullptr, false);
		if (!HexEngine::g_pEnv->_prefabLoader->LoadPrefabAssetToScene(variantData.basePrefabAbsolutePath, baseScene))
			return false;

		auto* baseEntity = FindEntityByPrefabNodeIdInScene(baseScene, entity->GetPrefabNodeId());
		if (baseEntity == nullptr)
			return false;

		json baseComponents = json::array();
		json editedComponents = json::array();
		if (!CaptureEntityComponentsSnapshot(baseEntity, baseComponents) ||
			!CaptureEntityComponentsSnapshot(entity, editedComponents))
		{
			return false;
		}

		json desiredComponents = editedComponents;
		const json* baseComponent = FindComponentEntryByName(baseComponents, componentName);
		json* desiredComponent = FindMutableComponentEntryByName(desiredComponents, componentName);

		if (baseComponent == nullptr)
		{
			if (componentName == "Transform")
				return false;

			if (desiredComponents.is_array())
			{
				desiredComponents.erase(
					std::remove_if(desiredComponents.begin(), desiredComponents.end(),
						[&](const json& item)
						{
							return item.is_object() && item.value("name", std::string()) == componentName;
						}),
					desiredComponents.end());
			}
		}
		else
		{
			if (desiredComponent != nullptr)
			{
				*desiredComponent = *baseComponent;
			}
			else if (desiredComponents.is_array())
			{
				desiredComponents.push_back(*baseComponent);
			}
		}

		const auto patches = BuildGenericPrefabOverridePatches(editedComponents, desiredComponents);
		if (patches.empty())
			return true;

		const bool applied = ApplyGenericPrefabOverridePatchesToEntity(entity, patches);
		if (applied)
		{
			entity->GetScene()->ForceRebuildPVS();
		}
		return applied;
	}

	bool PrefabController::OpenPrefabStage(const fs::path& prefabPath)
	{
		if (prefabPath.empty() || prefabPath.extension() != ".hprefab")
			return false;

		if (_integrator != nullptr && _integrator->GetState() == GameTestState::Started)
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
			_prefabStage.stageScene = sceneManager->CreateEmptyScene(false, _stageEntityListener, true);
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
		_prefabStage.isVariantAsset = IsVariantPrefabAsset(prefabPath);
		_prefabStage.previousActiveScene = activeScene;
		_prefabStage.previousSceneFlags.clear();

		if (!HexEngine::g_pEnv->_prefabLoader->LoadPrefabAssetToScene(prefabPath, _prefabStage.stageScene))
		{
			LOG_WARN("Failed to open prefab stage for '%s'", prefabPath.string().c_str());
			_prefabStage.prefabPath.clear();
			_prefabStage.isVariantAsset = false;
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

		if (_inspector != nullptr)
		{
			_inspector->InspectEntity(nullptr);
		}

		if (_entityList != nullptr)
		{
			_entityList->RefreshList();
		}

		LOG_INFO("Opened prefab stage: %s", prefabPath.string().c_str());
		return true;
	}

	bool PrefabController::SavePrefabStage()
	{
		if (!_prefabStage.active || _prefabStage.stageScene == nullptr || _prefabStage.prefabPath.empty())
			return false;

		if (IsVariantPrefabAsset(_prefabStage.prefabPath))
		{
			auto* sceneManager = HexEngine::g_pEnv->_sceneManager;
			if (sceneManager == nullptr)
				return false;

			size_t patchCount = 0;
			if (!SaveVariantAssetFromEditedScene(_prefabStage.prefabPath, _prefabStage.stageScene.get(), sceneManager, &patchCount))
			{
				LOG_WARN("Failed to save variant prefab '%s'.", _prefabStage.prefabPath.string().c_str());
				return false;
			}

			RefreshPrefabInstancesFromAsset(_prefabStage.prefabPath);
			LOG_INFO("Saved prefab variant: %s (%zu patches)", _prefabStage.prefabPath.string().c_str(), patchCount);
			return true;
		}

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

		RefreshPrefabInstancesFromAsset(_prefabStage.prefabPath);

		LOG_INFO("Saved prefab: %s", _prefabStage.prefabPath.string().c_str());
		return true;
	}

	bool PrefabController::RefreshPrefabInstancesFromAsset(const fs::path& prefabPath)
	{
		if (prefabPath.empty() || prefabPath.extension() != ".hprefab")
			return false;

		RefreshPrefabAssetPreview(prefabPath);
		OnPrefabAssetChanged(prefabPath);
		return true;
	}

	bool PrefabController::ClosePrefabStage(bool saveChanges)
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
		_prefabStage.isVariantAsset = false;
		_prefabStage.prefabPath.clear();
		_prefabStage.previousActiveScene.reset();
		_prefabStage.previousSceneFlags.clear();

		if (_inspector != nullptr)
		{
			_inspector->InspectEntity(nullptr);
		}

		if (_entityList != nullptr)
		{
			_entityList->RefreshList();
		}

		LOG_INFO("Exited prefab stage");
		return true;
	}

	bool PrefabController::IsPrefabStageActive() const
	{
		return _prefabStage.active;
	}
}

