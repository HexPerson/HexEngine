

#include "World.hpp"
#include "../../../HexEngine.Core/Environment/IEnvironment.hpp"
#include "../../../HexEngine.Core/Entity/Component/MeshRenderer.hpp"
#include "../../Game.hpp"

namespace CityBuilder
{
	World::World(int32_t numChunks, int32_t numTiles, float tileSize) :
		_numChunks(numChunks),
		_numTiles(numTiles),
		_tileSize(tileSize)
	{
		SetName("World");
		SetLayer(HexEngine::Layer::StaticGeometry);
	}

	void World::Destroy()
	{
	}

	HexEngine::SoundEffect* ambience = nullptr;

	World* World::Create(int32_t numChunks, int32_t numTiles, float tileSize)
	{
		World* world = new World(numChunks, numTiles, tileSize);

		g_pGame->_world = world;

		HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->AddEntity(world);

		HexEngine::g_pEnv->_chunkManager->CreateTerrainChunks(numChunks, numTiles, tileSize);
		HexEngine::g_pEnv->_chunkManager->CreateDetails();
		HexEngine::g_pEnv->_chunkManager->CreateWater();

		ambience = (HexEngine::SoundEffect*)g_pEnv->_resourceSystem->LoadResource("Audio/tropical_ambience.wav");
		g_pEnv->_audioManager->Loop(ambience);

		return world;
	}

	HexEngine::Tile* World::GetTileByPosition(HexEngine::Entity* chunk, const math::Vector3& position)
	{
		return HexEngine::g_pEnv->_chunkManager->GetTileByPosition(chunk, position);
	}

	HexEngine::Tile* World::GetTileByPosition(const math::Vector3& position)
	{
		return HexEngine::g_pEnv->_chunkManager->GetTileByPosition(position);
	}

	HexEngine::Tile* World::GetTileByIndex(int32_t x, int32_t y)
	{
		/*int32_t halfSquares = _numSquares / 2;

		if (x < -(halfSquares-1) || x > (halfSquares))
			return nullptr;

		if (y < -(halfSquares-1) || y > (halfSquares))
			return nullptr;

		return &_tiles[x + (_numSquares / 2) - 1][y + (_numSquares / 2) - 1];*/

		return nullptr;
	}

	void World::Save(HexEngine::DiskFile* file)
	{
		file->Write(&_numChunks, sizeof(int32_t));
		file->Write(&_numTiles, sizeof(int32_t));
		file->Write(&_tileSize, sizeof(float));

		Entity::Save(file);
	}

	World* World::Load(HexEngine::DiskFile* file)
	{
		int32_t numChunks;
		int32_t numTiles;
		float tileSize;

		file->Read(&numChunks, sizeof(int32_t));
		file->Read(&numTiles, sizeof(int32_t));
		file->Read(&tileSize, sizeof(float));

		World* world = World::Create(numChunks, numTiles, tileSize);

		LoadBasicEntityData(file, world);

		return world;
	}
}