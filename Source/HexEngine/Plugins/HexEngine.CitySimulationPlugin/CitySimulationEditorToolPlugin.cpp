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
#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>

	namespace
	{
		using namespace HexEngine;

	constexpr const char* kRoadPainterWrapperPrefix = "CityRoadDraw_";
	constexpr const char* kRoadPainterPreviewPrefix = "CityRoadPreview_";
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
	};

	struct CellPlacement
	{
		fs::path assetPath;
		bool isPrefab = false;
		math::Quaternion rotation = math::Quaternion::Identity;
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

	int32_t SnapToGrid(float worldValue, float cellSize)
	{
		if (cellSize <= 0.0001f)
			return 0;
		return static_cast<int32_t>(std::lround(worldValue / cellSize));
	}

	math::Vector3 GridToWorld(const GridCoord& coord, float cellSize, float y)
	{
		return math::Vector3(static_cast<float>(coord.x) * cellSize, y, static_cast<float>(coord.z) * cellSize);
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

	bool TryMeasureMeshAsset(const fs::path& assetPath, float& outSizeX, float& outSizeZ)
	{
		auto mesh = Mesh::Create(assetPath);
		if (mesh == nullptr)
			return false;

		const auto bounds = mesh->GetAABB();
		outSizeX = std::max(bounds.Extents.x * 2.0f, 0.01f);
		outSizeZ = std::max(bounds.Extents.z * 2.0f, 0.01f);
		return true;
	}

	bool TryMeasurePrefabAsset(const fs::path& assetPath, float& outSizeX, float& outSizeZ)
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
		const bool measured = outSpec.isPrefab
			? TryMeasurePrefabAsset(assetPath, sizeX, sizeZ)
			: TryMeasureMeshAsset(assetPath, sizeX, sizeZ);
		if (!measured)
			return false;

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
			if (!ComputeRotationForMask(desiredMask, tBaseMask, outPlacement.rotation))
				outPlacement.rotation = math::Quaternion::Identity;
			ApplyQuarterTurnRotation(outPlacement.rotation, tJunctionYawQuarterTurns);
			return true;
		}

		if (connections == 2 && !IsOpposingStraight(desiredMask) && cornerSpec.has_value())
		{
			outPlacement.assetPath = cornerSpec->assetPath;
			outPlacement.isPrefab = cornerSpec->isPrefab;
			if (!ComputeRotationForMask(desiredMask, cornerBaseMask, outPlacement.rotation))
				outPlacement.rotation = math::Quaternion::Identity;
			ApplyQuarterTurnRotation(outPlacement.rotation, cornerYawQuarterTurns);
			return true;
		}

		outPlacement.assetPath = straightSpec.assetPath;
		outPlacement.isPrefab = straightSpec.isPrefab;

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

	void GatherManagedRoadCells(
		Scene* scene,
		std::unordered_map<GridCoord, float, GridCoordHash>& outHeights,
		std::vector<Entity*>& outWrappers)
	{
		if (scene == nullptr)
			return;

		for (const auto& bySignature : scene->GetEntities())
		{
			for (auto* entity : bySignature.second)
			{
				if (entity == nullptr || entity->IsPendingDeletion())
					continue;

				GridCoord coord;
				if (!TryParseManagedWrapperName(entity->GetName(), coord))
					continue;

				if (entity->GetParent() != nullptr)
					continue;

				outHeights[coord] = entity->GetPosition().y;
				outWrappers.push_back(entity);
			}
		}
	}

	void RebuildManagedRoadNetwork(
		Scene* scene,
		const std::unordered_map<GridCoord, float, GridCoordHash>& heights,
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
			scene->DestroyEntity(wrapper);
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

			auto* wrapper = scene->CreateEntity(MakeManagedWrapperName(coord), GridToWorld(coord, spacing, height), placement.rotation, math::Vector3(1.0f));
			if (wrapper == nullptr)
				continue;

			wrapper->SetCastsShadows(false);

			if (placement.isPrefab)
				SpawnPrefabCell(wrapper, placement.assetPath);
			else
				SpawnMeshCell(wrapper, placement.assetPath, addPhysics);
		}

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

		// GatherManagedRoadCells filters out non-root wrappers; mirror that here so the
		// incremental path agrees with the full-rebuild path on what counts as "the cell".
		if (entity->GetParent() != nullptr)
			return nullptr;

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
		for (const auto& action : actions)
		{
			if (action.existingWrapper != nullptr)
				scene->DestroyEntity(action.existingWrapper);

			auto* wrapper = scene->CreateEntity(MakeManagedWrapperName(action.coord), GridToWorld(action.coord, spacing, action.height), action.placement.rotation, math::Vector3(1.0f));
			if (wrapper == nullptr)
				continue;

			wrapper->SetCastsShadows(false);

			if (action.placement.isPrefab)
				SpawnPrefabCell(wrapper, action.placement.assetPath);
			else
				SpawnMeshCell(wrapper, action.placement.assetPath, addPhysics);

			scene->FlushPVS(wrapper);
		}
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
	if (!_roadPainterEnabled || !_roadPainterHasAnchor || !_roadPainterHasHover)
	{
		DestroyRoadPainterPreviewEntities();
		return;
	}

	// Bail out cheaply if the (anchor, hover) snapped grid pair is unchanged; the run cells
	// only depend on the snapped pair, not on raw mouse position. This is what keeps mouse-
	// move from rebuilding the preview every pixel of motion.
	if (_roadPainterHasPreviewKey
		&& _roadPainterPreviewAnchorX == _roadPainterAnchorX
		&& _roadPainterPreviewAnchorZ == _roadPainterAnchorZ
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
	const GridCoord start{ _roadPainterAnchorX, _roadPainterAnchorZ };
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
		const std::string wrapperName = std::format("{}{}_{}", kRoadPainterPreviewPrefix, coord.x, coord.z);
		auto* wrapper = scene->CreateEntity(wrapperName, GridToWorld(coord, previewSpacing, _roadPainterAnchorHeight), placement.rotation, math::Vector3(1.0f));
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
	_roadPainterPreviewAnchorX = _roadPainterAnchorX;
	_roadPainterPreviewAnchorZ = _roadPainterAnchorZ;
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
		_roadPainterAnchorX = SnapToGrid(message->worldPosition.x, _roadPainterCellSize);
		_roadPainterAnchorZ = SnapToGrid(message->worldPosition.z, _roadPainterCellSize);
		_roadPainterAnchorHeight = message->worldPosition.y;
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

	_roadPainterHoverX = SnapToGrid(message->worldPosition.x, _roadPainterCellSize);
	_roadPainterHoverZ = SnapToGrid(message->worldPosition.z, _roadPainterCellSize);
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
	const GridCoord end{ SnapToGrid(worldPosition.x, _roadPainterCellSize), SnapToGrid(worldPosition.z, _roadPainterCellSize) };

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

	ApplyIncrementalRoadNetworkChange(scene, runCells, _roadPainterAnchorHeight,
		straightSpec, cornerSpec, crossroadSpec, tJunctionSpec,
		_roadPainterYawQuarterTurns,
		_roadPainterCornerYawQuarterTurns,
		_roadPainterTJunctionYawQuarterTurns,
		_roadPainterCrossroadYawQuarterTurns,
		_roadPainterCellSize, _roadPainterAddCollisions);

	_roadPainterAnchorX = end.x;
	_roadPainterAnchorZ = end.z;
	_roadPainterAnchorHeight = worldPosition.y;
	_roadPainterHasAnchor = true;
	_roadPainterHoverX = end.x;
	_roadPainterHoverZ = end.z;
	_roadPainterHoverHeight = worldPosition.y;
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

	RebuildManagedRoadNetwork(scene, heights,
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

