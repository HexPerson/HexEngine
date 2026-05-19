#include "CitySimulationEditorToolPlugin.hpp"
#include "CitySimulationInterface.hpp"
#include "RoadComponent.hpp"
#include "VehicleComponent.hpp"

#include <HexEngine.Core/Entity/Component/RigidBody.hpp>
#include <HexEngine.Core/Entity/Component/StaticMeshComponent.hpp>
#include <HexEngine.Core/FileSystem/PrefabLoader.hpp>
#include <HexEngine.Core/FileSystem/SceneSaveFile.hpp>
#include <HexEngine.Core/GUI/Elements/AssetSearch.hpp>
#include <HexEngine.Core/GUI/Elements/Button.hpp>
#include <HexEngine.Core/GUI/Elements/Checkbox.hpp>
#include <HexEngine.Core/GUI/Elements/ComponentWidget.hpp>
#include <HexEngine.Core/GUI/Elements/Dialog.hpp>
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

	bool TryMeasureMeshAsset(const fs::path& assetPath, float& outLength, bool& outUsesXAxis)
	{
		auto mesh = Mesh::Create(assetPath);
		if (mesh == nullptr)
			return false;

		const auto bounds = mesh->GetAABB();
		const float sizeX = std::max(bounds.Extents.x * 2.0f, 0.01f);
		const float sizeZ = std::max(bounds.Extents.z * 2.0f, 0.01f);
		outUsesXAxis = sizeX >= sizeZ;
		outLength = std::max(outUsesXAxis ? sizeX : sizeZ, 0.01f);
		return true;
	}

	bool TryMeasurePrefabAsset(const fs::path& assetPath, float& outLength, bool& outUsesXAxis)
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
		const float sizeX = std::max(size.x, 0.01f);
		const float sizeZ = std::max(size.z, 0.01f);
		outUsesXAxis = sizeX >= sizeZ;
		outLength = std::max(outUsesXAxis ? sizeX : sizeZ, 0.01f);
		return true;
	}

	bool TryBuildPlacementSpec(const fs::path& assetPath, int32_t yawQuarterTurns, PlacementSpec& outSpec)
	{
		if (assetPath.empty())
			return false;

		outSpec = {};
		outSpec.assetPath = assetPath;
		outSpec.isPrefab = assetPath.extension() == ".hprefab";
		const bool measured = outSpec.isPrefab
			? TryMeasurePrefabAsset(assetPath, outSpec.length, outSpec.usesXAxis)
			: TryMeasureMeshAsset(assetPath, outSpec.length, outSpec.usesXAxis);
		if (!measured)
			return false;

		outSpec.usesXAxis = ApplyQuarterTurnToAxis(outSpec.usesXAxis, yawQuarterTurns);
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

	void SpawnMeshCell(Entity* wrapper, const fs::path& assetPath)
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
		EnsureMeshPhysics(child, mesh);
		scene->FlushPVS(child);
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
			wrapper->GetScene()->FlushPVS(root);
		}
	}

	bool TryChoosePlacement(
		uint8_t desiredMask,
		const PlacementSpec& straightSpec,
		const std::optional<PlacementSpec>& cornerSpec,
		const std::optional<PlacementSpec>& crossroadSpec,
		const std::optional<PlacementSpec>& tJunctionSpec,
		int32_t yawQuarterTurns,
		CellPlacement& outPlacement)
	{
		const int32_t connections = CountConnections(desiredMask);
		const uint8_t straightBaseMask = straightSpec.usesXAxis ? (DirectionEast | DirectionWest) : (DirectionNorth | DirectionSouth);
		const uint8_t cornerBaseMask = DirectionNorth | DirectionEast;
		const uint8_t tBaseMask = DirectionNorth | DirectionEast | DirectionWest;

		outPlacement = {};

		if (connections >= 4 && crossroadSpec.has_value())
		{
			outPlacement.assetPath = crossroadSpec->assetPath;
			outPlacement.isPrefab = crossroadSpec->isPrefab;
			outPlacement.rotation = math::Quaternion::Identity;
			ApplyQuarterTurnRotation(outPlacement.rotation, yawQuarterTurns);
			return true;
		}

		if (connections == 3 && tJunctionSpec.has_value())
		{
			outPlacement.assetPath = tJunctionSpec->assetPath;
			outPlacement.isPrefab = tJunctionSpec->isPrefab;
			if (!ComputeRotationForMask(desiredMask, tBaseMask, outPlacement.rotation))
				outPlacement.rotation = math::Quaternion::Identity;
			ApplyQuarterTurnRotation(outPlacement.rotation, yawQuarterTurns);
			return true;
		}

		if (connections == 2 && !IsOpposingStraight(desiredMask) && cornerSpec.has_value())
		{
			outPlacement.assetPath = cornerSpec->assetPath;
			outPlacement.isPrefab = cornerSpec->isPrefab;
			if (!ComputeRotationForMask(desiredMask, cornerBaseMask, outPlacement.rotation))
				outPlacement.rotation = math::Quaternion::Identity;
			ApplyQuarterTurnRotation(outPlacement.rotation, yawQuarterTurns);
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
		ApplyQuarterTurnRotation(outPlacement.rotation, yawQuarterTurns);
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
		int32_t yawQuarterTurns)
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

		for (const auto& [coord, height] : heights)
		{
			uint8_t mask = DirectionNone;
			if (hasCell({ coord.x, coord.z + 1 })) mask |= DirectionNorth;
			if (hasCell({ coord.x + 1, coord.z })) mask |= DirectionEast;
			if (hasCell({ coord.x, coord.z - 1 })) mask |= DirectionSouth;
			if (hasCell({ coord.x - 1, coord.z })) mask |= DirectionWest;

			CellPlacement placement;
			if (!TryChoosePlacement(mask, straightSpec, cornerSpec, crossroadSpec, tJunctionSpec, yawQuarterTurns, placement))
				continue;

			auto* wrapper = scene->CreateEntity(MakeManagedWrapperName(coord), GridToWorld(coord, straightSpec.length, height), placement.rotation, math::Vector3(1.0f));
			if (wrapper == nullptr)
				continue;

			if (placement.isPrefab)
				SpawnPrefabCell(wrapper, placement.assetPath);
			else
				SpawnMeshCell(wrapper, placement.assetPath);

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
	if (_roadPainterRegisteredScene != nullptr)
	{
		_roadPainterRegisteredScene->UnregisterCustomRenderer(this);
		_roadPainterRegisteredScene = nullptr;
	}
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

	if (!_roadPainterEnabled || !_roadPainterHasAnchor || !_roadPainterHasHover)
		return;

	const math::Color previewColour(0.15f, 0.7f, 1.0f, 0.95f);
	const math::Color anchorColour(1.0f, 0.85f, 0.2f, 0.95f);

	const int32_t deltaX = std::abs(_roadPainterHoverX - _roadPainterAnchorX);
	const int32_t deltaZ = std::abs(_roadPainterHoverZ - _roadPainterAnchorZ);
	const bool horizontal = deltaX >= deltaZ;
	const float y = _roadPainterAnchorHeight + 0.05f;
	const float halfCell = _roadPainterCellSize * 0.5f;

	auto drawCell = [halfCell](const math::Vector3& center, const math::Color& colour)
	{
		const math::Vector3 a(center.x - halfCell, center.y, center.z - halfCell);
		const math::Vector3 b(center.x + halfCell, center.y, center.z - halfCell);
		const math::Vector3 c(center.x + halfCell, center.y, center.z + halfCell);
		const math::Vector3 d(center.x - halfCell, center.y, center.z + halfCell);
		HexEngine::g_pEnv->_debugRenderer->DrawLine(a, b, colour);
		HexEngine::g_pEnv->_debugRenderer->DrawLine(b, c, colour);
		HexEngine::g_pEnv->_debugRenderer->DrawLine(c, d, colour);
		HexEngine::g_pEnv->_debugRenderer->DrawLine(d, a, colour);
	};

	drawCell(math::Vector3(static_cast<float>(_roadPainterAnchorX) * _roadPainterCellSize, y, static_cast<float>(_roadPainterAnchorZ) * _roadPainterCellSize), anchorColour);

	if (horizontal)
	{
		const int32_t step = _roadPainterHoverX >= _roadPainterAnchorX ? 1 : -1;
		for (int32_t x = _roadPainterAnchorX;; x += step)
		{
			drawCell(math::Vector3(static_cast<float>(x) * _roadPainterCellSize, y, static_cast<float>(_roadPainterAnchorZ) * _roadPainterCellSize), previewColour);
			if (x == _roadPainterHoverX)
				break;
		}
	}
	else
	{
		const int32_t step = _roadPainterHoverZ >= _roadPainterAnchorZ ? 1 : -1;
		for (int32_t z = _roadPainterAnchorZ;; z += step)
		{
			drawCell(math::Vector3(static_cast<float>(_roadPainterAnchorX) * _roadPainterCellSize, y, static_cast<float>(z) * _roadPainterCellSize), previewColour);
			if (z == _roadPainterHoverZ)
				break;
		}
	}
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
				UpdateRoadPainterRendererRegistration();
			}
		});

	new HexEngine::Button(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 22), L"Reset Anchor",
		[this](HexEngine::Button*) -> bool
		{
			_roadPainterHasAnchor = false;
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
		return false;
	}

	_roadPainterHasAnchor = false;
	_roadPainterHasHover = false;
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
		message->handled = true;
		return;
	}

	PaintOrthogonalRun(message->worldPosition);
	message->handled = true;
}

