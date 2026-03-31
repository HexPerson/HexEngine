#include "NavMeshTool.hpp"
#include "../../Editor.hpp"
#include "../EditorUI.hpp"

namespace HexEditor
{
	NavMeshTool::NavMeshTool(Element* parent, const HexEngine::Point& position, const HexEngine::Point& size) :
		Dialog(parent, position, size, L"Navigation Mesh")
	{
		_params = BuildDefaultParams();
		_settings = new HexEngine::ComponentWidget(this, HexEngine::Point(10, 10), HexEngine::Point(size.x - 20, -1), L"Recast Build Settings");
	}

	NavMeshTool::~NavMeshTool()
	{
	}

	HexEngine::INavMeshProvider::NavMeshCreationParams NavMeshTool::BuildDefaultParams()
	{
		HexEngine::INavMeshProvider::NavMeshCreationParams params = {};

		params.cs = 16.0f;
		params.ch = 8.0f;
		params.walkableSlopeAngle = 45.0f;
		params.walkableClimb = 6;
		params.walkableHeight = 12;
		params.walkableRadius = 2;
		params.minRegionArea = 8;
		params.mergeRegionArea = 20;
		params.maxEdgeLen = 12;
		params.maxSimplificationError = 1.3f;
		params.maxVertsPerPoly = 6;
		params.detailSampleDist = 6.0f;
		params.detailSampleMaxError = 1.0f;
		params.borderSize = params.walkableRadius + 3;
		params.width = 0;
		params.height = 0;
		params.tileSize = 0;

		params.bmin[0] = params.bmin[1] = params.bmin[2] = 0.0f;
		params.bmax[0] = params.bmax[1] = params.bmax[2] = 0.0f;

		return params;
	}

	NavMeshTool* NavMeshTool::CreateEditorDialog(Element* parent)
	{
		uint32_t width = 0;
		uint32_t height = 0;
		HexEngine::g_pEnv->GetScreenSize(width, height);

		const int32_t sizeX = 460;
		const int32_t sizeY = 600;

		auto* dialog = new NavMeshTool(
			parent,
			HexEngine::Point((int32_t)width / 2 - sizeX / 2, (int32_t)height / 2 - sizeY / 2),
			HexEngine::Point(sizeX, sizeY));

		const int32_t controlWidth = sizeX - 40;

		new HexEngine::DragFloat(dialog->_settings, dialog->_settings->GetNextPos(), HexEngine::Point(controlWidth, 18), L"Cell Size", &dialog->_params.cs, 0.1f, 256.0f, 0.1f, 2);
		new HexEngine::DragFloat(dialog->_settings, dialog->_settings->GetNextPos(), HexEngine::Point(controlWidth, 18), L"Cell Height", &dialog->_params.ch, 0.1f, 128.0f, 0.1f, 2);
		new HexEngine::DragFloat(dialog->_settings, dialog->_settings->GetNextPos(), HexEngine::Point(controlWidth, 18), L"Walkable Slope", &dialog->_params.walkableSlopeAngle, 0.0f, 89.0f, 1.0f, 1);
		new HexEngine::DragInt(dialog->_settings, dialog->_settings->GetNextPos(), HexEngine::Point(controlWidth, 18), L"Walkable Height", &dialog->_params.walkableHeight, 1, 255, 1);
		new HexEngine::DragInt(dialog->_settings, dialog->_settings->GetNextPos(), HexEngine::Point(controlWidth, 18), L"Walkable Climb", &dialog->_params.walkableClimb, 0, 255, 1);
		new HexEngine::DragInt(dialog->_settings, dialog->_settings->GetNextPos(), HexEngine::Point(controlWidth, 18), L"Walkable Radius", &dialog->_params.walkableRadius, 0, 255, 1);
		new HexEngine::DragInt(dialog->_settings, dialog->_settings->GetNextPos(), HexEngine::Point(controlWidth, 18), L"Border Size", &dialog->_params.borderSize, 0, 255, 1);
		new HexEngine::DragInt(dialog->_settings, dialog->_settings->GetNextPos(), HexEngine::Point(controlWidth, 18), L"Min Region Area", &dialog->_params.minRegionArea, 0, 65535, 1);
		new HexEngine::DragInt(dialog->_settings, dialog->_settings->GetNextPos(), HexEngine::Point(controlWidth, 18), L"Merge Region Area", &dialog->_params.mergeRegionArea, 0, 65535, 1);
		new HexEngine::DragInt(dialog->_settings, dialog->_settings->GetNextPos(), HexEngine::Point(controlWidth, 18), L"Max Edge Length", &dialog->_params.maxEdgeLen, 0, 4096, 1);
		new HexEngine::DragFloat(dialog->_settings, dialog->_settings->GetNextPos(), HexEngine::Point(controlWidth, 18), L"Max Simplification Error", &dialog->_params.maxSimplificationError, 0.0f, 16.0f, 0.05f, 3);
		new HexEngine::DragInt(dialog->_settings, dialog->_settings->GetNextPos(), HexEngine::Point(controlWidth, 18), L"Max Verts Per Poly", &dialog->_params.maxVertsPerPoly, 3, 12, 1);
		new HexEngine::DragFloat(dialog->_settings, dialog->_settings->GetNextPos(), HexEngine::Point(controlWidth, 18), L"Detail Sample Dist", &dialog->_params.detailSampleDist, 0.0f, 64.0f, 0.1f, 2);
		new HexEngine::DragFloat(dialog->_settings, dialog->_settings->GetNextPos(), HexEngine::Point(controlWidth, 18), L"Detail Sample Max Error", &dialog->_params.detailSampleMaxError, 0.0f, 32.0f, 0.1f, 2);

		new HexEngine::Button(dialog->_settings, dialog->_settings->GetNextPos(), HexEngine::Point(210, 25), L"Build Scene NavMesh", std::bind(&NavMeshTool::BuildSceneNavMesh, dialog));
		new HexEngine::Button(dialog->_settings, dialog->_settings->GetNextPos(), HexEngine::Point(210, 25), L"Rebuild Scene NavMesh", std::bind(&NavMeshTool::RebuildSceneNavMesh, dialog));
		new HexEngine::Button(dialog->_settings, dialog->_settings->GetNextPos(), HexEngine::Point(210, 25), L"Build Chunk NavMeshes", std::bind(&NavMeshTool::BuildChunkNavMeshes, dialog));
		new HexEngine::Button(dialog->_settings, dialog->_settings->GetNextPos(), HexEngine::Point(210, 25), L"Rebuild Chunk NavMeshes", std::bind(&NavMeshTool::RebuildChunkNavMeshes, dialog));

		dialog->BringToFront();
		return dialog;
	}

