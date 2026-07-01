#pragma once

#include <HexEngine.Core/HexEngine.hpp>
#include <HexEngine.Core/Scene/ISceneCustomRenderer.hpp>

#include <memory>
#include <vector>

namespace HexEngine { class Material; }
class CitySimulationInterface;

class CitySimulationEditorToolPlugin final : public HexEngine::IEditorToolPlugin, public HexEngine::ISceneCustomRenderer
{
public:
	explicit CitySimulationEditorToolPlugin(CitySimulationInterface* citySimulationInterface);
	virtual ~CitySimulationEditorToolPlugin();

	virtual void OnCreateUI(HexEngine::MenuBar* menuBar) override;
	virtual void OnAssetExplorerCreateNew(HexEngine::ContextMenu* menu, HexEngine::ContextRoot* rootMenu, const fs::path& baseDir, HexEngine::FileSystem* fileSystem, std::function<void()> onAssetsCreated) override;
	virtual void OnMessage(HexEngine::Message* message, HexEngine::MessageListener* sender) override;
	virtual void RenderCustom(HexEngine::Scene* scene, HexEngine::Camera* camera, HexEngine::MeshRenderFlags renderFlags) override;

private:
	void AutoWireRoutineSim();   // tag buildings + place waypoints from the scene geometry
	void ShowRoadPainterDialog();
	void CloseRoadPainterDialog();
	bool ToggleRoadPainterActive();
	void HandleSceneViewportMouseDown(HexEngine::EditorSceneViewportMouseDownMessage* message);
	void HandleSceneViewportMouseMove(HexEngine::EditorSceneViewportMouseMoveMessage* message);
	bool MeasureStraightAsset(float& outSpacing, bool& outUsesXAxis) const;
	void PaintOrthogonalRun(const math::Vector3& worldPosition);
	void RebuildRoadPainterNetwork(float defaultHeight) const;
	void UpdateRoadPainterRendererRegistration();
	void RefreshRoadPainterPreview();
	void DestroyRoadPainterPreviewEntities();
	std::shared_ptr<HexEngine::Material> GetOrCreatePreviewMaterial();
	void CreateRoadPrefab(const fs::path& baseDir, HexEngine::FileSystem* fileSystem, const std::function<void()>& onAssetsCreated);
	void CreateVehiclePrefab(const fs::path& baseDir, HexEngine::FileSystem* fileSystem, const std::function<void()>& onAssetsCreated);

private:
	CitySimulationInterface* _citySimulationInterface = nullptr;
	bool _uiCreated = false;
	HexEngine::MenuBar::RootItem* _citySimMenuRoot = nullptr;
	HexEngine::Dialog* _roadPainterDialog = nullptr;
	fs::path _roadPainterStraightPath;
	fs::path _roadPainterCornerPath;
	fs::path _roadPainterCrossroadPath;
	fs::path _roadPainterTJunctionPath;
	bool _roadPainterEnabled = false;
	bool _roadPainterHasAnchor = false;
	float _roadPainterCellSize = 1.0f;
	// Multiplier applied to the auto-measured straight asset extent before using it as
	// cell-to-cell spacing. Lets the user tune the spacing if their mesh's AABB is
	// slightly larger or smaller than the visible road geometry (e.g. an asset that
	// authors caps/curbs slightly beyond the road surface ends up needing a value just
	// under 1.0 to make consecutive segments touch cleanly).
	float _roadPainterCellSizeScale = 1.0f;
	// Y offset applied to the initial road placement (the click that sets a fresh
	// anchor). The raycast hit lands ON the terrain; adding a small positive offset
	// lifts the road just above the ground to avoid Z-fighting with the terrain mesh
	// or to clear small surface variations. Subsequent clicks inherit the height from
	// the existing anchor cell, so the offset only needs to be applied once per paint
	// session.
	float _roadPainterInitialYOffset = 0.0f;
	// World-space (X, Z) of cell coord (0, 0). Snapping is "snap to a grid of step
	// cellSize starting at the origin" rather than "snap to multiples of cellSize from
	// world zero" so that the very first click can land on the editor's fine grid
	// (ed_translateSnapSize, typically 1.0) instead of the much coarser cell size
	// (typically 2-4 units). After the first click the origin is locked and all
	// subsequent cells snap at cellSize intervals from it. Mirrors the RoadNetwork
	// entity's position once that entity exists - if the user moves the network in
	// the editor, the origin re-syncs on the next mouse event.
	float _roadPainterOriginX = 0.0f;
	float _roadPainterOriginZ = 0.0f;
	// Whether to attach a static box collider to each painted road tile. 484+ static
	// colliders in a city grid is significant per-frame physics overhead (broad-phase
	// AABB updates, scene-query book-keeping) so leave this off until you actually have
	// vehicles/agents that need to interact with the roads.
	bool _roadPainterAddCollisions = false;
	bool _roadPainterUsesXAxis = false;
	int32_t _roadPainterYawQuarterTurns = 0;
	// Per-piece authoring-orientation offsets, in quarter turns (0/1/2/3).
	// The placement code expects corner / T-junction / crossroad meshes to be
	// authored with their connection sides facing specific WORLD-space directions
	// (corner: N+E, T: N+E+W) at identity rotation. If an asset was authored facing
	// a different direction the user dials in the offset here and the placement
	// composes it onto the computed mask rotation - no asset re-authoring needed.
	int32_t _roadPainterCornerYawQuarterTurns = 0;
	int32_t _roadPainterTJunctionYawQuarterTurns = 0;
	int32_t _roadPainterCrossroadYawQuarterTurns = 0;
	int32_t _roadPainterAnchorX = 0;
	int32_t _roadPainterAnchorZ = 0;
	float _roadPainterAnchorHeight = 0.0f;
	bool _roadPainterHasHover = false;
	int32_t _roadPainterHoverX = 0;
	int32_t _roadPainterHoverZ = 0;
	float _roadPainterHoverHeight = 0.0f;
	HexEngine::Scene* _roadPainterRegisteredScene = nullptr;
	std::vector<HexEngine::Entity*> _roadPainterPreviewEntities;
	std::shared_ptr<HexEngine::Material> _roadPainterPreviewMaterial;
	bool _roadPainterHasPreviewKey = false;
	int32_t _roadPainterPreviewAnchorX = 0;
	int32_t _roadPainterPreviewAnchorZ = 0;
	int32_t _roadPainterPreviewHoverX = 0;
	int32_t _roadPainterPreviewHoverZ = 0;
	// Origin used the last time we built a preview - the world position of cell (0,0).
	// Has to participate in the dedup key because before the first click the hover stays
	// at coord (0,0) while the origin slides with the cursor; without this field the
	// dedup would think "nothing changed" and the ghost would stick to its first position.
	float _roadPainterPreviewOriginX = 0.0f;
	float _roadPainterPreviewOriginZ = 0.0f;
};
