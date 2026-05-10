#include "SdfTerrainGenerator.hpp"

namespace HexEngine::VolumetricTerrain
{
	void SdfTerrainGenerationParams::Serialize(json& data, JsonFile* file)
	{
		file->Serialize(data, "seed", seed);
		file->Serialize(data, "chunkResolution", chunkResolution);
		file->Serialize(data, "collisionResolution", collisionResolution);
		file->Serialize(data, "chunkWorldSize", chunkWorldSize);
		file->Serialize(data, "chunksX", chunksX);
		file->Serialize(data, "chunksY", chunksY);
		file->Serialize(data, "chunksZ", chunksZ);
		file->Serialize(data, "terrainHeightScale", terrainHeightScale);
		file->Serialize(data, "surfaceNoiseFrequency", surfaceNoiseFrequency);
		file->Serialize(data, "surfaceNoiseStrength", surfaceNoiseStrength);
		file->Serialize(data, "caveFrequency", caveFrequency);
		file->Serialize(data, "caveStrength", caveStrength);
		file->Serialize(data, "caveThreshold", caveThreshold);
		file->Serialize(data, "uvScale", uvScale);
		file->Serialize(data, "seedFromHeightMap", seedFromHeightMap);
	}

	void SdfTerrainGenerationParams::Deserialize(json& data, JsonFile* file)
	{
		file->Deserialize(data, "seed", seed);
		file->Deserialize(data, "chunkResolution", chunkResolution);
		file->Deserialize(data, "collisionResolution", collisionResolution);
		file->Deserialize(data, "chunkWorldSize", chunkWorldSize);
		file->Deserialize(data, "chunksX", chunksX);
		file->Deserialize(data, "chunksY", chunksY);
		file->Deserialize(data, "chunksZ", chunksZ);
		file->Deserialize(data, "terrainHeightScale", terrainHeightScale);
		file->Deserialize(data, "surfaceNoiseFrequency", surfaceNoiseFrequency);
		file->Deserialize(data, "surfaceNoiseStrength", surfaceNoiseStrength);
		file->Deserialize(data, "caveFrequency", caveFrequency);
		file->Deserialize(data, "caveStrength", caveStrength);
		file->Deserialize(data, "caveThreshold", caveThreshold);
		file->Deserialize(data, "uvScale", uvScale);
		file->Deserialize(data, "seedFromHeightMap", seedFromHeightMap);
		collisionResolution = std::clamp(collisionResolution, 4, std::max(4, chunkResolution));
	}

	SdfTerrainGenerator::SdfTerrainGenerator(const SdfTerrainGenerationParams& params) :
		_params(params)
	{
		_surfaceNoise.SetSeed(static_cast<int32_t>(params.seed));
		_surfaceNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
		_surfaceNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
		_surfaceNoise.SetFractalOctaves(5);
		_surfaceNoise.SetFrequency(params.surfaceNoiseFrequency);

		_detailNoise.SetSeed(static_cast<int32_t>(params.seed ^ 0x78ABF31u));
		_detailNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
		_detailNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
		_detailNoise.SetFractalOctaves(3);
		_detailNoise.SetFrequency(params.surfaceNoiseFrequency * 3.0f);

		_caveNoise.SetSeed(static_cast<int32_t>(params.seed ^ 0xAD12931u));
		_caveNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
		_caveNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
		_caveNoise.SetFractalOctaves(4);
		_caveNoise.SetFrequency(params.caveFrequency);
	}

	float SdfTerrainGenerator::SampleDensity(const math::Vector3& worldPosition, float seedHeight) const
	{
		const float baseDensity = worldPosition.y - seedHeight;

		const float detail = _detailNoise.GetNoise(worldPosition.x, worldPosition.z) * (_params.surfaceNoiseStrength * 0.3f);
		const float caveSample = _caveNoise.GetNoise(worldPosition.x, worldPosition.y, worldPosition.z);
		float cave = 0.0f;

		// Keep cave carving below the terrain surface to avoid layered artifacts on top.
		const float depthBelowSurface = seedHeight - worldPosition.y;
		if (depthBelowSurface > 0.0f && caveSample > _params.caveThreshold)
		{
			const float caveMask = (caveSample - _params.caveThreshold) / std::max(0.001f, 1.0f - _params.caveThreshold);
			const float depthMask = std::clamp((depthBelowSurface - 6.0f) / 40.0f, 0.0f, 1.0f);
			cave = caveMask * _params.caveStrength * depthMask;
		}

		return baseDensity - detail + cave;
	}

	std::vector<float> SdfTerrainGenerator::GenerateHeightSeedMap(const math::Vector3& chunkCenter, int32_t resolution, float width) const
	{
		if (_params.seedFromHeightMap)
		{
			HeightMapGenerationParams legacy;
			legacy.seed = _params.seed;
			legacy.resolution = static_cast<uint32_t>(std::max(2, resolution));
			legacy.width = width;
			legacy.position = chunkCenter;
			legacy.heightScale = _params.terrainHeightScale;
			return GenerateHeightMap(legacy);
		}

		std::vector<float> map;
		const int32_t points = std::max(2, resolution) + 1;
		map.resize(static_cast<size_t>(points * points));

		const float halfWidth = width * 0.5f;
		for (int32_t z = 0; z < points; ++z)
		{
			for (int32_t x = 0; x < points; ++x)
			{
				const float fx = -halfWidth + (width * static_cast<float>(x) / static_cast<float>(points - 1));
				const float fz = -halfWidth + (width * static_cast<float>(z) / static_cast<float>(points - 1));
				const float s = _surfaceNoise.GetNoise(chunkCenter.x + fx, chunkCenter.z + fz);
				map[static_cast<size_t>(z * points + x)] = (s * _params.surfaceNoiseStrength) * _params.terrainHeightScale;
			}
		}

		return map;
	}

	float SdfTerrainGenerator::SampleHeightFromMap(const std::vector<float>& heightMap, int32_t resolution, int32_t x, int32_t z) const
	{
		const int32_t points = std::max(2, resolution) + 1;
		const int32_t sx = std::clamp(x, 0, points - 1);
		const int32_t sz = std::clamp(z, 0, points - 1);
		const size_t idx = static_cast<size_t>(sz * points + sx);
		return idx < heightMap.size() ? heightMap[idx] : 0.0f;
	}
}