	bool NavMeshTool::BuildSceneNavMesh()
	{
		auto* navProvider = HexEngine::g_pEnv->_navMeshProvider;
		auto* scene = HexEngine::g_pEnv->_sceneManager->GetCurrentScene().get();

		if (navProvider == nullptr || scene == nullptr)
		{
			LOG_WARN("Cannot build scene navmesh because the nav provider or scene is unavailable.");
			return false;
		}

		if (_params.mergeRegionArea < _params.minRegionArea)
		{
			_params.mergeRegionArea = _params.minRegionArea;
		}
		if (_params.borderSize < (_params.walkableRadius + 3))
		{
			_params.borderSize = _params.walkableRadius + 3;
		}

		HexEngine::NavMeshId navMeshId = 0;
		if (!navProvider->CreateNavMeshForScene(scene, _params, &navMeshId))
		{
			LOG_CRIT("Scene navmesh build failed.");
			return false;
		}

		_sceneNavMeshId = navMeshId;
		_hasSceneNavMesh = true;
		LOG_INFO("Scene navmesh created successfully. NavMeshId=%u", navMeshId);
		return true;
	}

	bool NavMeshTool::RebuildSceneNavMesh()
	{
		if (!_hasSceneNavMesh)
		{
			LOG_WARN("No scene navmesh id is tracked yet. Build the scene navmesh first.");
			return false;
		}

		if (HexEngine::g_pEnv->_navMeshProvider->RebuildMesh(_sceneNavMeshId))
		{
			LOG_INFO("Scene navmesh rebuilt. NavMeshId=%u", _sceneNavMeshId);
			return true;
		}

		LOG_CRIT("Scene navmesh rebuild failed. NavMeshId=%u", _sceneNavMeshId);
		return false;
	}

	bool NavMeshTool::BuildChunkNavMeshes()
	{
		auto* navProvider = HexEngine::g_pEnv->_navMeshProvider;
		auto* scene = HexEngine::g_pEnv->_sceneManager->GetCurrentScene().get();
		if (navProvider == nullptr || scene == nullptr)
		{
			LOG_WARN("Cannot build chunk navmeshes because the nav provider or scene is unavailable.");
			return false;
		}

		HexEngine::ChunkData chunkData = {};
		if (!HexEngine::g_pEnv->_chunkManager->GetChunkData(scene, &chunkData))
		{
			LOG_WARN("No active chunks found for the current scene. Build chunks first.");
			return false;
		}

		if (_params.mergeRegionArea < _params.minRegionArea)
		{
			_params.mergeRegionArea = _params.minRegionArea;
		}
		if (_params.borderSize < (_params.walkableRadius + 3))
		{
			_params.borderSize = _params.walkableRadius + 3;
		}

		_chunkNavMeshIds.clear();
		int32_t builtCount = 0;

		HexEngine::g_pEnv->_chunkManager->ForEachChunkExecute(
			chunkData,
			[this, navProvider, scene, &builtCount](int32_t, int32_t, HexEngine::Chunk* chunk)
			{
				if (chunk == nullptr)
					return;

				HexEngine::NavMeshId navMeshId = 0;
				if (navProvider->CreateNavMeshForChunk(scene, chunk, _params, &navMeshId))
				{
					_chunkNavMeshIds.push_back(navMeshId);
					++builtCount;
				}
			});

		LOG_INFO("Built %d chunk navmeshes.", builtCount);
		return builtCount > 0;
	}

	bool NavMeshTool::RebuildChunkNavMeshes()
	{
		if (_chunkNavMeshIds.empty())
		{
			LOG_WARN("No chunk navmesh ids are tracked yet. Build chunk navmeshes first.");
			return false;
		}

		int32_t rebuiltCount = 0;
		for (auto meshId : _chunkNavMeshIds)
		{
			if (HexEngine::g_pEnv->_navMeshProvider->RebuildMesh(meshId))
			{
				++rebuiltCount;
			}
		}

		LOG_INFO("Rebuilt %d/%d chunk navmeshes.", rebuiltCount, (int32_t)_chunkNavMeshIds.size());
		return rebuiltCount > 0;
	}
}
