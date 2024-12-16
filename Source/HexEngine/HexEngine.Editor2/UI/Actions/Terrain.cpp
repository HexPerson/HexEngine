
#include "Terrain.hpp"
#include "../../Editor.hpp"
#include "../EditorUI.hpp"
#include <HexEngine.Core/GUI/Elements/MessageBox.hpp>

namespace HexEditor
{
	Terrain::Terrain(Element* parent, const Point& position, const Point& size) :
		Dialog(parent, position, size, L"Terrain Generator")
	{
		_widgetBase = new ComponentWidget(this, Point(10, 10), Point(size.x - 20, -1), L"Environment");
		//_shadowSettings = new ComponentWidget(this, Point(10, 200), Point(size.x - 20, -1), L"Shadow");
	}

	Terrain::~Terrain()
	{
	}

	Terrain* Terrain::CreateTerrainDialog(Element* parent, OnCompleted onCompletedAction)
	{
		uint32_t width, height;
		g_pEnv->GetScreenSize(width, height);

		int32_t centrex = width >> 1;
		int32_t centrey = height >> 1;

		const int32_t sizex = 800;
		const int32_t sizey = 480;

		Terrain* pm = new Terrain(parent, Point(centrex - sizex / 2, centrey - sizey / 2), Point(sizex, sizey));

		pm->_seed = new LineEdit(pm->_widgetBase, pm->_widgetBase->GetNextPos(), Point(sizex - 40, 18), L"Seed");
		DragFloat* heightScale = new DragFloat(pm->_widgetBase, pm->_widgetBase->GetNextPos(), Point(sizex - 40, 18), L"Height Scale", &pm->_heightScale, 0.1f, 100.0f, 0.1f);
		DragInt* gridSize = new DragInt(pm->_widgetBase, pm->_widgetBase->GetNextPos(), Point(sizex - 40, 18), L"Grid Size", &pm->_gridSize, 1, 1024, 1);
		DragFloat* chunkWidth = new DragFloat(pm->_widgetBase, pm->_widgetBase->GetNextPos(), Point(sizex - 40, 18), L"Chunk Width", &pm->_width, 1.0f, 1024.0f, 1.0f);
		DragFloat* uvScale = new DragFloat(pm->_widgetBase, pm->_widgetBase->GetNextPos(), Point(sizex - 40, 18), L"Texture Scale", &pm->_uvScale, 0.1f, 10.0f, 0.1f);
		DragInt* resolution = new DragInt(pm->_widgetBase, pm->_widgetBase->GetNextPos(), Point(sizex - 40, 18), L"Resolution", & pm->_resolution, 1, 512, 1);
		DragInt* lod = new DragInt(pm->_widgetBase, pm->_widgetBase->GetNextPos(), Point(sizex - 40, 18), L"Max LOD", &pm->_maxLod, 0, 3, 1);
		Checkbox* makeParent = new Checkbox(pm->_widgetBase, pm->_widgetBase->GetNextPos(), Point(sizex - 40, 18), L"Create Parent", &pm->_parent);
		Checkbox* makeColliders = new Checkbox(pm->_widgetBase, pm->_widgetBase->GetNextPos(), Point(sizex - 40, 18), L"Add Colliders", &pm->_makeColliders);
		Checkbox* useChunkingSystem = new Checkbox(pm->_widgetBase, pm->_widgetBase->GetNextPos(), Point(sizex - 40, 18), L"Enable Chunk System", &pm->_useChunkSystem);
		pm->_materialName = new LineEdit(pm->_widgetBase, pm->_widgetBase->GetNextPos(), Point(sizex - 40, 18), L"Material");
		pm->_materialName->SetValue(L"Materials/DesertTerrain.hmat");
		Button* generateBtn = new Button(pm->_widgetBase, pm->_widgetBase->GetNextPos(), Point(100, 25), L"Generate", std::bind(&Terrain::GenerateTerrain, pm));

		//Button* regen = new Button(pm->_widgetBase, pm->_widgetBase->GetNextPos(), Point(100, 25), L"Re", std::bind(&Terrain::GenerateTerrain, pm));
		


		pm->BringToFront();

		return pm;
	}

