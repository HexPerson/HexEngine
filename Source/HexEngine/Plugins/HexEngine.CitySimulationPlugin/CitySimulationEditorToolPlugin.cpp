#include "CitySimulationEditorToolPlugin.hpp"
#include "CitySimulationInterface.hpp"
#include "RoadComponent.hpp"
#include "VehicleComponent.hpp"

#include <HexEngine.Core/Entity/Component/RigidBody.hpp>
#include <HexEngine.Core/Entity/Component/StaticMeshComponent.hpp>
#include <HexEngine.Core/FileSystem/PrefabLoader.hpp>
#include <HexEngine.Core/FileSystem/SceneSaveFile.hpp>
#include <HexEngine.Core/Graphics/Material.hpp>
#include <HexEngine.Core/GUI/Elements/AssetSearch.hpp>
#include <HexEngine.Core/GUI/Elements/Button.hpp>
#include <HexEngine.Core/GUI/Elements/Checkbox.hpp>
#include <HexEngine.Core/GUI/Elements/ComponentWidget.hpp>
#include <HexEngine.Core/GUI/Elements/Dialog.hpp>
#include <HexEngine.Core/GUI/Elements/DragFloat.hpp>
#include <HexEngine.Core/GUI/Elements/DropDown.hpp>
#include <HexEngine.Core/Physics/IRigidBody.hpp>
#include <HexEngine.Core/Scene/Mesh.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>
#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>

	namespace
	{
		using namespace HexEngine;

	constexpr const char* kRoadPainterWrapperPrefix = "CityRoadDraw_";
	constexpr const char* kRoadPainterPreviewPrefix = "CityRoadPreview_";
	constexpr const char* kRoadNetworkRootName = "RoadNetwork";
	constexpr float kHalfPi = 1.57079632679f;

	int32_t NormalizeQuarterTurns(int32_t quarterTurns)
	{
		quarterTurns %= 4;
		if (quarterTurns < 0)
			quarterTurns += 4;
		return quarterTurns;
	}

	std::wstring QuarterTurnLabel(int32_t quarterTurns)
	{
		switch (NormalizeQuarterTurns(quarterTurns))
		{
		case 1: return L"90";
		case 2: return L"180";
		case 3: return L"270";
		case 0:
		default: return L"0";
		}
	}

	bool ApplyQuarterTurnToAxis(bool usesXAxis, int32_t quarterTurns)
	{
		return (NormalizeQuarterTurns(quarterTurns) % 2) == 0 ? usesXAxis : !usesXAxis;
	}

	void ApplyQuarterTurnRotation(math::Quaternion& rotation, int32_t quarterTurns)
	{
		const int32_t normalizedTurns = NormalizeQuarterTurns(quarterTurns);
		if (normalizedTurns == 0)
			return;

		rotation = rotation * math::Quaternion::CreateFromYawPitchRoll(kHalfPi * static_cast<float>(normalizedTurns), 0.0f, 0.0f);
		rotation.Normalize();
	}

	enum DirectionMask : uint8_t
	{
		DirectionNone = 0,
		DirectionNorth = 1 << 0,
		DirectionEast = 1 << 1,
		DirectionSouth = 1 << 2,
		DirectionWest = 1 << 3,
	};

	struct GridCoord
	{
		int32_t x = 0;
		int32_t z = 0;

		auto operator<=>(const GridCoord&) const = default;
	};

	struct GridCoordHash
	{
		size_t operator()(const GridCoord& value) const noexcept
		{
			const auto hx = static_cast<uint32_t>(value.x);
			const auto hz = static_cast<uint32_t>(value.z);
			return (static_cast<size_t>(hx) << 32) ^ static_cast<size_t>(hz);
		}
	};

	struct PlacementSpec
	{
		fs::path assetPath;
		bool isPrefab = false;
		float length = 1.0f;
		bool usesXAxis = false;
		// Mesh-local AABB centre in the XZ plane. Used by the spawn code to subtract
		// the rotated centre from the wrapper position, so that whether the user
		// authored their road piece with origin at the centre, at a corner, or
		// anywhere in between, the visual extent lands centred on the cell's grid
		// coord. Without this correction a corner-authored mesh (origin at one edge)
		// renders offset by half a cell, which silently mis-aligns the snap grid
		// versus where the road piece actually sits - the user clicks the visual end
		// expecting to land on that cell, but the click snaps to the NEXT cell over
		// and the would-be corner cell gets no second connection.
		math::Vector2 aabbCenterXZ = math::Vector2(0.0f, 0.0f);
	};

	struct CellPlacement
	{
		fs::path assetPath;
		bool isPrefab = false;
		math::Quaternion rotation = math::Quaternion::Identity;
		// Copied from PlacementSpec at choose time so the spawner can apply the
		// centring offset without having to look up which spec drove the placement.
		math::Vector2 aabbCenterXZ = math::Vector2(0.0f, 0.0f);
	};

	std::string MakeManagedWrapperName(const GridCoord& coord)
	{
		return std::format("{}{}_{}", kRoadPainterWrapperPrefix, coord.x, coord.z);
	}

	bool TryParseManagedWrapperName(const std::string& name, GridCoord& outCoord)
	{
		if (name.rfind(kRoadPainterWrapperPrefix, 0) != 0)
			return false;

		const std::string suffix = name.substr(std::char_traits<char>::length(kRoadPainterWrapperPrefix));
		const size_t separator = suffix.find('_');
		if (separator == std::string::npos)
			return false;

		try
		{
			outCoord.x = std::stoi(suffix.substr(0, separator));
			outCoord.z = std::stoi(suffix.substr(separator + 1));
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	// Snap a world-space value to the editor's translate-snap grid (the same grid the
	// translation gizmo uses, controlled by ed_translateSnap + ed_translateSnapSize).
	// Used for FIRST-CLICK placement so the anchor lands on the editor's fine grid
	// instead of the much coarser road cell size. Returns `value` unchanged when
	// snapping is disabled or the HVars are unavailable.
	float SnapToEditorGrid(float value)
	{
		if (g_pEnv == nullptr || g_pEnv->_commandManager == nullptr)
			return value;
		auto* enabledVar = g_pEnv->_commandManager->FindHVar("ed_translateSnap");
		auto* sizeVar = g_pEnv->_commandManager->FindHVar("ed_translateSnapSize");
		if (enabledVar == nullptr || sizeVar == nullptr)
			return value;
		if (!enabledVar->_val.b)
			return value;
		const float snapSize = std::max(sizeVar->_val.f32, 0.001f);
		return std::round(value / snapSize) * snapSize;
	}

	// Snap a world coord into a cell index relative to an arbitrary origin offset.
	// Reads "how many cellSize steps from origin is worldValue?" - the network origin
	// (cell (0,0)'s world position) need NOT be at world (0,0,0); it's the snapped
	// position of the FIRST click, so the road grid is anchored wherever the user
	// initially placed it rather than to absolute world coordinates.
	int32_t SnapToGrid(float worldValue, float originOffset, float cellSize)
	{
		if (cellSize <= 0.0001f)
			return 0;
		return static_cast<int32_t>(std::lround((worldValue - originOffset) / cellSize));
	}

	math::Vector3 GridToWorld(const GridCoord& coord, const math::Vector2& origin, float cellSize, float y)
	{
		return math::Vector3(
			origin.x + static_cast<float>(coord.x) * cellSize,
			y,
			origin.y + static_cast<float>(coord.z) * cellSize);
	}

	// Subtracts the rotated mesh-local AABB centre from the cell's grid-world
	// position so the placed mesh's visual centre ends up exactly on the grid
	// coord. Corrects for road assets authored with origin at a corner (or any
	// off-centre origin): without this correction the visual extent runs from
	// grid*L to (grid+1)*L while the snap grid + anchor wireframe centre at
	// grid*L, so clicking the visual right edge of a cell snaps to the NEXT
	// cell - which is why perpendicular runs failed to overlap the previous
	// run's last cell and corners never formed.
	math::Vector3 ApplyMeshCentreCorrection(const math::Vector3& gridWorld, const math::Vector2& aabbCenterXZ, const math::Quaternion& rotation)
	{
		const math::Vector3 centerLocal(aabbCenterXZ.x, 0.0f, aabbCenterXZ.y);
		const math::Vector3 centerWorld = math::Vector3::Transform(centerLocal, rotation);
		return math::Vector3(gridWorld.x - centerWorld.x, gridWorld.y, gridWorld.z - centerWorld.z);
	}

	// Destroys `wrapper` AND every descendant that was parented under it. The
	// straightforward Scene::DestroyEntity(parent) call leaves children orphaned
	// because DeleteMe(parent) empties parent->_children before Scene::DestroyEntity's
	// internal recursive loop reads it, so the cascade runs against an empty list
	// and the deeper subtree stays in the scene with IsPendingRemoval set,
	// rendering off its last PVS snapshot. For prefab-based road pieces this shows
	// up as a stale straight mesh hanging around after a corner replaces it.
	//
	// Implementation: post-order traversal (leaves first), and each entity detaches
	// itself from its parent BEFORE being destroyed. The detach matters - if we
	// destroyed a child without detaching, its parent's _children would still hold
	// the now-dangling pointer and the parent's later DeleteMe would dereference
	// it (use-after-free). Doing the work bottom-up means by the time we reach the
	// wrapper, every entity below it is already gone and the wrapper's children
	// list is genuinely empty - no relying on Scene::DestroyEntity's cascade at
	// all.
	void DestroyWrapperAndDescendants(Scene* scene, Entity* wrapper)
	{
		if (scene == nullptr || wrapper == nullptr)
			return;

		std::vector<EntityId> destroyOrder;
		std::function<void(Entity*)> collect = [&](Entity* e)
		{
			if (e == nullptr)
				return;
			// Snapshot the children list - destruction will mutate the live one,
			// and we need to recurse over what's there BEFORE we touch anything.
			const std::vector<Entity*> children = e->GetChildren();
			for (auto* child : children)
			{
				if (child == nullptr)
					continue;
				collect(child);
			}
			destroyOrder.push_back(e->GetId());
		};
		collect(wrapper);

		for (const auto& id : destroyOrder)
		{
			Entity* e = scene->TryGetEntity(id);
			if (e == nullptr)
				continue; // already gone (shouldn't happen in post-order, defensive)

			// Detach from parent before destroying so the parent's _children doesn't
			// hold a dangling pointer when its own destruction iterates the list.
			if (auto* parent = e->GetParent(); parent != nullptr)
			{
				e->SetParent(nullptr);
			}

			scene->DestroyEntity(e);
		}
	}

	// Strips a wrapper's content (children + mesh/physics components) without destroying
	// the wrapper itself. Used by the corner-upgrade path: when a straight cell becomes a
	// corner we want to KEEP the wrapper entity (and its scene name "CityRoadDraw_X_Z")
	// because Scene::CreateEntity with a duplicate name either returns the existing entity
	// or auto-renames the new one - both leave the OLD wrapper alive and rendering its
	// stale straight mesh on top of the new corner. By re-using the wrapper we sidestep
	// the duplicate-name path entirely: the wrapper persists across the upgrade, only its
	// children and components change.
	//
	// Uses the same bottom-up + detach destruction as DestroyWrapperAndDescendants, just
	// applied to each child subtree rather than starting from the wrapper itself. See that
	// function's comment for why Scene::DestroyEntity alone is insufficient.
	void ClearWrapperContent(Scene* scene, Entity* wrapper)
	{
		if (scene == nullptr || wrapper == nullptr)
			return;

		std::vector<EntityId> destroyOrder;
		std::function<void(Entity*)> collect = [&](Entity* e)
		{
			if (e == nullptr)
				return;
			const std::vector<Entity*> children = e->GetChildren();
			for (auto* child : children)
			{
				if (child == nullptr)
					continue;
				collect(child);
			}
			destroyOrder.push_back(e->GetId());
		};

		// Snapshot top-level children once, then recurse - we want each subtree fully
		// collected before any destruction starts so the live _children mutations don't
		// trip the traversal.
		const std::vector<Entity*> directChildren = wrapper->GetChildren();
		for (auto* child : directChildren)
		{
			if (child == nullptr)
				continue;
			collect(child);
		}

		for (const auto& id : destroyOrder)
		{
			Entity* e = scene->TryGetEntity(id);
			if (e == nullptr)
				continue;
			if (auto* parent = e->GetParent(); parent != nullptr)
			{
				e->SetParent(nullptr);
			}
			scene->DestroyEntity(e);
		}

		// Strip wrapper-level components that the spawn paths add. SpawnMeshCell attaches
		// a StaticMeshComponent (and optionally a RigidBody) directly to the wrapper, so
		// without removing them an upgrade from mesh->prefab (or mesh->mesh with a different
		// asset) would inherit the previous cell's mesh component on top of the new prefab
		// subtree and you'd see both meshes overlapping.
		if (auto* smc = wrapper->GetComponent<HexEngine::StaticMeshComponent>(); smc != nullptr)
		{
			wrapper->RemoveComponent<HexEngine::StaticMeshComponent>();
		}
		if (auto* rb = wrapper->GetComponent<HexEngine::RigidBody>(); rb != nullptr)
		{
			wrapper->RemoveComponent<HexEngine::RigidBody>();
		}
	}

	uint8_t RotateMaskClockwise(uint8_t mask)
	{
		uint8_t rotated = DirectionNone;
		if ((mask & DirectionNorth) != 0)
			rotated |= DirectionEast;
		if ((mask & DirectionEast) != 0)
			rotated |= DirectionSouth;
		if ((mask & DirectionSouth) != 0)
			rotated |= DirectionWest;
		if ((mask & DirectionWest) != 0)
			rotated |= DirectionNorth;
		return rotated;
	}

	int32_t CountConnections(uint8_t mask)
	{
		int32_t count = 0;
		if ((mask & DirectionNorth) != 0) ++count;
		if ((mask & DirectionEast) != 0) ++count;
		if ((mask & DirectionSouth) != 0) ++count;
		if ((mask & DirectionWest) != 0) ++count;
		return count;
	}

	bool IsOpposingStraight(uint8_t mask)
	{
		return mask == (DirectionNorth | DirectionSouth) || mask == (DirectionEast | DirectionWest);
	}

	bool ComputeRotationForMask(uint8_t desiredMask, uint8_t baseMask, math::Quaternion& outRotation)
	{
		uint8_t rotatedMask = baseMask;
		for (int32_t step = 0; step < 4; ++step)
		{
			if (rotatedMask == desiredMask)
			{
				outRotation = math::Quaternion::CreateFromYawPitchRoll(kHalfPi * static_cast<float>(step), 0.0f, 0.0f);
				return true;
			}
			rotatedMask = RotateMaskClockwise(rotatedMask);
		}

		outRotation = math::Quaternion::Identity;
		return false;
	}

	bool TryMeasureMeshAsset(const fs::path& assetPath, float& outSizeX, float& outSizeZ, math::Vector2& outCenterXZ)
	{
		auto mesh = Mesh::Create(assetPath);
		if (mesh == nullptr)
			return false;

		const auto bounds = mesh->GetAABB();
		outSizeX = std::max(bounds.Extents.x * 2.0f, 0.01f);
		outSizeZ = std::max(bounds.Extents.z * 2.0f, 0.01f);
		outCenterXZ = math::Vector2(bounds.Center.x, bounds.Center.z);
		return true;
	}

	bool TryMeasurePrefabAsset(const fs::path& assetPath, float& outSizeX, float& outSizeZ, math::Vector2& outCenterXZ)
	{
		auto tempScene = g_pEnv->_sceneManager->CreateEmptyScene(false);
		if (tempScene == nullptr)
			return false;

		auto entities = g_pEnv->_prefabLoader->LoadPrefab(tempScene, assetPath);
		if (entities.empty())
			return false;

		math::Vector3 boundsMin(std::numeric_limits<float>::max());
		math::Vector3 boundsMax(-std::numeric_limits<float>::max());
		bool hasBounds = false;

		for (auto* entity : entities)
		{
			if (entity == nullptr || entity->IsPendingDeletion())
				continue;

			if (entity->HasA<StaticMeshComponent>() == false)
				continue;

			const auto worldBounds = entity->GetWorldAABB();
			const math::Vector3 center(worldBounds.Center);
			const math::Vector3 extents(worldBounds.Extents);
			const math::Vector3 minValue = center - extents;
			const math::Vector3 maxValue = center + extents;
			boundsMin = math::Vector3::Min(boundsMin, minValue);
			boundsMax = math::Vector3::Max(boundsMax, maxValue);
			hasBounds = true;
		}

		if (!hasBounds)
			return false;

		const math::Vector3 size = boundsMax - boundsMin;
		outSizeX = std::max(size.x, 0.01f);
		outSizeZ = std::max(size.z, 0.01f);
		// Centre of the prefab's combined AABB. Same role as the mesh-asset path's
		// AABB centre - lets the spawn code shift the wrapper position so the
		// prefab's visual extent ends up centred on the cell's grid coord regardless
		// of how the prefab was authored relative to its own origin.
		outCenterXZ.x = (boundsMin.x + boundsMax.x) * 0.5f;
		outCenterXZ.y = (boundsMin.z + boundsMax.z) * 0.5f;
		return true;
	}

	bool TryBuildPlacementSpec(const fs::path& assetPath, int32_t yawQuarterTurns, PlacementSpec& outSpec)
	{
		if (assetPath.empty())
			return false;

		outSpec = {};
		outSpec.assetPath = assetPath;
		outSpec.isPrefab = assetPath.extension() == ".hprefab";
		float sizeX = 0.0f;
		float sizeZ = 0.0f;
		math::Vector2 centerXZ(0.0f, 0.0f);
		const bool measured = outSpec.isPrefab
			? TryMeasurePrefabAsset(assetPath, sizeX, sizeZ, centerXZ)
			: TryMeasureMeshAsset(assetPath, sizeX, sizeZ, centerXZ);
		if (!measured)
			return false;

		outSpec.aabbCenterXZ = centerXZ;

		// Convention: road meshes are authored with the travel/connection direction along +Z,
		// so the mesh's Z-extent IS the cell-to-cell spacing (length of one road segment)
		// and the natural base-mask is (N|S). The user-set Straight Yaw Offset rotates this
		// for meshes authored along a different axis - for an odd quarter-turn the connection
		// direction becomes X, so the cell-to-cell spacing switches to the mesh's X-extent.
		// The previous auto-detection picked whichever axis was LONGER as the connection
		// direction, which mis-classified wider-than-long meshes (e.g. a road with curbs/
		// sidewalks that's 12m across and 4m long) and produced the "rotated 90 degrees
		// incorrectly and wrongly spaced according to their rotation" symptom.
		const int32_t turns = NormalizeQuarterTurns(yawQuarterTurns);
		const bool rotatedOntoX = (turns % 2) != 0;
		outSpec.usesXAxis = rotatedOntoX;
		outSpec.length = std::max(rotatedOntoX ? sizeX : sizeZ, 0.01f);
		return true;
	}

	void CollectPrefabRoots(const std::vector<Entity*>& spawnedEntities, std::vector<Entity*>& outRoots)
	{
		std::unordered_set<Entity*> spawnedSet;
		spawnedSet.reserve(spawnedEntities.size());
		for (auto* entity : spawnedEntities)
		{
			if (entity != nullptr)
				spawnedSet.insert(entity);
		}

		for (auto* entity : spawnedEntities)
		{
			if (entity == nullptr)
				continue;

			auto* parent = entity->GetParent();
			if (parent == nullptr || spawnedSet.find(parent) == spawnedSet.end())
				outRoots.push_back(entity);
		}

		if (outRoots.empty() && !spawnedEntities.empty())
			outRoots.push_back(spawnedEntities.front());
	}

	void EnsureMeshPhysics(Entity* entity, const std::shared_ptr<Mesh>& mesh)
	{
		if (entity == nullptr || mesh == nullptr)
			return;

		auto* rigidBody = entity->GetComponent<RigidBody>();
		if (rigidBody == nullptr)
			rigidBody = entity->AddComponent<RigidBody>(IRigidBody::BodyType::Static);

		if (rigidBody == nullptr)
			return;

		rigidBody->RemoveCollider();
		rigidBody->AddBoxCollider(mesh->GetAABB());
	}

	void SpawnMeshCell(Entity* wrapper, const fs::path& assetPath, bool addPhysics)
	{
		if (wrapper == nullptr || wrapper->GetScene() == nullptr)
			return;

		auto mesh = Mesh::Create(assetPath);
		if (mesh == nullptr)
			return;

		// Put the mesh component directly on the wrapper instead of creating a child entity.
		// At several hundred painted cells the previous wrapper+child structure roughly
		// doubled the per-frame CPU work for the same number of rendered instances - the
		// scene still has to iterate every entity for PVS visibility, transform caches and
		// AABB updates even when the child has the same transform as the wrapper. Attaching
		// the StaticMeshComponent directly to the wrapper gives the same visual result with
		// half the entity count.
		auto* staticMesh = wrapper->AddComponent<StaticMeshComponent>();
		if (staticMesh == nullptr)
			return;

		staticMesh->SetMesh(mesh);
		if (addPhysics)
		{
			EnsureMeshPhysics(wrapper, mesh);
		}
		wrapper->GetScene()->FlushPVS(wrapper);
	}

	// Recursively overrides the material on every StaticMeshComponent under `entity`. Used
	// to tint preview-only entities blue so the player can distinguish the ghost from the
	// real committed road meshes. The override material has hasTransparency=1 + Transparency
	// blend state so the preview goes through the engine's transparent-render path rather
	// than the gbuffer/SSR pipeline.
	void OverridePreviewMaterialRecursive(Entity* entity, const std::shared_ptr<Material>& previewMaterial)
	{
		if (entity == nullptr || previewMaterial == nullptr)
			return;

		if (auto* smc = entity->GetComponent<StaticMeshComponent>(); smc != nullptr)
		{
			smc->SetMaterial(previewMaterial);
		}

		for (auto* child : entity->GetChildren())
		{
			OverridePreviewMaterialRecursive(child, previewMaterial);
		}
	}

	// Records `entity` for explicit teardown by the preview destructor. Why this exists:
	// Entity::DeleteMe(parent) empties parent->_children and detaches each immediate child
	// (parent=nullptr, IsPendingRemoval=true), but does NOT touch the grandchildren - their
	// _children list survives intact. Scene::DestroyEntity(parent) then reads an empty
	// child list and never recurses, so the immediate children are orphaned+flagged but
	// never Scene-removed -> they keep rendering through their last PVS snapshot.
	//
	// We do NOT recurse into descendants here. Scene::DestroyEntity, when called later on
	// an already-flagged entity (e.g. one of these orphaned immediate children), takes the
	// "skip DeleteMe" path and DOES then walk the entity's preserved _children list and
	// destroy each recursively (because those grandchildren are unflagged and follow the
	// normal cascade). Tracking grandchildren explicitly would cause them to be destroyed
	// twice - once via the cascade, then again from the outer destruction loop on a
	// dangling pointer.
	void TrackPreviewEntity(Entity* entity, std::vector<Entity*>& outEntities)
	{
		if (entity == nullptr)
			return;

		outEntities.push_back(entity);
	}

	void SpawnPreviewMeshCell(Entity* wrapper, const fs::path& assetPath, const std::shared_ptr<Material>& previewMaterial, std::vector<Entity*>& outSpawnedEntities)
	{
		if (wrapper == nullptr || wrapper->GetScene() == nullptr)
			return;

		auto mesh = Mesh::Create(assetPath);
		if (mesh == nullptr)
			return;

		auto* scene = wrapper->GetScene();
		auto* child = scene->CreateEntity(std::format("{}_Mesh", wrapper->GetName()), wrapper->GetPosition(), math::Quaternion::Identity, math::Vector3(1.0f));
		if (child == nullptr)
			return;

		child->SetParent(wrapper);
		auto* staticMesh = child->AddComponent<StaticMeshComponent>();
		if (staticMesh == nullptr)
			return;

		staticMesh->SetMesh(mesh);
		// Preview-only: no RigidBody/collider, otherwise the editor's pick-ray would hit the
		// ghost and the painter would anchor onto its own preview. Material override after
		// SetMesh because SetMesh assigns the mesh's authored material first.
		staticMesh->SetMaterial(previewMaterial);
		scene->FlushPVS(child);

		// Track the immediate child explicitly so the destructor can call
		// Scene::DestroyEntity on it after wrapper's DeleteMe has orphaned it (mesh has no
		// further children, so no risk of double-destroy through cascade).
		TrackPreviewEntity(child, outSpawnedEntities);
	}

	void SpawnPrefabCell(Entity* wrapper, const fs::path& assetPath)
	{
		if (wrapper == nullptr || wrapper->GetScene() == nullptr)
			return;

		auto spawnedEntities = g_pEnv->_prefabLoader->LoadPrefab(HexEngine::g_pEnv->_sceneManager->GetCurrentScene(), assetPath);
		if (spawnedEntities.empty())
			return;

		std::vector<Entity*> roots;
		CollectPrefabRoots(spawnedEntities, roots);
		if (roots.empty())
			return;

		const math::Vector3 anchorPosition = roots.front()->GetPosition();
		const math::Quaternion wrapperRotation = wrapper->GetRotation();
		for (auto* root : roots)
		{
			if (root == nullptr)
				continue;

			const math::Vector3 authoredOffset = root->GetPosition() - anchorPosition;
			const math::Vector3 worldOffset = math::Vector3::Transform(authoredOffset, wrapperRotation);
			root->ForcePosition(wrapper->GetPosition() + worldOffset);
			root->SetParent(wrapper);
			root->SetCastsShadows(false);
		}
	}

	void SpawnPreviewPrefabCell(Entity* wrapper, const fs::path& assetPath, const std::shared_ptr<Material>& previewMaterial, std::vector<Entity*>& outSpawnedEntities)
	{
		if (wrapper == nullptr || wrapper->GetScene() == nullptr)
			return;

		auto spawnedEntities = g_pEnv->_prefabLoader->LoadPrefab(HexEngine::g_pEnv->_sceneManager->GetCurrentScene(), assetPath);
		if (spawnedEntities.empty())
			return;

		std::vector<Entity*> roots;
		CollectPrefabRoots(spawnedEntities, roots);
		if (roots.empty())
			return;

		const math::Vector3 anchorPosition = roots.front()->GetPosition();
		const math::Quaternion wrapperRotation = wrapper->GetRotation();
		for (auto* root : roots)
		{
			if (root == nullptr)
				continue;

			const math::Vector3 authoredOffset = root->GetPosition() - anchorPosition;
			const math::Vector3 worldOffset = math::Vector3::Transform(authoredOffset, wrapperRotation);
			root->ForcePosition(wrapper->GetPosition() + worldOffset);
			root->SetParent(wrapper);
			// Recurse into the spawned tree because prefabs typically nest StaticMeshComponents
			// under intermediate transforms; we have to retint every mesh, not just the root.
			OverridePreviewMaterialRecursive(root, previewMaterial);
			wrapper->GetScene()->FlushPVS(root);

			// Track each immediate prefab root - NOT its descendants. When the destructor
			// later calls Scene::DestroyEntity on this root, it will be flagged-but-with-
			// intact-children (because wrapper's DeleteMe only touched its immediate kids),
			// and Scene::DestroyEntity's already-flagged path correctly recurses through
			// the root's preserved _children list to destroy the prefab subtree.
			TrackPreviewEntity(root, outSpawnedEntities);
		}
	}

	bool TryChoosePlacement(
		uint8_t desiredMask,
		const PlacementSpec& straightSpec,
		const std::optional<PlacementSpec>& cornerSpec,
		const std::optional<PlacementSpec>& crossroadSpec,
		const std::optional<PlacementSpec>& tJunctionSpec,
		int32_t yawQuarterTurns,
		// Per-piece authoring-orientation corrections. Applied to the rotation AFTER
		// ComputeRotationForMask, so the user can rotate a mis-authored corner / T /
		// crossroad mesh into the engine's expected base-mask convention without
		// re-exporting the asset. Defaults of 0 give back the previous behaviour.
		int32_t cornerYawQuarterTurns,
		int32_t tJunctionYawQuarterTurns,
		int32_t crossroadYawQuarterTurns,
		CellPlacement& outPlacement)
	{
		const int32_t connections = CountConnections(desiredMask);
		const uint8_t straightBaseMask = straightSpec.usesXAxis ? (DirectionEast | DirectionWest) : (DirectionNorth | DirectionSouth);
		const uint8_t cornerBaseMask = DirectionNorth | DirectionEast;
		const uint8_t tBaseMask = DirectionNorth | DirectionEast | DirectionWest;

		outPlacement = {};

		// Corner / T / crossroad base masks are expressed in WORLD-space directions
		// (cornerBaseMask = N|E, tBaseMask = N|E|W). ComputeRotationForMask therefore
		// already produces the correct world-space rotation that maps the authored
		// connection layout onto the cell's desired mask - no additional yaw turn
		// belongs here.
		//
		// The straight path applies its yaw via PlacementSpec::usesXAxis (which flips
		// straightBaseMask from N|S to E|W on odd quarter turns) and skips the post-
		// rotation for the same reason; previously corner/T/crossroad called
		// ApplyQuarterTurnRotation after ComputeRotationForMask which DOUBLE-applied
		// the offset and was the source of "corners face the wrong direction with yaw
		// 90 set". Removing it makes these placements behave consistently across all
		// yaw values - the user-visible result depends only on how their corner / T /
		// crossroad assets are authored.

		if (connections >= 4 && crossroadSpec.has_value())
		{
			outPlacement.assetPath = crossroadSpec->assetPath;
			outPlacement.isPrefab = crossroadSpec->isPrefab;
			outPlacement.aabbCenterXZ = crossroadSpec->aabbCenterXZ;
			// Crossroads are 4-fold rotationally symmetric, so the rotation is just
			// identity - whatever yaw the user picked, a crossroad still looks like a
			// crossroad. The authoring offset is still applied because some custom
			// crossroad assets carry directional markings (lane arrows, signage) that
			// the user may want to rotate.
			outPlacement.rotation = math::Quaternion::Identity;
			ApplyQuarterTurnRotation(outPlacement.rotation, crossroadYawQuarterTurns);
			return true;
		}

		if (connections == 3 && tJunctionSpec.has_value())
		{
			outPlacement.assetPath = tJunctionSpec->assetPath;
			outPlacement.isPrefab = tJunctionSpec->isPrefab;
			outPlacement.aabbCenterXZ = tJunctionSpec->aabbCenterXZ;
			if (!ComputeRotationForMask(desiredMask, tBaseMask, outPlacement.rotation))
				outPlacement.rotation = math::Quaternion::Identity;
			ApplyQuarterTurnRotation(outPlacement.rotation, tJunctionYawQuarterTurns);
			return true;
		}

		if (connections == 2 && !IsOpposingStraight(desiredMask) && cornerSpec.has_value())
		{
			outPlacement.assetPath = cornerSpec->assetPath;
			outPlacement.isPrefab = cornerSpec->isPrefab;
			outPlacement.aabbCenterXZ = cornerSpec->aabbCenterXZ;
			if (!ComputeRotationForMask(desiredMask, cornerBaseMask, outPlacement.rotation))
				outPlacement.rotation = math::Quaternion::Identity;
			ApplyQuarterTurnRotation(outPlacement.rotation, cornerYawQuarterTurns);
			return true;
		}

		outPlacement.assetPath = straightSpec.assetPath;
		outPlacement.isPrefab = straightSpec.isPrefab;
		outPlacement.aabbCenterXZ = straightSpec.aabbCenterXZ;

		uint8_t straightMask = desiredMask;
		if (connections == 0)
			straightMask = straightBaseMask;
		else if (connections == 1)
			straightMask = ((desiredMask & (DirectionEast | DirectionWest)) != 0) ? (DirectionEast | DirectionWest) : (DirectionNorth | DirectionSouth);
		else if (connections >= 3)
			straightMask = ((desiredMask & (DirectionEast | DirectionWest)) != 0) ? (DirectionEast | DirectionWest) : (DirectionNorth | DirectionSouth);
		else if (!IsOpposingStraight(desiredMask))
			straightMask = ((desiredMask & (DirectionEast | DirectionWest)) != 0) ? (DirectionEast | DirectionWest) : (DirectionNorth | DirectionSouth);

		if (!ComputeRotationForMask(straightMask, straightBaseMask, outPlacement.rotation))
			outPlacement.rotation = math::Quaternion::Identity;
		// Intentionally NOT calling ApplyQuarterTurnRotation here. For straights, the user's
		// yawQuarterTurns is already baked into straightBaseMask via PlacementSpec::usesXAxis
		// (set in TryBuildPlacementSpec) and into the cell-size via PlacementSpec::length.
		// ComputeRotationForMask therefore already produces the correct rotation for the
		// desired connection direction with the yaw-adjusted base mask, and adding another
		// yawQuarterTurns rotation here would double-apply the offset and place the mesh
		// 90 degrees off (visible as a row of perpendicular/edge-on segments).
		return true;
	}

	// Returns the existing RoadNetwork root if one is present in the scene, or nullptr.
	// Used by the section-organisation pass and the "is this a managed cell" check
	// (a CityRoadDraw_ wrapper that doesn't sit under a RoadNetwork ancestor is probably
	// a stray from before the hierarchy work landed - we still pick it up by name, but
	// new commits re-parent it into the network).
	Entity* FindRoadNetworkRoot(Scene* scene)
	{
		if (scene == nullptr)
			return nullptr;

		Entity* existing = scene->GetEntityByName(kRoadNetworkRootName);
		if (existing == nullptr || existing->IsPendingDeletion())
			return nullptr;
		return existing;
	}

	// Lazy-creates the singleton RoadNetwork root entity. Created at origin with identity
	// rotation so road wrappers' world positions equal their local positions out of the
	// box - the user can later move the whole network as a single entity and all road
	// children follow via the usual parent-child transform inheritance.
	Entity* GetOrCreateRoadNetworkRoot(Scene* scene)
	{
		if (scene == nullptr)
			return nullptr;

		if (auto* existing = FindRoadNetworkRoot(scene); existing != nullptr)
			return existing;

		return scene->CreateEntity(kRoadNetworkRootName, math::Vector3::Zero, math::Quaternion::Identity, math::Vector3(1.0f));
	}

	// Pool of presentable street names the auto-naming code cycles through. When the
	// pool is exhausted, AllocateStreetName falls back to "<base> 2", "<base> 3", ...
	const std::vector<std::string>& StreetNamePool()
	{
		static const std::vector<std::string> pool = {
			"Main Street", "Oak Avenue", "Elm Street", "Maple Lane", "Park Road",
			"River Way", "Birch Boulevard", "Cedar Crescent", "Pine Way", "High Street",
			"Church Road", "Mill Lane", "Bridge Street", "Spring Avenue", "Sunset Boulevard",
			"Vine Street", "Willow Way", "Cherry Lane", "Walnut Drive", "Greenway Drive",
			"Linden Avenue", "Hawthorn Road", "Juniper Lane", "Magnolia Way", "Sycamore Street",
			"Ashford Lane", "Brookside Drive", "Castle Road", "Dogwood Avenue", "Forest Way"
		};
		return pool;
	}

	// Picks a street name not currently in use. Falls back to numeric-suffixed variants
	// of the base pool when every base name is taken, so the assignment never blocks
	// even on enormous networks.
	std::string AllocateStreetName(const std::unordered_set<std::string>& usedNames)
	{
		const auto& pool = StreetNamePool();
		for (const auto& name : pool)
		{
			if (usedNames.count(name) == 0)
				return name;
		}
		for (int32_t suffix = 2; suffix < 10000; ++suffix)
		{
			for (const auto& name : pool)
			{
				std::string candidate = name + " " + std::to_string(suffix);
				if (usedNames.count(candidate) == 0)
					return candidate;
			}
		}
		return "Unnamed Street";
	}

	// Returns true if `entity` is part of the RoadNetwork hierarchy - either parented
	// directly under it or under one of its section children. Used to filter out stray
	// CityRoadDraw_ wrappers that might exist outside the network (e.g. left over from
	// a partially-deleted older network).
	bool IsUnderRoadNetwork(Entity* entity, Entity* networkRoot)
	{
		if (entity == nullptr || networkRoot == nullptr)
			return false;
		Entity* current = entity->GetParent();
		while (current != nullptr)
		{
			if (current == networkRoot)
				return true;
			current = current->GetParent();
		}
		return false;
	}

	// Re-parents every managed road wrapper into the RoadNetwork hierarchy, grouping
	// continuous runs of straight cells under named section entities and parking
	// corners / T-junctions / crossroads directly under the root. Called as a post-pass
	// on every commit so the entity tree stays organised even as the network grows.
	//
	// Section identity is recovered across edits by reading each cell's current section
	// parent name and letting the new section inherit whichever name has the most cells
	// in it - so extending a straight keeps its name, and splitting a straight in two
	// (a corner placed in the middle) preserves the name on whichever half is bigger.
	// Truly new sections allocate a fresh street name from the pool. Empty section
	// entities left behind after a split / shrink are destroyed.
	void OrganizeRoadNetworkHierarchy(Scene* scene, bool defaultUsesXAxis)
	{
		if (scene == nullptr)
			return;

		// 1. Collect every managed road wrapper currently in the scene.
		std::unordered_map<GridCoord, Entity*, GridCoordHash> wrappersByCoord;
		for (const auto& bySignature : scene->GetEntities())
		{
			for (auto* entity : bySignature.second)
			{
				if (entity == nullptr || entity->IsPendingDeletion())
					continue;
				GridCoord coord;
				if (!TryParseManagedWrapperName(entity->GetName(), coord))
					continue;
				wrappersByCoord[coord] = entity;
			}
		}

		if (wrappersByCoord.empty())
		{
			// Nothing to organise; if a RoadNetwork root exists and is empty, leave it
			// alone - the user might be about to draw more.
			return;
		}

		Entity* networkRoot = GetOrCreateRoadNetworkRoot(scene);
		if (networkRoot == nullptr)
			return;

		auto hasCell = [&](const GridCoord& c) { return wrappersByCoord.count(c) > 0; };
		auto computeMask = [&](const GridCoord& c) -> uint8_t
		{
			uint8_t mask = DirectionNone;
			if (hasCell({ c.x, c.z + 1 })) mask |= DirectionNorth;
			if (hasCell({ c.x + 1, c.z })) mask |= DirectionEast;
			if (hasCell({ c.x, c.z - 1 })) mask |= DirectionSouth;
			if (hasCell({ c.x - 1, c.z })) mask |= DirectionWest;
			return mask;
		};

		// 2. Classify each cell as a straight (with axis) or a junction. Junctions don't
		//    get grouped into named sections - they sit at network-root level. Straights
		//    are grouped by walking continuous runs along their axis.
		enum class CellKind { StraightX, StraightZ, Junction };
		std::unordered_map<GridCoord, CellKind, GridCoordHash> cellKinds;
		for (const auto& [coord, wrapper] : wrappersByCoord)
		{
			const uint8_t mask = computeMask(coord);
			const int32_t conns = CountConnections(mask);
			const bool hasXMask = (mask & (DirectionEast | DirectionWest)) != 0;
			const bool hasZMask = (mask & (DirectionNorth | DirectionSouth)) != 0;

			CellKind kind;
			if (conns >= 3)
				kind = CellKind::Junction;
			else if (conns == 2 && !IsOpposingStraight(mask))
				kind = CellKind::Junction;
			else if (conns == 0)
				kind = defaultUsesXAxis ? CellKind::StraightX : CellKind::StraightZ;
			else if (hasXMask)
				kind = CellKind::StraightX;
			else if (hasZMask)
				kind = CellKind::StraightZ;
			else
				kind = CellKind::Junction; // defensive

			cellKinds[coord] = kind;
		}

		// 3. Walk the straights to build sections. Each section is a maximal run of
		//    straights of the same kind along the same axis line (same Z for X-sections,
		//    same X for Z-sections).
		struct Section
		{
			std::vector<GridCoord> cells;
			bool isX = true;
			std::string assignedName;
		};
		std::vector<Section> sections;
		std::unordered_set<GridCoord, GridCoordHash> visited;

		auto walkSection = [&](const GridCoord& seed, CellKind kind) -> Section
		{
			Section section;
			section.isX = (kind == CellKind::StraightX);
			// Walk in the negative direction first.
			if (section.isX)
			{
				int32_t x = seed.x;
				while (true)
				{
					GridCoord c{ x, seed.z };
					auto it = cellKinds.find(c);
					if (it == cellKinds.end() || it->second != CellKind::StraightX) break;
					if (visited.count(c)) break;
					section.cells.push_back(c);
					visited.insert(c);
					--x;
				}
				x = seed.x + 1;
				while (true)
				{
					GridCoord c{ x, seed.z };
					auto it = cellKinds.find(c);
					if (it == cellKinds.end() || it->second != CellKind::StraightX) break;
					if (visited.count(c)) break;
					section.cells.push_back(c);
					visited.insert(c);
					++x;
				}
			}
			else
			{
				int32_t z = seed.z;
				while (true)
				{
					GridCoord c{ seed.x, z };
					auto it = cellKinds.find(c);
					if (it == cellKinds.end() || it->second != CellKind::StraightZ) break;
					if (visited.count(c)) break;
					section.cells.push_back(c);
					visited.insert(c);
					--z;
				}
				z = seed.z + 1;
				while (true)
				{
					GridCoord c{ seed.x, z };
					auto it = cellKinds.find(c);
					if (it == cellKinds.end() || it->second != CellKind::StraightZ) break;
					if (visited.count(c)) break;
					section.cells.push_back(c);
					visited.insert(c);
					++z;
				}
			}
			return section;
		};

		for (const auto& [coord, kind] : cellKinds)
		{
			if (kind == CellKind::Junction) continue;
			if (visited.count(coord)) continue;
			sections.push_back(walkSection(coord, kind));
		}

		// 4. Recover stable section names. For each new section, count votes for each
		//    distinct existing parent-section name among its member cells. The name with
		//    the most votes wins (ties go to the first-seen name, which is fine for our
		//    purposes - both halves of a split would prefer the original name and only
		//    one can keep it). Sections with no votes get a fresh name from the pool.
		auto cellSectionName = [&](const GridCoord& coord) -> std::string
		{
			auto wIt = wrappersByCoord.find(coord);
			if (wIt == wrappersByCoord.end()) return "";
			Entity* wrapper = wIt->second;
			Entity* parent = wrapper->GetParent();
			if (parent == nullptr || parent == networkRoot) return "";
			// A valid section parent is a child of networkRoot whose name isn't itself a
			// managed-wrapper name.
			if (parent->GetParent() != networkRoot) return "";
			GridCoord dummy;
			if (TryParseManagedWrapperName(parent->GetName(), dummy)) return "";
			return parent->GetName();
		};

		std::unordered_set<std::string> usedNames;
		// Names already in use by sections we'll keep - seeded as we assign.
		for (auto& section : sections)
		{
			std::unordered_map<std::string, int32_t> votes;
			for (const auto& cell : section.cells)
			{
				std::string existing = cellSectionName(cell);
				if (!existing.empty())
					++votes[existing];
			}
			std::string chosen;
			int32_t best = 0;
			for (const auto& [name, count] : votes)
			{
				if (count > best && usedNames.count(name) == 0)
				{
					best = count;
					chosen = name;
				}
			}
			if (chosen.empty())
				chosen = AllocateStreetName(usedNames);
			usedNames.insert(chosen);
			section.assignedName = chosen;
		}

		// 5. Apply parenting. Section entities are looked up by name (since they were
		//    previously created or are being created now). Wrapper world positions are
		//    preserved by SetParent's local-position recompute.
		for (auto& section : sections)
		{
			Entity* sectionEntity = scene->GetEntityByName(section.assignedName);
			if (sectionEntity != nullptr && sectionEntity->IsPendingDeletion())
				sectionEntity = nullptr;
			if (sectionEntity == nullptr)
			{
				sectionEntity = scene->CreateEntity(section.assignedName, math::Vector3::Zero, math::Quaternion::Identity, math::Vector3(1.0f));
				if (sectionEntity == nullptr)
					continue;
				sectionEntity->SetParent(networkRoot);
			}
			else if (sectionEntity->GetParent() != networkRoot)
			{
				sectionEntity->SetParent(networkRoot);
			}

			for (const auto& cell : section.cells)
			{
				auto wIt = wrappersByCoord.find(cell);
				if (wIt == wrappersByCoord.end()) continue;
				if (wIt->second->GetParent() != sectionEntity)
					wIt->second->SetParent(sectionEntity);
			}
		}

		// 6. Junctions live directly under the network root.
		for (const auto& [coord, kind] : cellKinds)
		{
			if (kind != CellKind::Junction) continue;
			auto wIt = wrappersByCoord.find(coord);
			if (wIt == wrappersByCoord.end()) continue;
			if (wIt->second->GetParent() != networkRoot)
				wIt->second->SetParent(networkRoot);
		}

		// 7. Clean up empty section entities that no longer have any road children
		//    (happens after a split or after a section was completely overwritten). We
		//    do this in a snapshot pass because DestroyEntity mutates the children list.
		std::vector<Entity*> rootChildren = networkRoot->GetChildren();
		for (auto* child : rootChildren)
		{
			if (child == nullptr || child->IsPendingDeletion()) continue;
			// Skip cells - they're junctions parked here.
			GridCoord dummy;
			if (TryParseManagedWrapperName(child->GetName(), dummy)) continue;
			// Section entity: destroy if empty.
			if (child->GetChildren().empty())
				scene->DestroyEntity(child);
		}
	}

	void GatherManagedRoadCells(
		Scene* scene,
		std::unordered_map<GridCoord, float, GridCoordHash>& outHeights,
		std::vector<Entity*>& outWrappers)
	{
		if (scene == nullptr)
			return;

		Entity* networkRoot = FindRoadNetworkRoot(scene);

		for (const auto& bySignature : scene->GetEntities())
		{
			for (auto* entity : bySignature.second)
			{
				if (entity == nullptr || entity->IsPendingDeletion())
					continue;

				GridCoord coord;
				if (!TryParseManagedWrapperName(entity->GetName(), coord))
					continue;

				// Accept road cells if they're top-level (legacy / not-yet-organised)
				// or anywhere under the RoadNetwork hierarchy (the normal case after
				// OrganizeRoadNetworkHierarchy has run). Reject cells whose parent chain
				// goes elsewhere - those are probably children of unrelated prefabs and
				// shouldn't be picked up by the painter's rebuild.
				Entity* parent = entity->GetParent();
				if (parent != nullptr && !IsUnderRoadNetwork(entity, networkRoot))
					continue;

				outHeights[coord] = entity->GetPosition().y;
				outWrappers.push_back(entity);
			}
		}
	}

	void RebuildManagedRoadNetwork(
		Scene* scene,
		const std::unordered_map<GridCoord, float, GridCoordHash>& heights,
		const math::Vector2& origin,
		const PlacementSpec& straightSpec,
		const std::optional<PlacementSpec>& cornerSpec,
		const std::optional<PlacementSpec>& crossroadSpec,
		const std::optional<PlacementSpec>& tJunctionSpec,
		int32_t yawQuarterTurns,
		int32_t cornerYawQuarterTurns,
		int32_t tJunctionYawQuarterTurns,
		int32_t crossroadYawQuarterTurns,
		float cellSpacing,
		bool addPhysics)
	{
		if (scene == nullptr)
			return;

		std::vector<Entity*> wrappersToDelete;
		std::unordered_map<GridCoord, float, GridCoordHash> ignoredHeights;
		GatherManagedRoadCells(scene, ignoredHeights, wrappersToDelete);
		for (auto* wrapper : wrappersToDelete)
		{
			DestroyWrapperAndDescendants(scene, wrapper);
		}

		auto hasCell = [&heights](const GridCoord& coord) -> bool
		{
			return heights.find(coord) != heights.end();
		};

		const float spacing = (cellSpacing > 0.0f) ? cellSpacing : straightSpec.length;
		for (const auto& [coord, height] : heights)
		{
			uint8_t mask = DirectionNone;
			if (hasCell({ coord.x, coord.z + 1 })) mask |= DirectionNorth;
			if (hasCell({ coord.x + 1, coord.z })) mask |= DirectionEast;
			if (hasCell({ coord.x, coord.z - 1 })) mask |= DirectionSouth;
			if (hasCell({ coord.x - 1, coord.z })) mask |= DirectionWest;

			CellPlacement placement;
			if (!TryChoosePlacement(mask, straightSpec, cornerSpec, crossroadSpec, tJunctionSpec, yawQuarterTurns, cornerYawQuarterTurns, tJunctionYawQuarterTurns, crossroadYawQuarterTurns, placement))
				continue;

			const math::Vector3 wrapperWorld = ApplyMeshCentreCorrection(GridToWorld(coord, origin, spacing, height), placement.aabbCenterXZ, placement.rotation);
			auto* wrapper = scene->CreateEntity(MakeManagedWrapperName(coord), wrapperWorld, placement.rotation, math::Vector3(1.0f));
			if (wrapper == nullptr)
				continue;

			wrapper->SetCastsShadows(false);

			if (placement.isPrefab)
				SpawnPrefabCell(wrapper, placement.assetPath);
			else
				SpawnMeshCell(wrapper, placement.assetPath, addPhysics);
		}

		// Re-organise so every wrapper lands under the RoadNetwork hierarchy and straights
		// get grouped into named section entities. Has to run AFTER spawn so the new
		// wrappers exist and can be picked up.
		OrganizeRoadNetworkHierarchy(scene, straightSpec.usesXAxis);

		scene->ForceRebuildPVS();
	}

	// Returns the managed-road wrapper at `coord` if one exists in the scene, else nullptr.
	// O(log N) via Scene::GetEntityByName, which is backed by an std::map<string, Entity*>.
	// Replaces O(scene) full-scan lookups (GatherManagedRoadCells) for the hot-path
	// per-coord checks that the incremental paint flow needs.
	Entity* FindManagedRoadCell(Scene* scene, const GridCoord& coord)
	{
		if (scene == nullptr)
			return nullptr;

		Entity* entity = scene->GetEntityByName(MakeManagedWrapperName(coord));
		if (entity == nullptr || entity->IsPendingDeletion())
			return nullptr;

		// Accept wrappers that are top-level (legacy layouts predating the RoadNetwork
		// hierarchy) or anywhere under the RoadNetwork root (the normal post-organise
		// state). Reject wrappers parented elsewhere - those are not part of the network
		// we manage. Mirrors GatherManagedRoadCells.
		Entity* parent = entity->GetParent();
		if (parent != nullptr)
		{
			Entity* networkRoot = FindRoadNetworkRoot(scene);
			if (!IsUnderRoadNetwork(entity, networkRoot))
				return nullptr;
		}

		return entity;
	}

	// Incremental version of RebuildManagedRoadNetwork. Used by PaintOrthogonalRun and any
	// other path that adds a small set of new cells onto an existing managed network.
	//
	// Cost: O(|runCells|) scene lookups and at most |runCells| + 4*|runCells| entity
	// respawns - independent of the total network size. Compare with the full rebuild,
	// which destroys and respawns every existing cell every commit.
	//
	// Algorithm:
	//   1. Touched cells = runCells (cells the new run wants placed) plus every existing
	//      managed neighbour of a runCell. These are the only cells whose direction mask
	//      can change as a result of this commit.
	//   2. For each touched cell, compute the post-commit mask (would-be-existing after
	//      this run) and, if it already exists, the pre-commit mask. If the masks match
	//      then the cell's placement is unchanged and we skip it entirely (no destroy,
	//      no respawn) - this is the optimisation that makes overdrawing the same line
	//      essentially free.
	//   3. Stage destroy + respawn decisions before mutating the scene, so the lookups in
	//      step 2 see a stable pre-commit world. Then commit them in a single pass.
	void ApplyIncrementalRoadNetworkChange(
		Scene* scene,
		const std::vector<GridCoord>& runCells,
		const math::Vector2& origin,
		float anchorHeight,
		const PlacementSpec& straightSpec,
		const std::optional<PlacementSpec>& cornerSpec,
		const std::optional<PlacementSpec>& crossroadSpec,
		const std::optional<PlacementSpec>& tJunctionSpec,
		int32_t yawQuarterTurns,
		int32_t cornerYawQuarterTurns,
		int32_t tJunctionYawQuarterTurns,
		int32_t crossroadYawQuarterTurns,
		float cellSpacing,
		bool addPhysics)
	{
		if (scene == nullptr)
			return;

		// Match RebuildManagedRoadNetwork's spacing fallback: 0 means "use the raw mesh
		// length" so callers that haven't computed a scaled cell size still work.
		const float spacing = (cellSpacing > 0.0f) ? cellSpacing : straightSpec.length;

		// Fast-membership set for cells in this run.
		std::unordered_set<GridCoord, GridCoordHash> runSet;
		runSet.reserve(runCells.size());
		for (const auto& c : runCells)
			runSet.insert(c);

		// Pre-commit existence: does a CityRoadDraw_ wrapper exist at coord right now?
		// Cached so we don't repeat name lookups during mask computation.
		std::unordered_map<GridCoord, Entity*, GridCoordHash> existingByCoord;
		auto existingBefore = [&](const GridCoord& c) -> Entity*
		{
			auto it = existingByCoord.find(c);
			if (it != existingByCoord.end())
				return it->second;
			Entity* e = FindManagedRoadCell(scene, c);
			existingByCoord.emplace(c, e);
			return e;
		};

		auto existsBefore = [&](const GridCoord& c) { return existingBefore(c) != nullptr; };
		auto existsAfter  = [&](const GridCoord& c) { return existsBefore(c) || runSet.count(c) > 0; };

		auto computeMask = [](const auto& exists, const GridCoord& c) -> uint8_t
		{
			uint8_t mask = DirectionNone;
			if (exists({ c.x, c.z + 1 })) mask |= DirectionNorth;
			if (exists({ c.x + 1, c.z })) mask |= DirectionEast;
			if (exists({ c.x, c.z - 1 })) mask |= DirectionSouth;
			if (exists({ c.x - 1, c.z })) mask |= DirectionWest;
			return mask;
		};

		// Touched cells: run cells + run cells' existing-managed neighbours. Bounded by
		// 5 * |runCells|, so the work scales with the run, not the network.
		std::unordered_set<GridCoord, GridCoordHash> touched;
		touched.reserve(runCells.size() * 5);
		for (const auto& c : runCells)
		{
			touched.insert(c);
			const GridCoord neighbours[4] = {
				{ c.x, c.z + 1 }, { c.x + 1, c.z }, { c.x, c.z - 1 }, { c.x - 1, c.z }
			};
			for (const auto& n : neighbours)
			{
				if (existsBefore(n))
					touched.insert(n);
			}
		}

		struct CellAction
		{
			GridCoord coord;
			Entity* existingWrapper;  // nullptr when we're adding a brand-new cell
			CellPlacement placement;
			float height;
		};

		std::vector<CellAction> actions;
		actions.reserve(touched.size());

		// Phase 1: decide what changes. No scene mutation here, so existsBefore lookups
		// remain consistent across the whole loop.
		for (const auto& coord : touched)
		{
			Entity* existing = existingBefore(coord);
			const uint8_t newMask = computeMask(existsAfter, coord);

			if (existing != nullptr)
			{
				const uint8_t oldMask = computeMask(existsBefore, coord);
				if (oldMask == newMask)
				{
					// Mask unchanged -> placement chooser would give the same asset+rotation
					// it produced last time. Skip the destroy+respawn entirely; this is the
					// hot path when the new run extends a straight line and most neighbours
					// see no topology change.
					continue;
				}
			}

			CellPlacement placement;
			if (!TryChoosePlacement(newMask, straightSpec, cornerSpec, crossroadSpec, tJunctionSpec, yawQuarterTurns, cornerYawQuarterTurns, tJunctionYawQuarterTurns, crossroadYawQuarterTurns, placement))
				continue;

			// Re-use the existing wrapper's height when reshaping an already-placed cell;
			// only brand-new cells take the anchor height of this run.
			const float height = (existing != nullptr) ? existing->GetPosition().y : anchorHeight;

			actions.push_back({ coord, existing, placement, height });
		}

		// Phase 2: apply. Now that all decisions are made, mutating the scene won't
		// corrupt later lookups.
		//
		// For UPGRADES (existing wrapper at this coord), we re-use the wrapper entity
		// rather than destroy+CreateEntity-with-same-name. Reason: Scene::CreateEntity
		// resolves duplicate names by either (a) returning the existing entity if the
		// naming policy isn't AutoRename, or (b) auto-renaming the new entity with a
		// numeric suffix. Either branch leaves the OLD wrapper alive and rendering its
		// stale subtree on top of whatever the new wrapper draws - which is the visible
		// symptom of "corner cell retains its straight piece". The dup-name issue is
		// timing-dependent: Scene::DestroyEntity defers the actual removal when called
		// during entity iteration (`_insideEntityIteration`), so even though we called
		// destroy, the old wrapper is still in `_entNameMap` when CreateEntity hits.
		//
		// By re-using the wrapper we never reach that race: we just strip its existing
		// children/components via ClearWrapperContent, update its transform, and respawn
		// content onto the same entity. ForcePosition + SetRotation are immediate (no
		// scene-level book-keeping) so this is atomic from the scene's point of view.
		for (const auto& action : actions)
		{
			Entity* wrapper = nullptr;
			const math::Vector3 wrapperWorld = ApplyMeshCentreCorrection(GridToWorld(action.coord, origin, spacing, action.height), action.placement.aabbCenterXZ, action.placement.rotation);

			if (action.existingWrapper != nullptr)
			{
				wrapper = action.existingWrapper;
				ClearWrapperContent(scene, wrapper);
				// ForcePosition writes the LOCAL position. Detach from any current
				// section parent first so the value we pass (world coords from
				// ApplyMeshCentreCorrection / GridToWorld) is interpreted correctly -
				// otherwise the wrapper would land at parent_world + wrapperWorld
				// once a non-origin section gets in the chain. The post-spawn
				// OrganizeRoadNetworkHierarchy pass re-parents it into the right
				// section, preserving the world position via SetParent's local-recompute.
				if (wrapper->GetParent() != nullptr)
					wrapper->SetParent(nullptr);
				wrapper->ForcePosition(wrapperWorld);
				wrapper->SetRotation(action.placement.rotation);
				wrapper->SetScale(math::Vector3(1.0f));
			}
			else
			{
				wrapper = scene->CreateEntity(MakeManagedWrapperName(action.coord), wrapperWorld, action.placement.rotation, math::Vector3(1.0f));
				if (wrapper == nullptr)
					continue;
			}

			wrapper->SetCastsShadows(false);

			if (action.placement.isPrefab)
				SpawnPrefabCell(wrapper, action.placement.assetPath);
			else
				SpawnMeshCell(wrapper, action.placement.assetPath, addPhysics);

			scene->FlushPVS(wrapper);
		}

		// Re-organise after the new cells exist so they get parented under the
		// RoadNetwork root and the appropriate named section. Sections that are
		// extended keep their previous name (vote-by-overlap); freshly-created sections
		// get a new street name from the pool. Cheap: O(cells) walks + O(cells)
		// reparenting, with no scene-wide work.
		OrganizeRoadNetworkHierarchy(scene, straightSpec.usesXAxis);
	}
}

CitySimulationEditorToolPlugin::CitySimulationEditorToolPlugin(CitySimulationInterface* citySimulationInterface) :
	_citySimulationInterface(citySimulationInterface)
{
}

CitySimulationEditorToolPlugin::~CitySimulationEditorToolPlugin()
{
	DestroyRoadPainterPreviewEntities();
	if (_roadPainterRegisteredScene != nullptr)
	{
		_roadPainterRegisteredScene->UnregisterCustomRenderer(this);
		_roadPainterRegisteredScene = nullptr;
	}
}

std::shared_ptr<HexEngine::Material> CitySimulationEditorToolPlugin::GetOrCreatePreviewMaterial()
{
	if (_roadPainterPreviewMaterial != nullptr)
		return _roadPainterPreviewMaterial;

	// Clone the default material so the preview gets a valid shader + sampler setup, then
	// flip it into the transparency path with a blue diffuse multiplier. Vector4(rgb, a):
	// rgb tints the underlying texture; a controls how see-through the ghost is. Blue >1
	// pushes the tint past the typically dark road texture so the preview reads as blue
	// against the terrain rather than as dark grey.
	auto material = std::make_shared<HexEngine::Material>();
	if (auto defaultMaterial = HexEngine::Material::GetDefaultMaterial(); defaultMaterial != nullptr)
	{
		material->CopyFrom(defaultMaterial);
	}
	material->_properties.diffuseColour = math::Vector4(0.35f, 0.7f, 1.3f, 0.55f);
	material->_properties.hasTransparency = 1;
	material->SetBlendState(HexEngine::BlendState::Transparency);
	// AffectsGI off so the ghost mesh never feeds the voxel-GI clipmaps - otherwise an
	// in-flight preview would smear blue indirect light onto nearby surfaces every time
	// the user hovers a new cell.
	material->SetAffectsGI(false);
	material->SetEmissiveAffectsGI(false);
	material->SetName("CityRoadPreview");

	_roadPainterPreviewMaterial = material;
	return _roadPainterPreviewMaterial;
}

void CitySimulationEditorToolPlugin::DestroyRoadPainterPreviewEntities()
{
	auto* scene = HexEngine::g_pEnv != nullptr && HexEngine::g_pEnv->_sceneManager != nullptr
		? HexEngine::g_pEnv->_sceneManager->GetCurrentScene().get()
		: nullptr;

	if (scene != nullptr)
	{
		// Tear down in FORWARD insertion order: wrappers come before their children, so the
		// wrapper goes first. Entity::DeleteMe(wrapper) snapshots+empties wrapper->_children
		// and detaches each child (_parent = nullptr) but does NOT cascade Scene-level
		// removal into them - those children just get their IsPendingRemoval flag set and
		// keep rendering through the last PVS snapshot. The earlier version of this loop
		// relied on Scene::DestroyEntity recursing into children, which it cannot do
		// because DeleteMe has already emptied the child list by then.
		//
		// Doing this in REVERSE would be a use-after-free: destroying the child first
		// `delete`s it (Scene::RemoveEntityInternal), but wrapper->_children still holds
		// the dangling pointer until DeleteMe(wrapper) tries to dereference it.
		//
		// In forward order, after wrapper is destroyed each child is left flagged-but-alive.
		// Calling Scene::DestroyEntity(child) skips the already-flagged DeleteMe path but
		// still unconditionally runs RemoveEntityInternal (Scene.cpp ~line 1138), which is
		// what actually removes the child from PVS and frees it.
		for (auto* entity : _roadPainterPreviewEntities)
		{
			if (entity == nullptr)
				continue;

			scene->DestroyEntity(entity);
		}
	}

	_roadPainterPreviewEntities.clear();
	_roadPainterHasPreviewKey = false;
}

void CitySimulationEditorToolPlugin::RefreshRoadPainterPreview()
{
	// Drop the anchor requirement: hover alone is enough to draw a single-cell ghost at
	// the cursor before the first click, so the user can line up where the start of the
	// run will land. The "anchor + hover" case (mid-drag, after a first click) still
	// renders the full run, this just lets the no-anchor case render a single tile.
	if (!_roadPainterEnabled || !_roadPainterHasHover)
	{
		DestroyRoadPainterPreviewEntities();
		return;
	}

	// Effective anchor: the painter's anchor if set, else the hover itself so the
	// single-cell preview at the cursor still has a valid start/end pair. Height also
	// picks up _roadPainterInitialYOffset when previewing pre-anchor, since that's what
	// the eventual first click will apply.
	const int32_t effAnchorX = _roadPainterHasAnchor ? _roadPainterAnchorX : _roadPainterHoverX;
	const int32_t effAnchorZ = _roadPainterHasAnchor ? _roadPainterAnchorZ : _roadPainterHoverZ;
	const float effAnchorHeight = _roadPainterHasAnchor
		? _roadPainterAnchorHeight
		: (_roadPainterHoverHeight + _roadPainterInitialYOffset);

	// Bail out cheaply if the (anchor, hover) snapped grid pair is unchanged; the run cells
	// only depend on the snapped pair, not on raw mouse position. This is what keeps mouse-
	// move from rebuilding the preview every pixel of motion.
	if (_roadPainterHasPreviewKey
		&& _roadPainterPreviewAnchorX == effAnchorX
		&& _roadPainterPreviewAnchorZ == effAnchorZ
		&& _roadPainterPreviewHoverX == _roadPainterHoverX
		&& _roadPainterPreviewHoverZ == _roadPainterHoverZ)
	{
		return;
	}

	DestroyRoadPainterPreviewEntities();

	auto* scene = HexEngine::g_pEnv->_sceneManager->GetCurrentScene().get();
	if (scene == nullptr)
		return;

	PlacementSpec straightSpec;
	if (!TryBuildPlacementSpec(_roadPainterStraightPath, _roadPainterYawQuarterTurns, straightSpec))
		return;

	std::optional<PlacementSpec> cornerSpec;
	std::optional<PlacementSpec> crossroadSpec;
	std::optional<PlacementSpec> tJunctionSpec;
	PlacementSpec tempSpec;
	if (TryBuildPlacementSpec(_roadPainterCornerPath, _roadPainterYawQuarterTurns, tempSpec)) cornerSpec = tempSpec;
	if (TryBuildPlacementSpec(_roadPainterCrossroadPath, _roadPainterYawQuarterTurns, tempSpec)) crossroadSpec = tempSpec;
	if (TryBuildPlacementSpec(_roadPainterTJunctionPath, _roadPainterYawQuarterTurns, tempSpec)) tJunctionSpec = tempSpec;

	std::vector<GridCoord> runCells;
	const GridCoord start{ effAnchorX, effAnchorZ };
	const GridCoord end{ _roadPainterHoverX, _roadPainterHoverZ };
	const int32_t deltaX = std::abs(end.x - start.x);
	const int32_t deltaZ = std::abs(end.z - start.z);
	if (deltaX >= deltaZ)
	{
		const int32_t step = end.x >= start.x ? 1 : -1;
		for (int32_t x = start.x;; x += step)
		{
			runCells.push_back({ x, start.z });
			if (x == end.x)
				break;
		}
	}
	else
	{
		const int32_t step = end.z >= start.z ? 1 : -1;
		for (int32_t z = start.z;; z += step)
		{
			runCells.push_back({ start.x, z });
			if (z == end.z)
				break;
		}
	}

	// runSet for fast membership of "is this coord in the new run we're previewing".
	std::unordered_set<GridCoord, GridCoordHash> runSet;
	runSet.reserve(runCells.size());
	for (const auto& c : runCells)
		runSet.insert(c);

	auto previewMaterial = GetOrCreatePreviewMaterial();

	// Per-coord lookup against the committed network. O(log N) per call via
	// Scene::GetEntityByName, with a small memo cache so the typical case of asking
	// "is this neighbour an existing cell?" five times for a 5-cell preview run doesn't
	// hit the name map dozens of times. This replaces the previous O(scene) full-scan
	// (GatherManagedRoadCells) that ran on every hover-cell change as the user dragged.
	std::unordered_map<GridCoord, bool, GridCoordHash> existsCache;
	auto cellExistsInNetwork = [&](const GridCoord& c) -> bool
	{
		auto it = existsCache.find(c);
		if (it != existsCache.end())
			return it->second;
		const bool exists = FindManagedRoadCell(scene, c) != nullptr;
		existsCache.emplace(c, exists);
		return exists;
	};

	// Combined existence: in the run, OR an existing committed cell. This is the "future
	// network state" the preview is trying to visualise.
	auto hasCell = [&](const GridCoord& coord) -> bool
	{
		return runSet.count(coord) > 0 || cellExistsInNetwork(coord);
	};

	for (const auto& coord : runCells)
	{
		// Skip cells that already exist as committed roads - their real (opaque) mesh is
		// already visible and double-spawning a preview on top would just stack a tinted
		// copy in the same place. We do NOT update those committed pieces' shapes here;
		// the commit pass handles that via ApplyIncrementalRoadNetworkChange.
		if (cellExistsInNetwork(coord))
			continue;

		uint8_t mask = DirectionNone;
		if (hasCell({ coord.x, coord.z + 1 })) mask |= DirectionNorth;
		if (hasCell({ coord.x + 1, coord.z })) mask |= DirectionEast;
		if (hasCell({ coord.x, coord.z - 1 })) mask |= DirectionSouth;
		if (hasCell({ coord.x - 1, coord.z })) mask |= DirectionWest;

		CellPlacement placement;
		if (!TryChoosePlacement(mask, straightSpec, cornerSpec, crossroadSpec, tJunctionSpec,
				_roadPainterYawQuarterTurns,
				_roadPainterCornerYawQuarterTurns,
				_roadPainterTJunctionYawQuarterTurns,
				_roadPainterCrossroadYawQuarterTurns,
				placement))
			continue;

		// Use the painter's scaled cell spacing (not raw straightSpec.length) so the preview
		// aligns exactly with where the commit will eventually place the cell - otherwise
		// the ghost would sit at the raw-mesh spacing while the committed road lands on the
		// user's scaled grid, and the preview would drift away from the actual placement.
		const float previewSpacing = (_roadPainterCellSize > 0.0f) ? _roadPainterCellSize : straightSpec.length;
		const math::Vector2 previewOrigin(_roadPainterOriginX, _roadPainterOriginZ);
		const math::Vector3 previewWorld = ApplyMeshCentreCorrection(GridToWorld(coord, previewOrigin, previewSpacing, effAnchorHeight), placement.aabbCenterXZ, placement.rotation);
		const std::string wrapperName = std::format("{}{}_{}", kRoadPainterPreviewPrefix, coord.x, coord.z);
		auto* wrapper = scene->CreateEntity(wrapperName, previewWorld, placement.rotation, math::Vector3(1.0f));
		if (wrapper == nullptr)
			continue;

		// Push the wrapper FIRST so the forward-order teardown in
		// DestroyRoadPainterPreviewEntities destroys it before its immediate children. That
		// order matters: DeleteMe(wrapper) detaches the children (sets _parent=nullptr,
		// flags them) but does NOT scene-remove them, so we have to destroy each child
		// explicitly afterwards. Doing it in reverse would `delete` the child before
		// DeleteMe(wrapper) ran, leaving wrapper->_children with a dangling pointer.
		_roadPainterPreviewEntities.push_back(wrapper);

		if (placement.isPrefab)
			SpawnPreviewPrefabCell(wrapper, placement.assetPath, previewMaterial, _roadPainterPreviewEntities);
		else
			SpawnPreviewMeshCell(wrapper, placement.assetPath, previewMaterial, _roadPainterPreviewEntities);

		scene->FlushPVS(wrapper);
	}

	_roadPainterHasPreviewKey = true;
	_roadPainterPreviewAnchorX = effAnchorX;
	_roadPainterPreviewAnchorZ = effAnchorZ;
	_roadPainterPreviewHoverX = _roadPainterHoverX;
	_roadPainterPreviewHoverZ = _roadPainterHoverZ;
}

void CitySimulationEditorToolPlugin::OnCreateUI(HexEngine::MenuBar* menuBar)
{
	if (_uiCreated || menuBar == nullptr)
		return;

	_uiCreated = true;
	_citySimMenuRoot = new HexEngine::MenuBar::RootItem;
	_citySimMenuRoot->name = L"City Simulation";
	menuBar->AddRootItem(_citySimMenuRoot);

	auto* roadPainterItem = new HexEngine::MenuBar::Item;
	roadPainterItem->name = L"Road Painter";
	roadPainterItem->action = [this](HexEngine::MenuBar::Item*)
		{
			ShowRoadPainterDialog();
		};
	menuBar->AddSubItem(_citySimMenuRoot, roadPainterItem);
}

void CitySimulationEditorToolPlugin::OnAssetExplorerCreateNew(HexEngine::ContextMenu* menu, HexEngine::ContextRoot* rootMenu, const fs::path& baseDir, HexEngine::FileSystem* fileSystem, std::function<void()> onAssetsCreated)
{
	if (menu == nullptr || rootMenu == nullptr)
		return;

	menu->AddItem(new HexEngine::ContextItem(L"Road Prefab",
		[this, baseDir, fileSystem, onAssetsCreated](const std::wstring&)
		{
			CreateRoadPrefab(baseDir, fileSystem, onAssetsCreated);
		}), rootMenu);

	menu->AddItem(new HexEngine::ContextItem(L"Vehicle Prefab",
		[this, baseDir, fileSystem, onAssetsCreated](const std::wstring&)
		{
			CreateVehiclePrefab(baseDir, fileSystem, onAssetsCreated);
		}), rootMenu);
}

void CitySimulationEditorToolPlugin::OnMessage(HexEngine::Message* message, HexEngine::MessageListener* sender)
{
	(void)sender;

	if (_roadPainterDialog != nullptr && _roadPainterDialog->WantsDeletion())
	{
		_roadPainterDialog = nullptr;
	}

	UpdateRoadPainterRendererRegistration();

	if (_citySimulationInterface == nullptr || message == nullptr)
		return;

	if (auto* duplicated = message->CastAs<HexEngine::EditorEntityDuplicatedMessage>(); duplicated != nullptr)
	{
		duplicated->handled = _citySimulationInterface->OnEntityDuplicated(duplicated->source, duplicated->duplicate) || duplicated->handled;
		return;
	}

	if (auto* sceneMouseDown = message->CastAs<HexEngine::EditorSceneViewportMouseDownMessage>(); sceneMouseDown != nullptr)
	{
		HandleSceneViewportMouseDown(sceneMouseDown);
		return;
	}

	if (auto* sceneMouseMove = message->CastAs<HexEngine::EditorSceneViewportMouseMoveMessage>(); sceneMouseMove != nullptr)
	{
		HandleSceneViewportMouseMove(sceneMouseMove);
	}
}

void CitySimulationEditorToolPlugin::RenderCustom(HexEngine::Scene* scene, HexEngine::Camera* camera, HexEngine::MeshRenderFlags renderFlags)
{
	(void)scene;
	(void)camera;
	(void)renderFlags;

	if (!_roadPainterEnabled || !_roadPainterHasAnchor)
		return;

	// The ghost-mesh preview shows what *will* be placed; this debug-line square just marks
	// the current start anchor so the user can find it at a glance, especially before they
	// move the mouse into a valid hover cell. Drawn slightly above ground to avoid z-fight.
	const math::Color anchorColour(1.0f, 0.85f, 0.2f, 0.95f);
	const float halfCell = _roadPainterCellSize * 0.5f;
	const float y = _roadPainterAnchorHeight + 0.05f;
	const math::Vector3 center(static_cast<float>(_roadPainterAnchorX) * _roadPainterCellSize, y, static_cast<float>(_roadPainterAnchorZ) * _roadPainterCellSize);
	const math::Vector3 a(center.x - halfCell, center.y, center.z - halfCell);
	const math::Vector3 b(center.x + halfCell, center.y, center.z - halfCell);
	const math::Vector3 c(center.x + halfCell, center.y, center.z + halfCell);
	const math::Vector3 d(center.x - halfCell, center.y, center.z + halfCell);
	HexEngine::g_pEnv->_debugRenderer->DrawLine(a, b, anchorColour);
	HexEngine::g_pEnv->_debugRenderer->DrawLine(b, c, anchorColour);
	HexEngine::g_pEnv->_debugRenderer->DrawLine(c, d, anchorColour);
	HexEngine::g_pEnv->_debugRenderer->DrawLine(d, a, anchorColour);
}

void CitySimulationEditorToolPlugin::ShowRoadPainterDialog()
{
	if (_roadPainterDialog != nullptr)
	{
		if (_roadPainterDialog->WantsDeletion())
		{
			_roadPainterDialog = nullptr;
		}
		else
		{
			_roadPainterDialog->BringToFront();
			return;
		}
	}

	const int32_t dlgWidth = 520;
	const int32_t dlgHeight = 360;
	_roadPainterDialog = new HexEngine::Dialog(
		HexEngine::g_pEnv->GetUIManager().GetRootElement(),
		HexEngine::Point::GetScreenCenterWithOffset(-dlgWidth / 2, -dlgHeight / 2),
		HexEngine::Point(dlgWidth, dlgHeight),
		L"Road Painter");

	auto* widget = new HexEngine::ComponentWidget(_roadPainterDialog, HexEngine::Point(10, 10), HexEngine::Point(dlgWidth - 20, -1), L"Road Painter Assets");
	const std::vector<HexEngine::ResourceType> allowedTypes = { HexEngine::ResourceType::Mesh, HexEngine::ResourceType::Prefab };

	auto bindAssetSearch = [this, widget, &allowedTypes](const std::wstring& label, fs::path& targetPath)
	{
		auto* search = new HexEngine::AssetSearch(
			widget,
			widget->GetNextPos(),
			HexEngine::Point(widget->GetSize().x - 20, 84),
			label,
			allowedTypes,
			[this, &targetPath](HexEngine::AssetSearch*, const HexEngine::AssetSearchResult& result)
			{
				targetPath = !result.assetPath.empty() ? result.assetPath : result.absolutePath;
				if (_roadPainterEnabled)
				{
					MeasureStraightAsset(_roadPainterCellSize, _roadPainterUsesXAxis);
				}
			});
		search->SetValue(targetPath.wstring());
	};

	bindAssetSearch(L"Straight Piece", _roadPainterStraightPath);
	bindAssetSearch(L"Corner Piece", _roadPainterCornerPath);
	bindAssetSearch(L"Crossroad Piece", _roadPainterCrossroadPath);
	bindAssetSearch(L"T-Junction Piece", _roadPainterTJunctionPath);

	auto* cellSizeScale = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Cell Size Scale", &_roadPainterCellSizeScale, 0.5f, 2.0f, 0.005f, 3);
	cellSizeScale->SetOnDrag([this](float, float, float)
	{
		if (_roadPainterEnabled)
		{
			MeasureStraightAsset(_roadPainterCellSize, _roadPainterUsesXAxis);
		}
	});

	// Initial Y offset applied to the first click of a paint session. Range is generous
	// (-10..+10) because terrain scales vary widely - a small positive value (0.02 or so)
	// is typical for nudging meshes off the ground to avoid Z-fighting, but volumetric
	// terrains with large vertical scales may want more. Drag step is fine (0.01) for
	// precision tuning.
	new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Initial Y Offset", &_roadPainterInitialYOffset, -10.0f, 10.0f, 0.01f, 3);

	auto* straightYawOffset = new HexEngine::DropDown(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Straight Yaw Offset");
	straightYawOffset->SetValue(QuarterTurnLabel(_roadPainterYawQuarterTurns));
	auto setStraightYawOffset = [this, straightYawOffset](int32_t quarterTurns)
	{
		_roadPainterYawQuarterTurns = NormalizeQuarterTurns(quarterTurns);
		straightYawOffset->SetValue(QuarterTurnLabel(_roadPainterYawQuarterTurns));
		if (_roadPainterEnabled)
		{
			MeasureStraightAsset(_roadPainterCellSize, _roadPainterUsesXAxis);
		}
	};
	straightYawOffset->GetContextMenu()->AddItem(new HexEngine::ContextItem(L"0", [setStraightYawOffset](const std::wstring&) { setStraightYawOffset(0); }));
	straightYawOffset->GetContextMenu()->AddItem(new HexEngine::ContextItem(L"90", [setStraightYawOffset](const std::wstring&) { setStraightYawOffset(1); }));
	straightYawOffset->GetContextMenu()->AddItem(new HexEngine::ContextItem(L"180", [setStraightYawOffset](const std::wstring&) { setStraightYawOffset(2); }));
	straightYawOffset->GetContextMenu()->AddItem(new HexEngine::ContextItem(L"270", [setStraightYawOffset](const std::wstring&) { setStraightYawOffset(3); }));

	// Per-piece authoring offsets - apply a corrective quarter-turn to corner / T /
	// crossroad placements when the asset was authored facing a different direction
	// than the engine's base-mask convention expects (corner: N+E open ends;
	// T: N+E+W open ends). Saves the user from re-exporting the prefab.
	//
	// Local helper that builds one of these dropdowns. The captured `binding` is the
	// member backing the value; the captured drop pointer is used to refresh the
	// label after each pick. setter is invoked with the new quarter-turn count.
	auto addQuarterTurnDropdown = [&](const std::wstring& label, int32_t& binding)
	{
		auto* drop = new HexEngine::DropDown(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), label);
		drop->SetValue(QuarterTurnLabel(binding));
		auto setter = [&binding, drop](int32_t qt)
		{
			binding = NormalizeQuarterTurns(qt);
			drop->SetValue(QuarterTurnLabel(binding));
		};
		drop->GetContextMenu()->AddItem(new HexEngine::ContextItem(L"0",   [setter](const std::wstring&) { setter(0); }));
		drop->GetContextMenu()->AddItem(new HexEngine::ContextItem(L"90",  [setter](const std::wstring&) { setter(1); }));
		drop->GetContextMenu()->AddItem(new HexEngine::ContextItem(L"180", [setter](const std::wstring&) { setter(2); }));
		drop->GetContextMenu()->AddItem(new HexEngine::ContextItem(L"270", [setter](const std::wstring&) { setter(3); }));
	};
	addQuarterTurnDropdown(L"Corner Yaw Offset",    _roadPainterCornerYawQuarterTurns);
	addQuarterTurnDropdown(L"T-Junction Yaw Offset", _roadPainterTJunctionYawQuarterTurns);
	addQuarterTurnDropdown(L"Crossroad Yaw Offset",  _roadPainterCrossroadYawQuarterTurns);

	new HexEngine::Checkbox(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Add Collisions", &_roadPainterAddCollisions);

	auto* enablePainter = new HexEngine::Checkbox(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Enable Painter", &_roadPainterEnabled);
	enablePainter->SetOnCheckFn([this](HexEngine::Checkbox*, bool checked)
		{
			if (checked)
			{
				if (!ToggleRoadPainterActive())
				{
					_roadPainterEnabled = false;
				}
			}
			else
			{
				_roadPainterHasAnchor = false;
				_roadPainterHasHover = false;
				DestroyRoadPainterPreviewEntities();
				UpdateRoadPainterRendererRegistration();
			}
		});

	new HexEngine::Button(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 22), L"Reset Anchor",
		[this](HexEngine::Button*) -> bool
		{
			_roadPainterHasAnchor = false;
			DestroyRoadPainterPreviewEntities();
			return true;
		});

	new HexEngine::Button(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 22), L"Rebuild Managed Network",
		[this](HexEngine::Button*) -> bool
		{
			RebuildRoadPainterNetwork(_roadPainterAnchorHeight);
			return true;
		});

	new HexEngine::Button(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 22), L"Close",
		[this](HexEngine::Button*) -> bool
		{
			CloseRoadPainterDialog();
			return true;
		});
}

void CitySimulationEditorToolPlugin::CloseRoadPainterDialog()
{
	if (_roadPainterDialog == nullptr)
		return;

	if (!_roadPainterDialog->WantsDeletion())
		_roadPainterDialog->DeleteMe();
	_roadPainterDialog = nullptr;
}

bool CitySimulationEditorToolPlugin::ToggleRoadPainterActive()
{
	if (!MeasureStraightAsset(_roadPainterCellSize, _roadPainterUsesXAxis))
	{
		LOG_WARN("CitySimulationPlugin: Road Painter requires a valid straight mesh or prefab asset.");
		_roadPainterEnabled = false;
		_roadPainterHasAnchor = false;
		_roadPainterHasHover = false;
		DestroyRoadPainterPreviewEntities();
		return false;
	}

	_roadPainterHasAnchor = false;
	_roadPainterHasHover = false;
	DestroyRoadPainterPreviewEntities();
	UpdateRoadPainterRendererRegistration();
	return true;
}

void CitySimulationEditorToolPlugin::HandleSceneViewportMouseDown(HexEngine::EditorSceneViewportMouseDownMessage* message)
{
	if (message == nullptr || !_roadPainterEnabled)
		return;

	if (message->button != VK_LBUTTON || !message->hasHit)
		return;

	if (!MeasureStraightAsset(_roadPainterCellSize, _roadPainterUsesXAxis))
	{
		LOG_WARN("CitySimulationPlugin: Road Painter could not resolve straight asset spacing.");
		_roadPainterEnabled = false;
		_roadPainterHasAnchor = false;
		return;
	}

	if (!_roadPainterHasAnchor)
	{
		// First click of a paint session - lock in the network origin. If a RoadNetwork
		// already exists in the scene (extending an existing network), inherit its
		// position as the origin so new cells align with what's already there. Else
		// snap the click position to the EDITOR's translate-snap grid (typically much
		// finer than cellSize) - this is the user's preference for where the network
		// should sit, and we capture it at the first click so the painter can use the
		// finer grid for placement instead of cellSize multiples.
		auto* scene = HexEngine::g_pEnv->_sceneManager->GetCurrentScene().get();
		if (Entity* existingRoot = (scene != nullptr) ? FindRoadNetworkRoot(scene) : nullptr; existingRoot != nullptr)
		{
			const math::Vector3 rootPos = existingRoot->GetPosition();
			_roadPainterOriginX = rootPos.x;
			_roadPainterOriginZ = rootPos.z;
		}
		else
		{
			_roadPainterOriginX = SnapToEditorGrid(message->worldPosition.x);
			_roadPainterOriginZ = SnapToEditorGrid(message->worldPosition.z);
		}

		_roadPainterAnchorX = SnapToGrid(message->worldPosition.x, _roadPainterOriginX, _roadPainterCellSize);
		_roadPainterAnchorZ = SnapToGrid(message->worldPosition.z, _roadPainterOriginZ, _roadPainterCellSize);
		// Apply the configured Y offset ONLY here, at the very first click that creates
		// a fresh anchor. After this point the height inherits forward via the
		// anchor-cell lookup in PaintOrthogonalRun (and via _roadPainterAnchorHeight on
		// fresh-session fallback) so the offset is baked in once and propagates without
		// being re-added on every click. The raycast hit lands on the ground; lifting
		// by a small amount keeps the road from Z-fighting with the terrain surface.
		_roadPainterAnchorHeight = message->worldPosition.y + _roadPainterInitialYOffset;
		_roadPainterHasAnchor = true;
		_roadPainterHoverX = _roadPainterAnchorX;
		_roadPainterHoverZ = _roadPainterAnchorZ;
		_roadPainterHoverHeight = _roadPainterAnchorHeight;
		_roadPainterHasHover = true;
		RefreshRoadPainterPreview();
		message->handled = true;
		return;
	}

	PaintOrthogonalRun(message->worldPosition);
	RefreshRoadPainterPreview();
	message->handled = true;
}

void CitySimulationEditorToolPlugin::HandleSceneViewportMouseMove(HexEngine::EditorSceneViewportMouseMoveMessage* message)
{
	if (message == nullptr || !_roadPainterEnabled)
		return;

	if (!message->hasHit)
	{
		if (_roadPainterHasHover)
		{
			_roadPainterHasHover = false;
			DestroyRoadPainterPreviewEntities();
		}
		return;
	}

	if (!MeasureStraightAsset(_roadPainterCellSize, _roadPainterUsesXAxis))
		return;

	// Keep the origin in sync with the world. Before the first click of a session
	// (HasAnchor false) the origin tracks the editor-snapped hover position so the
	// pre-click ghost lands on the editor's fine grid - showing the user where the
	// click would actually land. Once they have an anchor we instead sync from the
	// RoadNetwork entity's position (so dragging the network in the editor and then
	// drawing more roads keeps the new cells aligned to the network's new location).
	auto* scene = HexEngine::g_pEnv->_sceneManager->GetCurrentScene().get();
	if (!_roadPainterHasAnchor)
	{
		if (Entity* existingRoot = (scene != nullptr) ? FindRoadNetworkRoot(scene) : nullptr; existingRoot != nullptr)
		{
			const math::Vector3 rootPos = existingRoot->GetPosition();
			_roadPainterOriginX = rootPos.x;
			_roadPainterOriginZ = rootPos.z;
		}
		else
		{
			_roadPainterOriginX = SnapToEditorGrid(message->worldPosition.x);
			_roadPainterOriginZ = SnapToEditorGrid(message->worldPosition.z);
		}
	}
	else if (Entity* existingRoot = (scene != nullptr) ? FindRoadNetworkRoot(scene) : nullptr; existingRoot != nullptr)
	{
		const math::Vector3 rootPos = existingRoot->GetPosition();
		_roadPainterOriginX = rootPos.x;
		_roadPainterOriginZ = rootPos.z;
	}

	_roadPainterHoverX = SnapToGrid(message->worldPosition.x, _roadPainterOriginX, _roadPainterCellSize);
	_roadPainterHoverZ = SnapToGrid(message->worldPosition.z, _roadPainterOriginZ, _roadPainterCellSize);
	_roadPainterHoverHeight = message->worldPosition.y;
	_roadPainterHasHover = true;
	// Refresh is cheap when the snapped (anchor, hover) hasn't changed - the function does
	// its own deduplication. Calling here unconditionally is what makes the preview track
	// the cursor as it crosses cell boundaries during a single drag.
	RefreshRoadPainterPreview();
}

bool CitySimulationEditorToolPlugin::MeasureStraightAsset(float& outSpacing, bool& outUsesXAxis) const
{
	PlacementSpec spec;
	if (!TryBuildPlacementSpec(_roadPainterStraightPath, _roadPainterYawQuarterTurns, spec))
		return false;

	// Apply the user-configurable scale so spacing can be tuned without re-authoring the
	// mesh. Default 1.0 uses the raw AABB extent; values < 1.0 tighten the grid (good for
	// meshes that include caps/curbs that overshoot the road body) and > 1.0 loosen it.
	const float scale = (_roadPainterCellSizeScale > 0.0f) ? _roadPainterCellSizeScale : 1.0f;
	outSpacing = std::max(spec.length * scale, 0.01f);
	outUsesXAxis = spec.usesXAxis;
	return true;
}

void CitySimulationEditorToolPlugin::PaintOrthogonalRun(const math::Vector3& worldPosition)
{
	auto* scene = HexEngine::g_pEnv->_sceneManager->GetCurrentScene().get();
	if (scene == nullptr)
		return;

	const GridCoord start{ _roadPainterAnchorX, _roadPainterAnchorZ };
	const GridCoord end{
		SnapToGrid(worldPosition.x, _roadPainterOriginX, _roadPainterCellSize),
		SnapToGrid(worldPosition.z, _roadPainterOriginZ, _roadPainterCellSize)
	};

	// Build the orthogonal run as a flat list of cell coordinates. Used to be merged into
	// a heights map alongside ALL existing managed cells and handed to RebuildManagedRoadNetwork,
	// which destroyed+respawned every road in the network on every click - O(network size)
	// per commit, so each successive line drawn got slower as the city grew. Now we keep
	// the run cells separate and hand them to ApplyIncrementalRoadNetworkChange, which only
	// touches the new cells and their immediate existing neighbours.
	std::vector<GridCoord> runCells;
	const int32_t deltaX = std::abs(end.x - start.x);
	const int32_t deltaZ = std::abs(end.z - start.z);
	if (deltaX >= deltaZ)
	{
		const int32_t step = end.x >= start.x ? 1 : -1;
		for (int32_t x = start.x;; x += step)
		{
			runCells.push_back({ x, start.z });
			if (x == end.x)
				break;
		}
	}
	else
	{
		const int32_t step = end.z >= start.z ? 1 : -1;
		for (int32_t z = start.z;; z += step)
		{
			runCells.push_back({ start.x, z });
			if (z == end.z)
				break;
		}
	}

	PlacementSpec straightSpec;
	if (!TryBuildPlacementSpec(_roadPainterStraightPath, _roadPainterYawQuarterTurns, straightSpec))
		return;

	std::optional<PlacementSpec> cornerSpec;
	std::optional<PlacementSpec> crossroadSpec;
	std::optional<PlacementSpec> tJunctionSpec;
	PlacementSpec tempSpec;
	if (TryBuildPlacementSpec(_roadPainterCornerPath, _roadPainterYawQuarterTurns, tempSpec)) cornerSpec = tempSpec;
	if (TryBuildPlacementSpec(_roadPainterCrossroadPath, _roadPainterYawQuarterTurns, tempSpec)) crossroadSpec = tempSpec;
	if (TryBuildPlacementSpec(_roadPainterTJunctionPath, _roadPainterYawQuarterTurns, tempSpec)) tJunctionSpec = tempSpec;

	// When extending an existing road, lock new cells to the anchor cell's Y rather than
	// the raycast hit Y. The raycast lands on the terrain under the mouse, which steps
	// the road up/down with whatever ground is below the cursor - jarring on sloped
	// terrain and visibly wrong where the new road meets a previously placed straight at
	// a different height. Reading height from the existing wrapper at `start` keeps the
	// whole road at a consistent level set by the very first click that put road down.
	//
	// Only fall back to _roadPainterAnchorHeight (the raycast Y stored on the previous
	// click) when there's no managed cell at the anchor - i.e. when this is the first
	// click of a fresh paint session and there's nothing to inherit from yet.
	float runHeight = _roadPainterAnchorHeight;
	if (auto* existingAtAnchor = FindManagedRoadCell(scene, start); existingAtAnchor != nullptr)
	{
		runHeight = existingAtAnchor->GetPosition().y;
	}

	// Ensure the RoadNetwork root exists at the painter's locked origin BEFORE the
	// commit so the wrappers we create have a parent to attach to with the right
	// world transform. If a root already exists we leave its position alone (the user
	// may have moved it, and we want new cells to land relative to its CURRENT pos -
	// _roadPainterOriginX/Z is kept in sync by the mouse-move handler).
	if (FindRoadNetworkRoot(scene) == nullptr)
	{
		scene->CreateEntity(kRoadNetworkRootName,
			math::Vector3(_roadPainterOriginX, 0.0f, _roadPainterOriginZ),
			math::Quaternion::Identity,
			math::Vector3(1.0f));
	}

	const math::Vector2 origin(_roadPainterOriginX, _roadPainterOriginZ);
	ApplyIncrementalRoadNetworkChange(scene, runCells, origin, runHeight,
		straightSpec, cornerSpec, crossroadSpec, tJunctionSpec,
		_roadPainterYawQuarterTurns,
		_roadPainterCornerYawQuarterTurns,
		_roadPainterTJunctionYawQuarterTurns,
		_roadPainterCrossroadYawQuarterTurns,
		_roadPainterCellSize, _roadPainterAddCollisions);

	// The run only spans the major axis (deltaX >= deltaZ -> X, else Z); the
	// perpendicular component of `end` was never placed as a cell. Advancing the
	// anchor to `end` directly leaves it stranded N or E of the actual last placed
	// cell - and the next click's run starts from that stranded anchor instead of
	// the road end, so the perpendicular run's first cell has NO connection to the
	// previous run's last cell and the would-be corner is never detected (it has
	// only one connection direction in its mask). Pin the anchor to the last cell
	// actually written, i.e. the run's tail (which is `end` along the major axis
	// and `start` along the minor).
	const GridCoord lastPlaced = (deltaX >= deltaZ)
		? GridCoord{ end.x, start.z }
		: GridCoord{ start.x, end.z };

	_roadPainterAnchorX = lastPlaced.x;
	_roadPainterAnchorZ = lastPlaced.z;
	// Keep the anchor at the run's Y, not the raycast click Y - otherwise the next
	// click's "extending an existing road" lookup would still see this cell's actual
	// position.y (which is correct), but if the user immediately starts a NEW paint
	// session by clicking off-road, _roadPainterAnchorHeight is the fallback and we
	// want it consistent with what we just placed rather than stale raycast data.
	_roadPainterAnchorHeight = runHeight;
	_roadPainterHasAnchor = true;
	_roadPainterHoverX = lastPlaced.x;
	_roadPainterHoverZ = lastPlaced.z;
	_roadPainterHoverHeight = runHeight;
	_roadPainterHasHover = true;
}

void CitySimulationEditorToolPlugin::RebuildRoadPainterNetwork(float defaultHeight) const
{
	auto* scene = HexEngine::g_pEnv->_sceneManager->GetCurrentScene().get();
	if (scene == nullptr)
		return;

	PlacementSpec straightSpec;
	if (!TryBuildPlacementSpec(_roadPainterStraightPath, _roadPainterYawQuarterTurns, straightSpec))
		return;

	std::optional<PlacementSpec> cornerSpec;
	std::optional<PlacementSpec> crossroadSpec;
	std::optional<PlacementSpec> tJunctionSpec;
	PlacementSpec tempSpec;
	if (TryBuildPlacementSpec(_roadPainterCornerPath, _roadPainterYawQuarterTurns, tempSpec)) cornerSpec = tempSpec;
	if (TryBuildPlacementSpec(_roadPainterCrossroadPath, _roadPainterYawQuarterTurns, tempSpec)) crossroadSpec = tempSpec;
	if (TryBuildPlacementSpec(_roadPainterTJunctionPath, _roadPainterYawQuarterTurns, tempSpec)) tJunctionSpec = tempSpec;

	std::unordered_map<GridCoord, float, GridCoordHash> heights;
	std::vector<HexEngine::Entity*> wrappers;
	GatherManagedRoadCells(scene, heights, wrappers);
	for (auto& [coord, height] : heights)
	{
		if (!std::isfinite(height))
			height = defaultHeight;
	}

	// Read origin from the RoadNetwork entity if it exists (full rebuild typically
	// operates on a network that was previously painted) - falls back to the painter's
	// stored origin which is kept in sync with the network entity by mouse handlers.
	math::Vector2 origin(_roadPainterOriginX, _roadPainterOriginZ);
	if (auto* root = FindRoadNetworkRoot(scene); root != nullptr)
	{
		const math::Vector3 p = root->GetPosition();
		origin = math::Vector2(p.x, p.z);
	}

	RebuildManagedRoadNetwork(scene, heights, origin,
		straightSpec, cornerSpec, crossroadSpec, tJunctionSpec,
		_roadPainterYawQuarterTurns,
		_roadPainterCornerYawQuarterTurns,
		_roadPainterTJunctionYawQuarterTurns,
		_roadPainterCrossroadYawQuarterTurns,
		_roadPainterCellSize, _roadPainterAddCollisions);
}

void CitySimulationEditorToolPlugin::UpdateRoadPainterRendererRegistration()
{
	auto currentScene = HexEngine::g_pEnv != nullptr && HexEngine::g_pEnv->_sceneManager != nullptr
		? HexEngine::g_pEnv->_sceneManager->GetCurrentScene().get()
		: nullptr;

	if (!_roadPainterEnabled)
	{
		if (_roadPainterRegisteredScene != nullptr)
		{
			_roadPainterRegisteredScene->UnregisterCustomRenderer(this);
			_roadPainterRegisteredScene = nullptr;
		}
		return;
	}

	if (_roadPainterRegisteredScene == currentScene)
		return;

	if (_roadPainterRegisteredScene != nullptr)
	{
		_roadPainterRegisteredScene->UnregisterCustomRenderer(this);
		_roadPainterRegisteredScene = nullptr;
	}

	if (currentScene != nullptr)
	{
		currentScene->RegisterCustomRenderer(this);
		_roadPainterRegisteredScene = currentScene;
	}
}

void CitySimulationEditorToolPlugin::CreateRoadPrefab(const fs::path& baseDir, HexEngine::FileSystem* fileSystem, const std::function<void()>& onAssetsCreated)
{
	if (fileSystem == nullptr)
	{
		LOG_WARN("CitySimulationPlugin: cannot create road prefab without filesystem context.");
		return;
	}

	fs::path targetPath = fileSystem->GetLocalAbsoluteDataPath(baseDir / L"NewRoadPrefab.hprefab");
	int32_t duplicateIndex = 1;
	while (fs::exists(targetPath))
	{
		targetPath = fileSystem->GetLocalAbsoluteDataPath(baseDir / std::format(L"NewRoadPrefab{}.hprefab", duplicateIndex));
		++duplicateIndex;
	}

	auto prefabScene = HexEngine::g_pEnv->_sceneManager->CreateEmptyScene(false);
	if (prefabScene == nullptr)
	{
		LOG_WARN("CitySimulationPlugin: failed to create scene for road prefab.");
		return;
	}

	auto* rootEntity = prefabScene->CreateEntity("RoadSection");
	if (rootEntity == nullptr)
	{
		LOG_WARN("CitySimulationPlugin: failed to create road root entity.");
		return;
	}

	rootEntity->AddComponent<RoadComponent>();

	std::vector<HexEngine::Entity*> entitiesToSave = { rootEntity };
	HexEngine::SceneSaveFile saveFile(targetPath, std::ios::out | std::ios::trunc, prefabScene, HexEngine::SceneFileFlags::IsPrefab);
	if (!saveFile.Save(entitiesToSave))
	{
		LOG_WARN("CitySimulationPlugin: failed to save road prefab '%s'.", targetPath.string().c_str());
		return;
	}

	if (onAssetsCreated)
	{
		onAssetsCreated();
	}
}

void CitySimulationEditorToolPlugin::CreateVehiclePrefab(const fs::path& baseDir, HexEngine::FileSystem* fileSystem, const std::function<void()>& onAssetsCreated)
{
	if (fileSystem == nullptr)
	{
		LOG_WARN("CitySimulationPlugin: cannot create vehicle prefab without filesystem context.");
		return;
	}

	fs::path targetPath = fileSystem->GetLocalAbsoluteDataPath(baseDir / L"NewVehiclePrefab.hprefab");
	int32_t duplicateIndex = 1;
	while (fs::exists(targetPath))
	{
		targetPath = fileSystem->GetLocalAbsoluteDataPath(baseDir / std::format(L"NewVehiclePrefab{}.hprefab", duplicateIndex));
		++duplicateIndex;
	}

	auto prefabScene = HexEngine::g_pEnv->_sceneManager->CreateEmptyScene(false);
	if (prefabScene == nullptr)
	{
		LOG_WARN("CitySimulationPlugin: failed to create scene for vehicle prefab.");
		return;
	}

	auto* rootEntity = prefabScene->CreateEntity("VehicleRoot");
	if (rootEntity == nullptr)
	{
		LOG_WARN("CitySimulationPlugin: failed to create vehicle root entity.");
		return;
	}

	rootEntity->AddComponent<VehicleComponent>();

	std::vector<HexEngine::Entity*> entitiesToSave = { rootEntity };
	HexEngine::SceneSaveFile saveFile(targetPath, std::ios::out | std::ios::trunc, prefabScene, HexEngine::SceneFileFlags::IsPrefab);
	if (!saveFile.Save(entitiesToSave))
	{
		LOG_WARN("CitySimulationPlugin: failed to save vehicle prefab '%s'.", targetPath.string().c_str());
		return;
	}

	if (onAssetsCreated)
	{
		onAssetsCreated();
	}
}