void CitySimulationEditorToolPlugin::HandleSceneViewportMouseMove(HexEngine::EditorSceneViewportMouseMoveMessage* message)
{
	if (message == nullptr || !_roadPainterEnabled)
		return;

	if (!message->hasHit)
	{
		_roadPainterHasHover = false;
		return;
	}

	if (!MeasureStraightAsset(_roadPainterCellSize, _roadPainterUsesXAxis))
		return;

	_roadPainterHoverX = SnapToGrid(message->worldPosition.x, _roadPainterCellSize);
	_roadPainterHoverZ = SnapToGrid(message->worldPosition.z, _roadPainterCellSize);
	_roadPainterHoverHeight = message->worldPosition.y;
	_roadPainterHasHover = true;
}

bool CitySimulationEditorToolPlugin::MeasureStraightAsset(float& outSpacing, bool& outUsesXAxis) const
{
	PlacementSpec spec;
	if (!TryBuildPlacementSpec(_roadPainterStraightPath, _roadPainterYawQuarterTurns, spec))
		return false;

	outSpacing = spec.length;
	outUsesXAxis = spec.usesXAxis;
	return true;
}

void CitySimulationEditorToolPlugin::PaintOrthogonalRun(const math::Vector3& worldPosition)
{
	auto* scene = HexEngine::g_pEnv->_sceneManager->GetCurrentScene().get();
	if (scene == nullptr)
		return;

	std::unordered_map<GridCoord, float, GridCoordHash> heights;
	std::vector<HexEngine::Entity*> wrappers;
	GatherManagedRoadCells(scene, heights, wrappers);

	const GridCoord start{ _roadPainterAnchorX, _roadPainterAnchorZ };
	const GridCoord end{ SnapToGrid(worldPosition.x, _roadPainterCellSize), SnapToGrid(worldPosition.z, _roadPainterCellSize) };

	const int32_t deltaX = std::abs(end.x - start.x);
	const int32_t deltaZ = std::abs(end.z - start.z);
	if (deltaX >= deltaZ)
	{
		const int32_t step = end.x >= start.x ? 1 : -1;
		for (int32_t x = start.x;; x += step)
		{
			heights[{ x, start.z }] = _roadPainterAnchorHeight;
			if (x == end.x)
				break;
		}
	}
	else
	{
		const int32_t step = end.z >= start.z ? 1 : -1;
		for (int32_t z = start.z;; z += step)
		{
			heights[{ start.x, z }] = _roadPainterAnchorHeight;
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

	RebuildManagedRoadNetwork(scene, heights, straightSpec, cornerSpec, crossroadSpec, tJunctionSpec, _roadPainterYawQuarterTurns);

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

	RebuildManagedRoadNetwork(scene, heights, straightSpec, cornerSpec, crossroadSpec, tJunctionSpec, _roadPainterYawQuarterTurns);
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

