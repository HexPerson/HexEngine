
#include "EntityList.hpp"
#include "Button.hpp"
#include "Checkbox.hpp"
#include "Dialog.hpp"
#include "GroupBox.hpp"
#include "LineEdit.hpp"
#include "ListBox.hpp"
#include "../UIManager.hpp"
#include "../../Environment/LogFile.hpp"
#include "../../FileSystem/FileSystem.hpp"
#include "../../Entity\Entity.hpp"
#include "../../Entity/Component/StaticMeshComponent.hpp"
#include "../../Entity/Component/Transform.hpp"
#include "../../Environment\IEnvironment.hpp"
#include "../../Graphics/Material.hpp"
#include "../../Graphics/MeshLoader.hpp"
#include "../../Scene/Mesh.hpp"
#include "../../Scene\SceneManager.hpp"
#include <algorithm>
#include <cwctype>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace HexEngine
{
	namespace
	{
		struct EntityListState
		{
			std::unordered_map<const Entity*, bool> entityOpenState;
			std::unordered_map<const Scene*, bool> sceneOpenState;
			std::wstring filterText;
			std::vector<std::wstring> filterTokens;
			Entity* selectedEntity = nullptr;
			// Order-preserving list of multi-selected entities. INVARIANT: when non-empty,
			// the *first* entry is the "anchor" or "survivor" - the entity any merge action
			// keeps and merges the rest into. New Ctrl-click selections are appended at the
			// end so the user's first picked entity remains the anchor through the session.
			std::vector<Entity*> selectedEntities;
		};

		std::unordered_map<const EntityList*, EntityListState> g_entityListStates;

		EntityListState& GetState(const EntityList* list)
		{
			return g_entityListStates[list];
		}

		const EntityListState& GetStateConst(const EntityList* list)
		{
			auto it = g_entityListStates.find(list);
			if (it != g_entityListStates.end())
				return it->second;

			static const EntityListState emptyState{};
			return emptyState;
		}

		void TryAutoLinkDuplicatedTrafficLane(Entity* source, Entity* duplicate)
		{
			(void)source;
			(void)duplicate;

			// Core is intentionally plugin-agnostic. Traffic lane relinking is handled by
			// optional city-simulation tooling when the plugin is available.
		}

		bool CloneEntityChildrenRecursive(Scene* scene, Entity* sourceParent, Entity* clonedParent)
		{
			if (scene == nullptr || sourceParent == nullptr || clonedParent == nullptr)
				return false;

			const auto sourceChildren = sourceParent->GetChildren();
			for (auto* sourceChild : sourceChildren)
			{
				if (sourceChild == nullptr || sourceChild->IsPendingDeletion())
					continue;

				auto* clonedChild = scene->CloneEntity(sourceChild, false);
				if (clonedChild == nullptr)
					return false;

				clonedChild->SetParent(clonedParent);
				if (!CloneEntityChildrenRecursive(scene, sourceChild, clonedChild))
					return false;
			}

			return true;
		}

		Entity* CloneEntityHierarchy(Scene* scene, Entity* sourceRoot)
		{
			if (scene == nullptr || sourceRoot == nullptr || sourceRoot->IsPendingDeletion())
				return nullptr;

			auto* clonedRoot = scene->CloneEntity(sourceRoot);
			if (clonedRoot == nullptr)
				return nullptr;

			if (!CloneEntityChildrenRecursive(scene, sourceRoot, clonedRoot))
			{
				scene->DestroyEntity(clonedRoot);
				return nullptr;
			}

			return clonedRoot;
		}

		// --- Static mesh merge ---------------------------------------------------------------
		//
		// The merge action collapses a `root` entity and every descendant that has a
		// StaticMeshComponent into a single combined static mesh assigned to `root`.
		// Each descendant's vertices are baked into root-local space (so root's own
		// transform survives intact), the combined mesh is written to disk as a fresh
		// .hmesh under EngineData/Meshes/Merged, and every descendant whose mesh got
		// folded in is destroyed.
		//
		// Counts every contributor: root included if it has its own StaticMeshComponent.
		// "Extras" = components other than Transform and StaticMeshComponent. If any
		// descendant has extras we present them to the user before doing anything
		// destructive; the user can force-merge (extras vanish with the entity) or cancel.

		// A single source of vertex data that will be folded into the combined mesh.
		struct MergeSource
		{
			Entity* entity = nullptr;          // entity owning the contributing StaticMeshComponent
			std::shared_ptr<Mesh> mesh;        // shared with the entity's component; vertex data is read, not modified
			math::Matrix entityToRootLocal;    // world TM of entity * inverse(world TM of root); applied to verts
		};

		// Collects every entity under `root` (including `root` itself when it has its own SMC)
		// that contributes a static mesh to the merge. Walk skips pending-deletion entities.
		void CollectStaticMeshSources(Entity* root, std::vector<Entity*>& outEntities)
		{
			if (root == nullptr || root->IsPendingDeletion())
				return;

			if (root->GetComponent<StaticMeshComponent>() != nullptr)
				outEntities.push_back(root);

			for (auto* child : root->GetChildren())
			{
				CollectStaticMeshSources(child, outEntities);
			}
		}

		// Collects every descendant of `root` (not including root itself). Used by the
		// merge to know which entities to destroy afterwards - including non-SMC nodes,
		// otherwise they get orphaned-but-still-flagged when their SMC-bearing parent is
		// removed and end up leaking entity slots / PVS state.
		void CollectAllDescendants(Entity* root, std::vector<Entity*>& outEntities)
		{
			if (root == nullptr || root->IsPendingDeletion())
				return;

			for (auto* child : root->GetChildren())
			{
				if (child == nullptr || child->IsPendingDeletion())
					continue;
				outEntities.push_back(child);
				CollectAllDescendants(child, outEntities);
			}
		}

		// Returns a list of component names on `entity` that are neither Transform nor
		// StaticMeshComponent. These are the "extras" we have to surface to the user before
		// the entity gets destroyed during the merge.
		std::vector<std::string> CollectExtraComponentNames(Entity* entity)
		{
			std::vector<std::string> extras;
			if (entity == nullptr)
				return extras;

			const auto components = entity->GetAllComponents();
			for (auto* comp : components)
			{
				if (comp == nullptr)
					continue;
				const char* name = comp->GetComponentName();
				if (name == nullptr)
					continue;
				const std::string_view view(name);
				if (view == "Transform" || view == "StaticMeshComponent")
					continue;
				extras.emplace_back(name);
			}
			// Deduplicate (defensive: multi-comp signatures are uncommon but legal).
			std::sort(extras.begin(), extras.end());
			extras.erase(std::unique(extras.begin(), extras.end()), extras.end());
			return extras;
		}

		// Builds the combined mesh, saves it to disk, swaps it onto `survivor`, and then
		// destroys every entity in `entitiesToDestroyAfter` (and their descendants, via
		// id-resolved walks). Returns true on success.
		//
		// `outputName` is the bare filename (no extension); we append .hmesh and place it
		// in EngineData/Meshes/Merged/, suffixing with _N if a file with the same stem
		// exists. `entitiesToDestroyAfter` is owned by the caller and may safely include
		// entries that are equal to `survivor` (we skip them) - this is what keeps the two
		// callers (hierarchy vs flat selection) sharing the same execution path even though
		// each has a different definition of what to clean up.
		bool ExecuteStaticMeshMerge(
			Entity* survivor,
			const std::vector<MergeSource>& sources,
			const std::vector<Entity*>& entitiesToDestroyAfter,
			const std::wstring& outputName)
		{
			if (survivor == nullptr || sources.size() < 2)
			{
				LOG_WARN("ExecuteStaticMeshMerge needs at least two source meshes");
				return false;
			}

			if (g_pEnv == nullptr || g_pEnv->_meshLoader == nullptr)
				return false;

			std::vector<MeshVertex> combinedVertices;
			std::vector<MeshIndexFormat> combinedIndices;
			combinedVertices.reserve(4096);
			combinedIndices.reserve(4096);

			fs::path materialFromFirstSource;

			for (const auto& source : sources)
			{
				if (source.mesh == nullptr)
					continue;

				if (source.mesh->HasAnimations())
				{
					LOG_WARN("Skipping animated mesh on entity '%s' during merge - not supported", source.entity != nullptr ? source.entity->GetName().c_str() : "?");
					continue;
				}

				const auto& srcVertices = source.mesh->GetVertices();
				const auto& srcIndices = source.mesh->GetIndices();
				if (srcVertices.empty() || srcIndices.empty())
					continue;

				if (materialFromFirstSource.empty())
				{
					const auto& matName = source.mesh->GetMaterialName();
					if (!matName.empty())
						materialFromFirstSource = fs::path(matName);
				}

				const MeshIndexFormat indexOffset = static_cast<MeshIndexFormat>(combinedVertices.size());

				// Bake the entity-to-root-local transform into each vertex. Position uses the
				// full affine transform; normal/tangent/bitangent use the rotation-only
				// transform (TransformNormal). With non-uniform scale this is approximate -
				// strictly correct would be inverse-transpose - but matches what most engines
				// ship by default and the visible discrepancy is tiny in practice.
				for (const auto& v : srcVertices)
				{
					MeshVertex out = v;
					const math::Vector3 srcPos(v._position.x, v._position.y, v._position.z);
					const math::Vector3 dstPos = math::Vector3::Transform(srcPos, source.entityToRootLocal);
					out._position = math::Vector4(dstPos.x, dstPos.y, dstPos.z, 1.0f);
					out._normal = math::Vector3::TransformNormal(v._normal, source.entityToRootLocal);
					out._normal.Normalize();
					out._tangent = math::Vector3::TransformNormal(v._tangent, source.entityToRootLocal);
					out._tangent.Normalize();
					out._bitangent = math::Vector3::TransformNormal(v._bitangent, source.entityToRootLocal);
					out._bitangent.Normalize();
					combinedVertices.push_back(out);
				}

				for (const auto idx : srcIndices)
				{
					combinedIndices.push_back(idx + indexOffset);
				}
			}

			if (combinedVertices.empty() || combinedIndices.empty())
			{
				LOG_WARN("Static mesh merge produced no geometry");
				return false;
			}

			// Resolve output path. Default to a per-project Meshes/Merged subdirectory; bump
			// the suffix if a file already exists so successive merges never silently stomp.
			std::wstring stem = outputName.empty() ? std::wstring(L"MergedMesh") : outputName;
			fs::path relativeDir = fs::path(L"Meshes") / L"Merged";
			fs::path outputPath = g_pEnv->GetFileSystem().GetLocalAbsoluteDataPath(relativeDir / (stem + L".hmesh"));
			if (fs::exists(outputPath))
			{
				for (int32_t i = 1; i < 4096; ++i)
				{
					fs::path candidate = g_pEnv->GetFileSystem().GetLocalAbsoluteDataPath(relativeDir / (stem + L"_" + std::to_wstring(i) + L".hmesh"));
					if (!fs::exists(candidate))
					{
						outputPath = candidate;
						break;
					}
				}
			}

			auto combinedMesh = std::shared_ptr<Mesh>(new Mesh(nullptr, outputPath.stem().string()), ResourceDeleter());
			combinedMesh->SetPaths(outputPath, &g_pEnv->GetFileSystem());
			combinedMesh->SetLoader(g_pEnv->_meshLoader);
			combinedMesh->SetNumFaces(static_cast<uint32_t>(combinedIndices.size() / 3));
			combinedMesh->AddVertices(combinedVertices);
			combinedMesh->AddIndices(combinedIndices);

			dx::BoundingBox aabb;
			dx::BoundingBox::CreateFromPoints(
				aabb,
				static_cast<size_t>(combinedVertices.size()),
				reinterpret_cast<const math::Vector3*>(combinedVertices.data()),
				sizeof(MeshVertex));
			combinedMesh->SetAABB(aabb);

			dx::BoundingOrientedBox obb;
			dx::BoundingOrientedBox::CreateFromBoundingBox(obb, aabb);
			combinedMesh->SetOBB(obb);

			if (!materialFromFirstSource.empty())
			{
				combinedMesh->SetMaterial(Material::Create(materialFromFirstSource));
			}
			else
			{
				combinedMesh->SetMaterial(Material::GetDefaultMaterial());
			}

			combinedMesh->Save();

			// Build GPU buffers so the mesh is renderable in the current session without a
			// resource reload round-trip.
			combinedMesh->CreateBuffers();

			// Swap the merged mesh onto the survivor. Re-uses the existing SMC if one is
			// already there, otherwise adds a fresh one.
			auto* survivorSmc = survivor->GetComponent<StaticMeshComponent>();
			if (survivorSmc == nullptr)
			{
				survivorSmc = survivor->AddComponent<StaticMeshComponent>();
			}
			if (survivorSmc != nullptr)
			{
				survivorSmc->SetMesh(combinedMesh);
			}

			// Destroy the caller-supplied list, expanded to also include every descendant
			// of each requested entity. Cleaning up descendants (not just the entity itself)
			// matters because Entity::DeleteMe(parent) only flags the immediate children -
			// any deeper subtree would otherwise be orphaned-flagged-but-not-scene-removed
			// (same root cause as the road-painter ghost-preview bug).
			//
			// Implementation note: Scene::DestroyEntity on an already-flagged entity runs
			// RemoveEntityInternal unconditionally, and its flagged path DOES recurse into
			// the entity's preserved _children. We therefore work with EntityIds (not raw
			// pointers) and re-resolve each via TryGetEntity per iteration, so any entity
			// a prior cascade already freed safely no-ops.
			Scene* scene = survivor->GetScene();
			if (scene != nullptr)
			{
				std::unordered_set<EntityId, EntityIdHasher> idSet;
				std::vector<EntityId> destroyIdsOrdered;
				auto stageEntity = [&](Entity* ent)
				{
					if (ent == nullptr || ent == survivor)
						return;
					const EntityId id = ent->GetId();
					if (idSet.insert(id).second)
						destroyIdsOrdered.push_back(id);
				};

				for (auto* requested : entitiesToDestroyAfter)
				{
					if (requested == nullptr || requested == survivor || requested->IsPendingDeletion())
						continue;

					stageEntity(requested);

					std::vector<Entity*> descendants;
					CollectAllDescendants(requested, descendants);
					for (auto* d : descendants)
					{
						stageEntity(d);
					}
				}

				for (const auto& id : destroyIdsOrdered)
				{
					Entity* live = scene->TryGetEntity(id);
					if (live == nullptr)
						continue;
					scene->DestroyEntity(live);
				}
			}

			return true;
		}

		// Counts contributors and surfaces extras (non-Transform/non-SMC components on
		// non-root entities). Returns false if there's nothing to merge.
		bool PrepareStaticMeshMergeSources(
			Entity* root,
			std::vector<MergeSource>& outSources,
			std::vector<std::pair<Entity*, std::vector<std::string>>>& outConflicts)
		{
			outSources.clear();
			outConflicts.clear();

			if (root == nullptr || root->IsPendingDeletion())
				return false;

			std::vector<Entity*> entities;
			CollectStaticMeshSources(root, entities);
			if (entities.size() < 2)
				return false;

			// Cache root's inverse world transform once - GetWorldTMInvert is non-const so
			// we have to call it on a non-const reference, hence the indirection through
			// `rootTM`.
			const math::Matrix rootWorldInv = root->GetWorldTMInvert();

			for (auto* ent : entities)
			{
				auto* smc = ent->GetComponent<StaticMeshComponent>();
				if (smc == nullptr)
					continue;

				auto mesh = smc->GetMesh();
				if (mesh == nullptr)
					continue;

				MergeSource source;
				source.entity = ent;
				source.mesh = mesh;
				source.entityToRootLocal = ent->GetWorldTM() * rootWorldInv;
				outSources.push_back(std::move(source));

				// Conflicts only matter for entities that will be destroyed - the root
				// survives, so anything attached to it stays. For descendants, list anything
				// that isn't Transform or StaticMeshComponent.
				if (ent != root)
				{
					auto extras = CollectExtraComponentNames(ent);
					if (!extras.empty())
					{
						outConflicts.emplace_back(ent, std::move(extras));
					}
				}
			}

			return outSources.size() >= 2;
		}

		void ShowMergeStaticMeshesDialog(Entity* root)
		{
			if (root == nullptr || root->IsPendingDeletion())
				return;

			std::vector<MergeSource> sources;
			std::vector<std::pair<Entity*, std::vector<std::string>>> conflicts;
			if (!PrepareStaticMeshMergeSources(root, sources, conflicts))
			{
				LOG_WARN("Cannot merge: '%s' must have at least two entities with StaticMeshComponent in its hierarchy", root->GetName().c_str());
				return;
			}

			// Dialog layout. Height grows when there are conflicts to display so the
			// conflict list is visible without scrolling for the typical small-N case.
			const int32_t dlgWidth = 480;
			const int32_t baseHeight = 160;
			const int32_t conflictPaneHeight = conflicts.empty() ? 0 : 180;
			const int32_t dlgHeight = baseHeight + conflictPaneHeight;

			auto* dlg = new Dialog(
				g_pEnv->GetUIManager().GetRootElement(),
				Point::GetScreenCenterWithOffset(-dlgWidth / 2, -dlgHeight / 2),
				Point(dlgWidth, dlgHeight),
				L"Merge Child Static Meshes");

			auto* outputName = new LineEdit(
				dlg,
				Point(12, 12),
				Point(dlgWidth - 24, 24),
				L"Output mesh name");
			outputName->SetValue(std::wstring(root->GetName().begin(), root->GetName().end()) + L"_Merged");
			outputName->SetDoesCallbackWaitForReturn(false);

			// Stored in a shared_ptr so its address is stable for the Checkbox to write
			// into AND its lifetime extends until the last lambda owning the ptr is
			// destroyed (i.e. until the dialog tears down). A plain raw `new bool` would
			// leak if the user closes the dialog via the X button rather than Cancel/Merge.
			auto forceMergeFlag = std::make_shared<bool>(false);

			int32_t cursorY = 46;
			if (!conflicts.empty())
			{
				auto* group = new GroupBox(
					dlg,
					Point(12, cursorY),
					Point(dlgWidth - 24, conflictPaneHeight - 8),
					L"Entities with extra components");

				auto* list = new ListBox(
					group,
					Point(8, 18),
					Point(dlgWidth - 40, conflictPaneHeight - 36));
				for (const auto& [conflictEntity, extras] : conflicts)
				{
					std::wstring label;
					label.append(conflictEntity->GetName().begin(), conflictEntity->GetName().end());
					label += L": ";
					for (size_t i = 0; i < extras.size(); ++i)
					{
						if (i > 0)
							label += L", ";
						label.append(extras[i].begin(), extras[i].end());
					}
					list->AddItem(label);
				}

				cursorY += conflictPaneHeight;

				new Checkbox(
					dlg,
					Point(12, cursorY),
					Point(dlgWidth - 24, 22),
					L"Force merge (extra components will be lost with their entities)",
					forceMergeFlag.get());
				cursorY += 30;
			}

			// Cancel + Merge buttons pinned to the bottom-right.
			new Button(
				dlg,
				Point(dlgWidth - 180, dlgHeight - 36),
				Point(78, 24),
				L"Cancel",
				[dlg](Button*) {
					dlg->DeleteMe();
					return true;
				});

			// Capture root by id so a late click that arrives after the entity was deleted
			// elsewhere safely no-ops instead of dereferencing a dangling pointer.
			Scene* scene = root->GetScene();
			const EntityId rootId = root->GetId();
			const bool hasConflicts = !conflicts.empty();

			new Button(
				dlg,
				Point(dlgWidth - 92, dlgHeight - 36),
				Point(78, 24),
				L"Merge",
				[dlg, outputName, forceMergeFlag, scene, rootId, hasConflicts](Button*) {
					if (hasConflicts && !(*forceMergeFlag))
					{
						LOG_WARN("Merge cancelled: extras present and force-merge not enabled");
						dlg->DeleteMe();
						return true;
					}

					Entity* liveRoot = (scene != nullptr) ? scene->TryGetEntity(rootId) : nullptr;
					if (liveRoot == nullptr || liveRoot->IsPendingDeletion())
					{
						LOG_WARN("Merge cancelled: root entity is no longer valid");
						dlg->DeleteMe();
						return true;
					}

					// Re-collect sources at execute time: the scene may have been edited
					// between dialog-open and Merge-click, so we don't trust the snapshot.
					std::vector<MergeSource> liveSources;
					std::vector<std::pair<Entity*, std::vector<std::string>>> liveConflicts;
					if (PrepareStaticMeshMergeSources(liveRoot, liveSources, liveConflicts))
					{
						// Hierarchy mode destroys EVERY descendant of the survivor so the
						// post-merge subtree has only `liveRoot` left holding the combined
						// mesh. Pass them all as the destroy list - ExecuteStaticMeshMerge
						// will further expand each into its own subtree if needed.
						std::vector<Entity*> destroyList;
						CollectAllDescendants(liveRoot, destroyList);
						ExecuteStaticMeshMerge(liveRoot, liveSources, destroyList, outputName->GetValue());
					}

					dlg->DeleteMe();
					return true;
				});
		}

		// Selection-mode source preparation. `selected[0]` is the survivor; every other
		// selected entity that has a StaticMeshComponent becomes a MergeSource. Non-survivor
		// selected entities with non-Transform/non-SMC components are recorded as conflicts
		// just like in the hierarchy path - they're about to be destroyed and the user
		// should know the side-component data goes with them.
		bool PrepareStaticMeshMergeSourcesFromSelection(
			const std::vector<Entity*>& selected,
			Entity*& outSurvivor,
			std::vector<MergeSource>& outSources,
			std::vector<std::pair<Entity*, std::vector<std::string>>>& outConflicts)
		{
			outSources.clear();
			outConflicts.clear();
			outSurvivor = nullptr;

			if (selected.size() < 2)
				return false;

			// First valid entity in the snapshot wins survivor status. Filtering null /
			// pending-deletion up-front guards against stale selections (the user might
			// have deleted something between Ctrl-click and right-click).
			Entity* survivor = nullptr;
			for (auto* candidate : selected)
			{
				if (candidate != nullptr && !candidate->IsPendingDeletion())
				{
					survivor = candidate;
					break;
				}
			}
			if (survivor == nullptr)
				return false;

			Scene* survivorScene = survivor->GetScene();
			const math::Matrix survivorWorldInv = survivor->GetWorldTMInvert();

			for (auto* ent : selected)
			{
				if (ent == nullptr || ent->IsPendingDeletion())
					continue;
				// Cross-scene selections are not coherent for a merge - the survivor's
				// scene is the only valid frame of reference. Skip mismatches silently.
				if (ent->GetScene() != survivorScene)
					continue;

				auto* smc = ent->GetComponent<StaticMeshComponent>();
				if (smc == nullptr)
				{
					// Non-SMC entry in the selection: not a contributor, but if it's not
					// the survivor it'll still be destroyed at merge time, so flag any
					// extra components it carries.
					if (ent != survivor)
					{
						auto extras = CollectExtraComponentNames(ent);
						if (!extras.empty())
							outConflicts.emplace_back(ent, std::move(extras));
					}
					continue;
				}

				auto mesh = smc->GetMesh();
				if (mesh == nullptr)
					continue;

				MergeSource source;
				source.entity = ent;
				source.mesh = mesh;
				source.entityToRootLocal = ent->GetWorldTM() * survivorWorldInv;
				outSources.push_back(std::move(source));

				if (ent != survivor)
				{
					auto extras = CollectExtraComponentNames(ent);
					if (!extras.empty())
						outConflicts.emplace_back(ent, std::move(extras));
				}
			}

			outSurvivor = survivor;
			return outSources.size() >= 2;
		}

		void ShowMergeSelectedStaticMeshesDialog(const std::vector<Entity*>& selected)
		{
			Entity* survivor = nullptr;
			std::vector<MergeSource> sources;
			std::vector<std::pair<Entity*, std::vector<std::string>>> conflicts;
			if (!PrepareStaticMeshMergeSourcesFromSelection(selected, survivor, sources, conflicts))
			{
				LOG_WARN("Cannot merge selection: needs at least two entities with StaticMeshComponent");
				return;
			}

			// Dialog body is identical in shape to the hierarchy dialog - same conflict-
			// list + force-merge ergonomics. Survivor name drives the default output stem
			// so the user can tell which entity will hold the merged mesh.
			const int32_t dlgWidth = 480;
			const int32_t baseHeight = 160;
			const int32_t conflictPaneHeight = conflicts.empty() ? 0 : 180;
			const int32_t dlgHeight = baseHeight + conflictPaneHeight;

			auto* dlg = new Dialog(
				g_pEnv->GetUIManager().GetRootElement(),
				Point::GetScreenCenterWithOffset(-dlgWidth / 2, -dlgHeight / 2),
				Point(dlgWidth, dlgHeight),
				L"Merge Selected Static Meshes");

			auto* outputName = new LineEdit(
				dlg,
				Point(12, 12),
				Point(dlgWidth - 24, 24),
				L"Output mesh name");
			outputName->SetValue(std::wstring(survivor->GetName().begin(), survivor->GetName().end()) + L"_Merged");
			outputName->SetDoesCallbackWaitForReturn(false);

			auto forceMergeFlag = std::make_shared<bool>(false);

			int32_t cursorY = 46;
			if (!conflicts.empty())
			{
				auto* group = new GroupBox(
					dlg,
					Point(12, cursorY),
					Point(dlgWidth - 24, conflictPaneHeight - 8),
					L"Selected entities with extra components");

				auto* list = new ListBox(
					group,
					Point(8, 18),
					Point(dlgWidth - 40, conflictPaneHeight - 36));
				for (const auto& [conflictEntity, extras] : conflicts)
				{
					std::wstring label;
					label.append(conflictEntity->GetName().begin(), conflictEntity->GetName().end());
					label += L": ";
					for (size_t i = 0; i < extras.size(); ++i)
					{
						if (i > 0)
							label += L", ";
						label.append(extras[i].begin(), extras[i].end());
					}
					list->AddItem(label);
				}

				cursorY += conflictPaneHeight;

				new Checkbox(
					dlg,
					Point(12, cursorY),
					Point(dlgWidth - 24, 22),
					L"Force merge (extra components will be lost with their entities)",
					forceMergeFlag.get());
				cursorY += 30;
			}

			// Capture selection by EntityId so a stale-pointer race after dialog open
			// never crashes - the Merge button re-resolves each id at execute time.
			Scene* scene = survivor->GetScene();
			std::vector<EntityId> selectionIds;
			selectionIds.reserve(selected.size());
			for (auto* e : selected)
			{
				if (e != nullptr)
					selectionIds.push_back(e->GetId());
			}
			const bool hasConflicts = !conflicts.empty();

			new Button(
				dlg,
				Point(dlgWidth - 180, dlgHeight - 36),
				Point(78, 24),
				L"Cancel",
				[dlg](Button*) {
					dlg->DeleteMe();
					return true;
				});

			new Button(
				dlg,
				Point(dlgWidth - 92, dlgHeight - 36),
				Point(78, 24),
				L"Merge",
				[dlg, outputName, forceMergeFlag, scene, selectionIds, hasConflicts](Button*) {
					if (hasConflicts && !(*forceMergeFlag))
					{
						LOG_WARN("Merge cancelled: extras present and force-merge not enabled");
						dlg->DeleteMe();
						return true;
					}

					if (scene == nullptr)
					{
						LOG_WARN("Merge cancelled: scene unavailable");
						dlg->DeleteMe();
						return true;
					}

					// Re-resolve every id at execute time so any entity destroyed between
					// dialog-open and Merge-click is silently filtered out.
					std::vector<Entity*> liveSelected;
					liveSelected.reserve(selectionIds.size());
					for (const auto& id : selectionIds)
					{
						if (Entity* live = scene->TryGetEntity(id); live != nullptr && !live->IsPendingDeletion())
							liveSelected.push_back(live);
					}

					Entity* liveSurvivor = nullptr;
					std::vector<MergeSource> liveSources;
					std::vector<std::pair<Entity*, std::vector<std::string>>> liveConflicts;
					if (PrepareStaticMeshMergeSourcesFromSelection(liveSelected, liveSurvivor, liveSources, liveConflicts))
					{
						// Selection mode: destroy every selected entity except the survivor
						// (ExecuteStaticMeshMerge expands each into its own subtree).
						std::vector<Entity*> destroyList;
						for (auto* e : liveSelected)
						{
							if (e != liveSurvivor)
								destroyList.push_back(e);
						}
						ExecuteStaticMeshMerge(liveSurvivor, liveSources, destroyList, outputName->GetValue());
					}

					dlg->DeleteMe();
					return true;
				});
		}
	}

	EntityList::EntityList(Element* parent, const HexEngine::Point& position, const HexEngine::Point& size) :
		TreeList(parent, position, size)
	{
		(void)GetState(this);
		CreateIcons();

		//_onSelect = std::bind(&EntityList::OnClickEntityInList, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
		//_onDragAndDrop = std::bind(&EntityList::OnDragAndDropEntity, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
	}

	EntityList::~EntityList()
	{
		g_entityListStates.erase(this);
	}

	bool EntityList::OnInputEvent(InputEvent event, InputData* data)
	{
		if (_ctx != nullptr && IsMouseOver(_ctx->GetAbsolutePosition(), _ctx->GetContextSize()) == false)
		{
			_ctx->DeleteMe();
			_ctx = nullptr;
			return TreeList::OnInputEvent(event, data);
		}

		if (_ctx && _ctx->IsEnabled())
		{
			return false;
		}

		return TreeList::OnInputEvent(event, data);
	}

	// Syncs the TreeList's multi-selection set to whatever entities are currently in
	// `state.selectedEntities`, so the visual highlight matches the logical selection.
	// Called whenever the click handler mutates the selection.
	void EntityList::SyncMultiSelectionRendering()
	{
		ClearMultiSelection();
		const auto& state = GetState(this);
		for (auto* ent : state.selectedEntities)
		{
			if (ent == nullptr)
				continue;
			if (auto* node = FindItemByObjectPtr(ent))
				AddToMultiSelection(node);
		}
	}

	void EntityList::OnClickEntityInList(ListNode* item, int32_t mouseButton)
	{
		if (item == nullptr)
			return;

		if (mouseButton == VK_LBUTTON)
		{
			auto* entity = ResolveEntityNode(item);
			if (entity == nullptr || entity->IsPendingDeletion())
				return;

			auto& state = GetState(this);
			const bool ctrlHeld = g_pEnv != nullptr && g_pEnv->_inputSystem != nullptr && g_pEnv->_inputSystem->IsCtrlDown();

			if (ctrlHeld)
			{
				// Ctrl-click toggles `entity` in the multi-selection without disturbing
				// the focused/anchor entity (which stays on whatever was previously
				// active so keyboard nav doesn't jump around mid-multi-select).
				auto it = std::find(state.selectedEntities.begin(), state.selectedEntities.end(), entity);
				if (it != state.selectedEntities.end())
				{
					state.selectedEntities.erase(it);
				}
				else
				{
					// First Ctrl-click also needs to include the previously focused entity
					// in the multi-set, otherwise the user appears to "lose" the original
					// selection the moment they Ctrl-click a second entity.
					if (state.selectedEntities.empty() && state.selectedEntity != nullptr && state.selectedEntity != entity)
						state.selectedEntities.push_back(state.selectedEntity);
					state.selectedEntities.push_back(entity);
				}
				SyncMultiSelectionRendering();
				// Don't fire _onEntityClicked here: that drives Inspector focus, which a
				// multi-select toggle shouldn't change.
				return;
			}

			// Plain click: drop the multi-selection, replace with the single focused entity.
			state.selectedEntity = entity;
			state.selectedEntities.clear();
			SyncMultiSelectionRendering();
			FocusEntity(entity);

			if (_onEntityClicked)
				_onEntityClicked(this, entity);
			return;
		}

		if (mouseButton == VK_RBUTTON)
		{
			if (auto* sceneNode = dynamic_cast<SceneListNode*>(item); sceneNode != nullptr)
			{
				int32_t mx, my;
				g_pEnv->_inputSystem->GetMousePosition(mx, my);

				Point p(mx - 1, my - 1);

				_ctx = new ContextMenu(this, p.RelativeTo(GetAbsolutePosition()));

				_ctx->AddItem(new ContextItem(L"Load Scene", std::bind(&EntityList::OnLoadScene, this, sceneNode)));
				return;
			}

			auto* entity = ResolveEntityNode(item);
			if (entity == nullptr || entity->IsPendingDeletion())
				return;

			int32_t mx, my;
			g_pEnv->_inputSystem->GetMousePosition(mx, my);

			Point p(mx - 1, my - 1);

			_ctx = new ContextMenu(this, p.RelativeTo(GetAbsolutePosition()));

			_ctx->AddItem(new ContextItem(L"Duplicate", std::bind(&EntityList::DuplicateEntity, this, entity)));
			_ctx->AddItem(new ContextItem(L"Save as prefab", std::bind(&EntityList::SaveAsPrefab, this, entity, &g_pEnv->GetFileSystem())));

			// Merge menu items - two shapes depending on what the user has selected:
			//   * Selection mode: if 2+ entities are in the multi-select set AND the
			//     right-clicked entity is one of them, offer "Merge Selected Static Meshes"
			//     which operates on the flat selection (anchor = first selected).
			//   * Hierarchy mode: otherwise, if the right-clicked entity (plus descendants)
			//     contains 2+ StaticMeshComponents, offer "Merge Child Static Meshes" which
			//     walks the subtree under that single entity.
			// Both share the same merge plumbing (PerformStaticMeshMerge) - only how the
			// contributor set is gathered differs.
			{
				const auto& multiSelected = GetState(this).selectedEntities;
				const bool isRightClickedInMulti = std::find(multiSelected.begin(), multiSelected.end(), entity) != multiSelected.end();
				if (isRightClickedInMulti && multiSelected.size() >= 2)
				{
					std::vector<Entity*> selectionSnapshot = multiSelected;
					_ctx->AddItem(new ContextItem(L"Merge Selected Static Meshes",
						[selectionSnapshot](const std::wstring&) { ShowMergeSelectedStaticMeshesDialog(selectionSnapshot); }));
				}
				else
				{
					std::vector<Entity*> meshSources;
					CollectStaticMeshSources(entity, meshSources);
					if (meshSources.size() >= 2)
					{
						_ctx->AddItem(new ContextItem(L"Merge Child Static Meshes",
							[entity](const std::wstring&) { ShowMergeStaticMeshesDialog(entity); }));
					}
				}
			}
		}
	}

	Entity* EntityList::ResolveEntityNode(const ListNode* node) const
	{
		if (node == nullptr)
			return nullptr;

		if (dynamic_cast<const SceneListNode*>(node) != nullptr)
			return nullptr;

		return node->GetObjectAs<Entity>();
	}

	Scene* EntityList::ResolveSceneNode(const ListNode* node) const
	{
		if (auto* sceneNode = dynamic_cast<const SceneListNode*>(node); sceneNode != nullptr)
		{
			return sceneNode->GetScene();
		}

		if (auto* entity = ResolveEntityNode(node); entity != nullptr)
		{
			return entity->GetScene();
		}

		return nullptr;
	}

	bool EntityList::IsAncestorOf(const Entity* source, const Entity* potentialChild) const
	{
		if (source == nullptr || potentialChild == nullptr)
			return false;

		for (auto* ancestor = potentialChild->GetParent(); ancestor != nullptr; ancestor = ancestor->GetParent())
		{
			if (ancestor == source)
				return true;
		}

		return false;
	}

	ListNode* EntityList::AddEntityInternal(Entity* entity, ListNode* sceneNode, std::unordered_set<Entity*>& parentWalkGuard)
	{
		if (entity == nullptr || sceneNode == nullptr || entity->IsPendingDeletion())
			return nullptr;

		Scene* scene = ResolveSceneNode(sceneNode);
		if (scene == nullptr || entity->GetScene() != scene)
			return nullptr;

		if (!parentWalkGuard.insert(entity).second)
			return FindItemByObjectPtr(entity, sceneNode);

		auto removeFromGuard = [&]()
		{
			parentWalkGuard.erase(entity);
		};

		if (auto* existingNode = FindItemByObjectPtr(entity, sceneNode); existingNode != nullptr)
		{
			removeFromGuard();
			return existingNode;
		}

		ListNode* parentNode = sceneNode;
		if (auto* parent = entity->GetParent(); parent != nullptr && parent->GetScene() == scene)
		{
			parentNode = AddEntityInternal(parent, sceneNode, parentWalkGuard);
			if (parentNode == nullptr)
				parentNode = sceneNode;
		}

		const std::wstring entName(entity->GetName().begin(), entity->GetName().end());
		auto* node = new ListNode(this, entName, { _icons[IconId::Entity].get() }, entity);
		node->_onClick = std::bind(&EntityList::OnClickEntityInList, this, std::placeholders::_1, std::placeholders::_2);
		node->_onDragAndDrop = std::bind(&EntityList::OnDragAndDropEntity, this, this, std::placeholders::_1, std::placeholders::_2);
		auto& listState = GetState(this);
		if (auto state = listState.entityOpenState.find(entity); state != listState.entityOpenState.end())
		{
			node->SetOpen(state->second);
		}
		else if (!listState.filterTokens.empty())
		{
			node->SetOpen(true);
		}

		AddNode(node, parentNode, false);

		removeFromGuard();
		return node;
	}

	void EntityList::OnLoadScene(SceneListNode* node)
	{
		auto currentScene = g_pEnv->_sceneManager->GetCurrentScene();
		if (currentScene == nullptr || node == nullptr)
			return;

		currentScene->SetFlags(SceneFlags::Disabled);
		node->GetScene()->SetFlags(SceneFlags::Renderable | SceneFlags::Updateable | SceneFlags::PostProcessingEnabled);
	}

	bool EntityList::OnDragAndDropEntity(TreeList* list, ListNode* dragSource, ListNode* dragTarget)
	{
		(void)list;

		if (dragSource == nullptr || dragTarget == nullptr)
			return false;

		auto* sourceEnt = ResolveEntityNode(dragSource);
		if (sourceEnt == nullptr || sourceEnt->IsPendingDeletion())
			return false;

		auto* targetEnt = ResolveEntityNode(dragTarget);
		auto* sourceScene = sourceEnt->GetScene();
		auto* targetScene = ResolveSceneNode(dragTarget);

		if (sourceScene == nullptr || targetScene == nullptr || sourceScene != targetScene)
			return false;

		if (sourceEnt == targetEnt)
			return false;

		if (targetEnt != nullptr && (targetEnt->IsPendingDeletion() || IsAncestorOf(sourceEnt, targetEnt)))
			return false;

		if (sourceEnt->GetParent() == targetEnt)
			return true;

		if (_onEntityParented)
			_onEntityParented(this, sourceEnt, targetEnt);

		RefreshList();
		return true;
	}

	void EntityList::RefreshList()
	{
		auto& state = GetState(this);
		CaptureTreeState();
		Clear();

		auto scene = g_pEnv->_sceneManager->GetCurrentScene();
		if (scene == nullptr)
			return;

		auto* sceneRoot = AddScene(scene);
		if (sceneRoot == nullptr)
			return;

		std::vector<Entity*> entities;
		std::unordered_set<Entity*> uniqueEntities;
		scene->Lock();
		for (const auto& entSet : scene->GetEntities())
		{
			for (auto* ent : entSet.second)
			{
				if (ent != nullptr &&
					!ent->IsPendingDeletion() &&
					ent->GetScene() == scene.get() &&
					uniqueEntities.insert(ent).second)
				{
					entities.push_back(ent);
				}
			}
		}
		scene->Unlock();

		std::sort(entities.begin(), entities.end(),
			[](const Entity* lhs, const Entity* rhs)
			{
				return lhs->GetName() < rhs->GetName();
			});

		std::unordered_set<Entity*> filteredVisibilitySet;
		if (!state.filterTokens.empty())
		{
			for (auto* entity : entities)
			{
				if (!DoesEntityMatchFilter(entity))
					continue;

				for (auto* chain = entity; chain != nullptr && chain->GetScene() == scene.get(); chain = chain->GetParent())
				{
					filteredVisibilitySet.insert(chain);
				}
			}
		}

		for (auto* entity : entities)
		{
			if (!state.filterTokens.empty() && filteredVisibilitySet.find(entity) == filteredVisibilitySet.end())
				continue;

			std::unordered_set<Entity*> parentWalkGuard;
			AddEntityInternal(entity, sceneRoot, parentWalkGuard);
		}

		if (state.selectedEntity != nullptr && state.selectedEntity->GetScene() == scene.get() && !state.selectedEntity->IsPendingDeletion())
		{
			FocusEntity(state.selectedEntity);
		}
	}

	void EntityList::SetFilterText(const std::wstring& filterText)
	{
		auto& state = GetState(this);
		if (state.filterText == filterText)
			return;

		state.filterText = filterText;
		RebuildFilterTokens();
		RefreshList();
	}

	const std::wstring& EntityList::GetFilterText() const
	{
		return GetStateConst(this).filterText;
	}

	bool EntityList::FocusEntity(Entity* entity)
	{
		if (entity == nullptr || entity->IsPendingDeletion())
			return false;

		auto& state = GetState(this);
		state.selectedEntity = entity;

		auto* node = FindItemByObjectPtr(entity);
		if (node == nullptr)
			return false;

		for (auto* parent = node->GetParent(); parent != nullptr; parent = parent->GetParent())
		{
			parent->SetOpen(true);

			if (auto* parentEntity = ResolveEntityNode(parent); parentEntity != nullptr)
			{
				state.entityOpenState[parentEntity] = true;
			}
			else if (auto* parentScene = ResolveSceneNode(parent); parentScene != nullptr)
			{
				state.sceneOpenState[parentScene] = true;
			}
		}

		state.entityOpenState[entity] = node->IsOpen();
		SetSelectedItem(node, false);
		ScrollToItem(node, 28);
		Repaint();
		return true;
	}

	void EntityList::DuplicateEntity(Entity* entity)
	{
		if (entity == nullptr)
			return;

		auto* currentScene = g_pEnv->_sceneManager->GetCurrentScene().get();
		auto* duplicate = CloneEntityHierarchy(currentScene, entity);
		TryAutoLinkDuplicatedTrafficLane(entity, duplicate);
		if (_onEntityDuplicated && duplicate != nullptr)
		{
			_onEntityDuplicated(this, entity, duplicate);
		}

		if (_ctx != nullptr)
		{
			_ctx->DeleteMe();
			_ctx = nullptr;
		}
	}

	void EntityList::SaveAsPrefab(Entity* entity, FileSystem* fs)
	{
		auto sceneManager = g_pEnv->_sceneManager;
		auto currentScene = sceneManager->GetCurrentScene();
		auto prefabScene = sceneManager->CreateEmptyScene(false);

		sceneManager->SetActiveScene(prefabScene);
		auto rootEntity = prefabScene->CloneEntity(entity);

		for (auto children : entity->GetChildren())
		{
			auto newChild = prefabScene->CloneEntity(children, false);

			newChild->SetParent(rootEntity);
		}

		SceneSaveFile saveFile(
			fs->GetLocalAbsoluteDataPath("Prefabs/" + entity->GetName() + ".hprefab"),
			std::ios::out,
			prefabScene,
			SceneFileFlags::DontSaveVariables);

		saveFile.Save();
		
		sceneManager->SetActiveScene(currentScene);
	}

	void EntityList::CreateIcons()
	{
		_icons[IconId::Entity] = HexEngine::ITexture2D::Create("EngineData.Textures/UI/entity.png");
		_icons[IconId::Folder] = HexEngine::ITexture2D::Create("EngineData.Textures/UI/folder.png");
		_icons[IconId::Scene] = HexEngine::ITexture2D::Create("EngineData.Textures/UI/scene.png");
	}

	ListNode* EntityList::AddScene(const std::shared_ptr<Scene>& scene)
	{
		if (scene == nullptr)
			return nullptr;

		auto sceneNode = new SceneListNode(this, scene->GetName(), { _icons[IconId::Scene].get(), _icons[IconId::Scene].get() }, scene.get());
		auto& listState = GetState(this);
		if (auto state = listState.sceneOpenState.find(scene.get()); state != listState.sceneOpenState.end())
		{
			sceneNode->SetOpen(state->second);
		}
		else
		{
			sceneNode->SetOpen(true);
		}

		if (_onSceneClicked)
		{
			sceneNode->_onClick = [this, scenePtr = sceneNode->GetScene()](ListNode*, int32_t mouseButton)
			{
				if (mouseButton == VK_LBUTTON && _onSceneClicked)
					_onSceneClicked(this, scenePtr);
			};
		}

		AddNode(sceneNode, nullptr, false);

		return sceneNode;
	}

	void EntityList::AddEntity(HexEngine::Entity* entity, ListNode* scene)
	{
		(void)entity;
		(void)scene;
		RefreshList();
	}

	void EntityList::AddEntity(HexEngine::Entity* entity)
	{
		if (entity == nullptr || entity->IsPendingDeletion())
			return;

		auto scene = g_pEnv->_sceneManager->GetCurrentScene();
		if (scene == nullptr || entity->GetScene() != scene.get())
			return;

		auto& state = GetState(this);
		state.selectedEntity = entity;
		RefreshList();
	}

	void EntityList::RemoveEntity(HexEngine::Entity* entity)
	{
		(void)entity;
		RefreshList();

		//auto& name = entity->GetName();
		//RemoveItem(std::wstring(name.begin(), name.end()));
	}

	void EntityList::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		TreeList::Render(renderer, w, h);
	}

	void EntityList::CaptureTreeState()
	{
		for (auto* root : GetRootItems())
		{
			CaptureNodeStateRecursive(root);
		}
	}

	void EntityList::CaptureNodeStateRecursive(const ListNode* node)
	{
		if (node == nullptr)
			return;

		if (auto* sceneNode = dynamic_cast<const SceneListNode*>(node); sceneNode != nullptr)
		{
			auto& state = GetState(this);
			state.sceneOpenState[sceneNode->GetScene()] = sceneNode->IsOpen();
		}
		else if (auto* entity = ResolveEntityNode(node); entity != nullptr)
		{
			auto& state = GetState(this);
			state.entityOpenState[entity] = node->IsOpen();
		}

		for (auto* child : node->GetChildren())
		{
			CaptureNodeStateRecursive(child);
		}
	}

	void EntityList::RebuildFilterTokens()
	{
		auto& state = GetState(this);
		state.filterTokens.clear();

		std::wstring currentToken;
		currentToken.reserve(state.filterText.size());

		for (wchar_t ch : state.filterText)
		{
			if (std::iswspace(ch))
			{
				if (!currentToken.empty())
				{
					state.filterTokens.push_back(currentToken);
					currentToken.clear();
				}
				continue;
			}

			currentToken.push_back((wchar_t)std::towlower(ch));
		}

		if (!currentToken.empty())
		{
			state.filterTokens.push_back(currentToken);
		}
	}

	bool EntityList::DoesEntityMatchFilter(const Entity* entity) const
	{
		if (entity == nullptr)
			return false;

		const auto& state = GetStateConst(this);
		if (state.filterTokens.empty())
			return true;

		std::wstring loweredName(entity->GetName().begin(), entity->GetName().end());
		std::transform(loweredName.begin(), loweredName.end(), loweredName.begin(),
			[](wchar_t ch)
			{
				return (wchar_t)std::towlower(ch);
			});

		for (const auto& token : state.filterTokens)
		{
			if (token.empty())
				continue;

			if (loweredName.find(token) == std::wstring::npos)
				return false;
		}

		return true;
	}
}
