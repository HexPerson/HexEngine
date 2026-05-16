#pragma once

#include <HexEngine.Core/HexEngine.hpp>
#include <fastnoiselite/Cpp/FastNoiseLite.h>

namespace HexEngine::VolumetricTerrain
{
	struct SdfTerrainGenerationParams
	{
		uint32_t seed = 1337;
		int32_t chunkResolution = 32;
		int32_t collisionResolution = 16;
		float chunkWorldSize = 96.0f;
		int32_t chunksX = 3;
		int32_t chunksY = 2;
		int32_t chunksZ = 3;
		float terrainHeightScale = 1.0f;
		float surfaceNoiseFrequency = 0.0012f;
		float surfaceNoiseStrength = 0.0f;
		float caveFrequency = 0.01f;
		float caveStrength = 6.0f;
		float caveThreshold = 0.8f;
		float uvScale = 0.03f;
		// Legacy heightmap seeding remains available, but defaults to off because
		// it tends to produce chunk-scale planar "slope tiles" at volumetric defaults.
		bool seedFromHeightMap = false;

		void Serialize(json& data, JsonFile* file);
		void Deserialize(json& data, JsonFile* file);
	};

	class SdfTerrainGenerator
	{
	public:
		explicit SdfTerrainGenerator(const SdfTerrainGenerationParams& params);

		float SampleDensity(const math::Vector3& worldPosition, float seedHeight) const;
		std::vector<float> GenerateHeightSeedMap(const math::Vector3& chunkCenter, int32_t resolution, float width) const;
		float SampleHeightFromMap(const std::vector<float>& heightMap, int32_t resolution, int32_t x, int32_t z) const;

	private:
		SdfTerrainGenerationParams _params;
		mutable FastNoiseLite _surfaceNoise;
		mutable FastNoiseLite _detailNoise;
		mutable FastNoiseLite _caveNoise;
	};
}
