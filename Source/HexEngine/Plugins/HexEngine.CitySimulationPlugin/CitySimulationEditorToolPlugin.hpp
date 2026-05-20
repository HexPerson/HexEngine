#pragma once

#include <HexEngine.Core/HexEngine.hpp>
#include <HexEngine.Core/Scene/ISceneCustomRenderer.hpp>

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
	void ShowRoadPainterDialog();
	void CloseRoadPainterDialog();
	bool ToggleRoadPainterActive();
	void HandleSceneViewportMouseDown(HexEngine::EditorSceneViewportMouseDownMessage* message);
	void HandleSceneViewportMouseMove(HexEngine::EditorSceneViewportMouseMoveMessage* message);
	bool MeasureStraightAsset(float& outSpacing, bool& outUsesXAxis) const;
	void PaintOrthogonalRun(const math::Vector3& worldPosition);
	void RebuildRoadPainterNetwork(float defaultHeight) const;
	void UpdateRoadPainterRendererRegistration();
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
	// Whether to attach a static box collider to each painted road tile. 484+ static
	// colliders in a city grid is significant per-frame physics overhead (broad-phase
	// AABB updates, scene-query book-keeping) so leave this off until you actually have
	// vehicles/agents that need to interact with the roads.
	bool _roadPainterAddCollisions = false;
	bool _roadPainterUsesXAxis = false;
	int32_t _roadPainterYawQuarterTurns = 0;
	int32_t _roadPainterAnchorX = 0;
	int32_t _roadPainterAnchorZ = 0;
	float _roadPainterAnchorHeight = 0.0f;
	bool _roadPainterHasHover = false;
	int32_t _roadPainterHoverX = 0;
	int32_t _roadPainterHoverZ = 0;
	float _roadPainterHoverHeight = 0.0f;
	HexEngine::Scene* _roadPainterRegisteredScene = nullptr;
};
