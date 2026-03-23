#include "PrefabController.hpp"

#include "../GameIntegrator.hpp"
#include "Actions/Inspector.hpp"
#include "Actions/Explorer.hpp"
#include "Elements/EntityList.hpp"

#include <HexEngine.Core\FileSystem\DiskFile.hpp>
#include <HexEngine.Core\FileSystem\SceneSaveFile.hpp>
#include <HexEngine.Core\Scene\SceneFramingUtils.hpp>
#include <algorithm>
#include <exception>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <cwctype>

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
			if (!sceneManager->LoadPrefabAssetToScene(prefabPath, prefabScene))
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
			if (!sceneManager->LoadPrefabAssetToScene(variantData.basePrefabAbsolutePath, baseScene))
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

		if (!HexEngine::SceneFramingUtils::FrameCameraToSceneBounds(scene.get(), camera, true))
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

	bool PrefabController::PropagateAppliedPrefabToInstances(
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
		if (!sceneManager->LoadPrefabAssetToScene(prefabPath, prefabScene))
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

					if (!entity->IsPrefabInstanceRoot() || !ArePrefabPathsEquivalent(entity->GetPrefabSourcePath(), prefabPath))
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

			auto* sourceRoot = FindPrefabRootInScene(
				prefabScene,
				instanceRoot->GetPrefabRootEntityName(),
				instanceRoot->GetPrefabNodeId());
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

			PrefabOverrideStateSet overrideStates;
			CollectPrefabOverrideStateRecursive(instanceRoot, "__root__", overrideStates);

			const bool wasInspected = (_inspector != nullptr && _inspector->GetInspectingEntity() == instanceRoot);
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

			if (wasInspected && _inspector != nullptr)
			{
				_inspector->InspectEntity(newRoot);
			}
		}

		if (replacedAny && _entityList != nullptr)
		{
			_entityList->RefreshList();
		}

		return replacedAny;
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
			for (const auto& patch : genericPatches)
			{
				entity->UpsertPrefabOverridePatch(patch);
			}
			if (!genericPatches.empty())
			{
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
		if (!sceneManager->LoadPrefabAssetToScene(prefabPath, prefabScene))
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
		PropagateAppliedPrefabToInstances(prefabPath, instanceRoot, nullptr);
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
		if (!sceneManager->LoadPrefabAssetToScene(prefabPath, prefabScene))
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
			PropagateAppliedPrefabToInstances(prefabPath, entity, nullptr);

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
		PropagateAppliedPrefabToInstances(prefabPath, entity, nullptr);

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
		if (!sceneManager->LoadPrefabAssetToScene(variantData.basePrefabAbsolutePath, baseScene))
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
		if (!sceneManager->LoadPrefabAssetToScene(variantData.basePrefabAbsolutePath, baseScene))
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

		if (!sceneManager->LoadPrefabAssetToScene(prefabPath, _prefabStage.stageScene))
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
		return PropagateAppliedPrefabToInstances(prefabPath, nullptr, nullptr);
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
