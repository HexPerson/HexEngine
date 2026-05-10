#include "VolumetricTerrainEditorTool.hpp"
#include "VolumetricTerrainInterface.hpp"
#include "Terrain/VolumetricTerrainComponent.hpp"
#include <climits>

namespace HexEngine::VolumetricTerrain
{
	VolumetricTerrainEditorTool::VolumetricTerrainEditorTool(VolumetricTerrainInterface* terrainInterface) :
		_terrainInterface(terrainInterface)
	{
	}

	VolumetricTerrainEditorTool::~VolumetricTerrainEditorTool()
	{
	}

	void VolumetricTerrainEditorTool::OnCreateUI(MenuBar* menuBar)
	{
		if (_uiCreated || menuBar == nullptr)
			return;

		_uiCreated = true;

		auto* root = new MenuBar::RootItem;
		root->name = L"Terrain";
		menuBar->AddRootItem(root);

		auto* create = new MenuBar::Item;
		create->name = L"Add Volumetric Terrain";
		create->action = [this](MenuBar::Item*)
			{
				OpenCreateTerrainDialog();
			};
		menuBar->AddSubItem(root, create);
	}

	void VolumetricTerrainEditorTool::OpenCreateTerrainDialog()
	{
		if (_terrainInterface == nullptr)
			return;

		const int32_t dlgWidth = 480;
		const int32_t dlgHeight = 460;
		Dialog* dlg = new Dialog(
			g_pEnv->GetUIManager().GetRootElement(),
			Point::GetScreenCenterWithOffset(-dlgWidth / 2, -dlgHeight / 2),
			Point(dlgWidth, dlgHeight),
			L"Add Volumetric Terrain");

		auto* widget = new ComponentWidget(dlg, Point(10, 10), Point(dlgWidth - 20, -1), L"Volumetric Terrain");

		auto creationParams = std::make_shared<SdfTerrainGenerationParams>();
		auto* seed = new DragInt(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Seed", reinterpret_cast<int32_t*>(&creationParams->seed), 0, INT_MAX, 1);
		auto* res = new DragInt(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Voxel Resolution", &creationParams->chunkResolution, 8, 64, 1);
		auto* chunkSize = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Chunk Size", &creationParams->chunkWorldSize, 32.0f, 1024.0f, 1.0f);
		auto* chunksX = new DragInt(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Chunks X", &creationParams->chunksX, 1, 32, 1);
		auto* chunksY = new DragInt(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Chunks Y", &creationParams->chunksY, 1, 16, 1);
		auto* chunksZ = new DragInt(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Chunks Z", &creationParams->chunksZ, 1, 32, 1);
		auto* heightScale = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Height Scale", &creationParams->terrainHeightScale, 0.01f, 8.0f, 0.01f, 3);
		auto* caveFreq = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Cave Frequency", &creationParams->caveFrequency, 0.0001f, 1.0f, 0.001f, 4);
		auto* caveStrength = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Cave Strength", &creationParams->caveStrength, 0.0f, 256.0f, 0.25f, 2);
		auto* seedHeight = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Seed from HeightMap Generator", &creationParams->seedFromHeightMap);

		new Button(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 24), L"Create",
			[this, dlg, creationParams](Button*) -> bool
			{
				auto* scene = g_pEnv->_sceneManager->GetCurrentScene().get();
				if (scene == nullptr)
					return false;

				Entity* terrainEntity = _terrainInterface->CreateVolumetricTerrainEntity(scene, math::Vector3::Zero);
				if (terrainEntity == nullptr)
					return false;

				auto* component = terrainEntity->GetComponent<VolumetricTerrainComponent>();
				if (component != nullptr)
				{
					component->GetGenerationParams() = *creationParams;
					component->InitializeTerrain();
				}

				dlg->DeleteMe();
				return true;
			});

		(void)seed; (void)res; (void)chunkSize; (void)chunksX; (void)chunksY; (void)chunksZ;
		(void)heightScale; (void)caveFreq; (void)caveStrength; (void)seedHeight;
	}

	void VolumetricTerrainEditorTool::OnAssetExplorerCreateNew(ContextMenu* menu, ContextRoot* rootMenu, const fs::path& baseDir, FileSystem* fileSystem, std::function<void()> onAssetsCreated)
	{
		(void)menu;
		(void)rootMenu;
		(void)baseDir;
		(void)fileSystem;
		(void)onAssetsCreated;
	}

	void VolumetricTerrainEditorTool::OnMessage(Message* message, MessageListener* sender)
	{
		(void)message;
		(void)sender;
	}
}