	bool Terrain::GenerateTerrain()
	{
		if (Material::Exists(_materialName->GetValue()) == false)
		{
			MessageBox::Info(L"Invalid Material", std::format(L"The material '{}' was not present on disk or in a loaded asset file, please check the file name and try again", _materialName->GetValue()));
			return false;
		}

		if (_created.size() > 0)
		{
			for (auto& ent : _created)
			{
				g_pEnv->_sceneManager->GetCurrentScene()->DestroyEntity(ent);
			}
			_created.clear();
		}

		g_pUIManager->GetInspector()->InspectEntity(nullptr);

		const auto seed = std::stoul(_seed->GetValue());// GetRandomInt();
		const int32_t gridSize = (int32_t)_gridSize;
		const int32_t halfGridSize = (gridSize / 2);
		const float width = _width;
		const float totalSize = width * gridSize;
		Entity* parentEnt = nullptr;

		Biome biome;
		biome.position = math::Vector3::Zero;
		biome.flattenMode = (int32_t)Biome::TerrainFlattenMode::Flatten;
		biome.radius = 2048.0f;
		biome.falloffStart = 400.0f;

		if (_useChunkSystem)
		{
			g_pEnv->_chunkManager->RemoveAllChunks(g_pEnv->_sceneManager->GetCurrentScene());
			g_pEnv->_chunkManager->CreateChunks(g_pEnv->_sceneManager->GetCurrentScene(), _width, _gridSize);
		}

		if (_parent)
		{
			parentEnt = g_pEnv->_sceneManager->GetCurrentScene()->CreateEntity("TerrainParent");
			_created.push_back(parentEnt);
		}

		

		Material* terrainMaterial = Material::Create(_materialName->GetValue());

		LOG_DEBUG("Creating a new heightmap terrain with seed %d", seed);

		//g_pEnv->_sceneManager->GetCurrentScene()->ClearTerrainParams();

		for (int32_t i = 0; i < gridSize; ++i)
		{
			for (int32_t j = 0; j < gridSize; ++j)
			{

				math::Vector3 position((float)(i - halfGridSize) * width, 0.0f, (float)(j - halfGridSize) * width);

				std::string ident = "Terrain_" + std::to_string(i) + "_" + std::to_string(j);

				int32_t resolution = _resolution;

				auto terrainEnt = g_pEnv->_sceneManager->GetCurrentScene()->CreateEntity(ident, position);

				for (int32_t k = 0; k <= _maxLod; ++k)
				{
					HeightMapGenerationParams params;

					params.seed = seed;
					params.resolution = resolution;
					params.width = _width;
					params.position = position;
					params.heightScale = _heightScale;

					//params.setMinHeight = true;
					//params.minimumHeight = 0.0f;

					params.biomes.push_back(biome);

					auto heightMap = GenerateHeightMap(params);

					if (heightMap.size() == 0)
					{
						LOG_CRIT("A heightmap failed to generate correctly");
						return false;
					}

					HexEngine::TerrainGenerationParams terrainParams;
					terrainParams.ident = ident;

					// append the LOD level to the string
					if(_maxLod > 0)
						terrainParams.ident += "_LOD" + std::to_string(k);

					terrainParams.resolution = params.resolution;
					terrainParams.position = params.position;
					terrainParams.heightMap = heightMap;
					terrainParams.uvScale = _uvScale;
					terrainParams.width = params.width;
					terrainParams.createInstance = true;

					auto mesh = HexEngine::CreateTerrain(terrainParams);

					mesh->CreateInstance();

					resolution /= 2;

					auto meshRenderer = terrainEnt->AddComponent<StaticMeshComponent>();
					meshRenderer->SetMesh(mesh);
					meshRenderer->SetMaterial(0, terrainMaterial);

					if (_makeColliders)
					{
						auto rb = terrainEnt->AddComponent<RigidBody>();
						rb->AddTriangleMeshCollider(mesh, true);
					}
				}

				terrainEnt->SetParent(parentEnt);

				if(_parent == false)
					_created.push_back(terrainEnt);
			}
		}

		//DeleteMe();

		g_pUIManager->GetEntityTreeList()->RefreshList();

		return true;
	}
}