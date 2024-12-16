
#pragma once

#include "../../../HexEngine.Core/Entity/ChunkManager.hpp"
//#include "../../../HexEngine.Core/Audio/SoundEffect.hpp"

namespace CityBuilder
{
	// {B45ADE1A-B154-4A15-A08E-901DD62C5D2F}
	DEFINE_HEX_GUID(WorldGUID,
		0xb45ade1a, 0xb154, 0x4a15, 0xa0, 0x8e, 0x90, 0x1d, 0xd6, 0x2c, 0x5d, 0x2f);	

	class World : public HexEngine::Entity
	{
	public:
		DEFINE_OBJECT_GUID(World);

		World(int32_t numChunks, int32_t numTiles, float tileSize);

		static World* Create(int32_t numChunks, int32_t numTiles, float tileSize);
		
		virtual void Destroy() override;

		HexEngine::Tile* GetTileByPosition(HexEngine::Entity* chunk, const math::Vector3& position);
		HexEngine::Tile* GetTileByPosition(const math::Vector3& position);
		HexEngine::Tile* GetTileByIndex(int32_t x, int32_t y);

		float GetTileSize() { return _tileSize; }

		virtual void Save(HexEngine::DiskFile* file) override;
		IMPLEMENT_LOADER(World);

	private:
		void CreateTrees();

	private:
		int32_t _numChunks;
		int32_t _numTiles;
		float _tileSize;

		//HexEngine::SoundEffect* _ambience;

		std::vector<HexEngine::Chunk*> _chunks;
	};
}
