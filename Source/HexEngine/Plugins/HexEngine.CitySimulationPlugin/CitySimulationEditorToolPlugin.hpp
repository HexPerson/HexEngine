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
	std::vector<HexEngine::Entity*> _roadPainterPreviewEntities;
	std::shared_ptr<HexEngine::Material> _roadPainterPreviewMaterial;
	bool _roadPainterHasPreviewKey = false;
	int32_t _roadPainterPreviewAnchorX = 0;
	int32_t _roadPainterPreviewAnchorZ = 0;
	int32_t _roadPainterPreviewHoverX = 0;
	int32_t _roadPainterPreviewHoverZ = 0;
};
