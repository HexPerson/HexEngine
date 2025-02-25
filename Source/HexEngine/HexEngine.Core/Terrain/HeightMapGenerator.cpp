
#include "HeightMapGenerator.hpp"
#include "../HexEngine.hpp"
#include <fastnoiselite/FastNoiseLite.h>
#include "../Math/easing.h"

namespace HexEngine
{
#define SHIFT_NOISE(n) ((n * 0.5f) + 0.5f)
	float GenerateNoise(FastNoiseLite& noise, float frequency, int octaves, const math::Vector3& position/*, float scale*/)
	{
		
		noise.SetFrequency(frequency);
		noise.SetFractalOctaves(octaves);

		return /*SHIFT_NOISE(*/noise.GetNoise(position.x, position.z/*, position.y*/);// *scale;
	}

	//float GenerateSmallDetailNoise(uint32_t seed, const math::Vector3& position, float scale)
	//{
	//	static FastNoiseLite noise;
	//	noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
	//	noise.SetFrequency(0.06f);
	//	noise.SetFractalOctaves(2);
	//	noise.SetSeed(seed /*^ 0x12098234*/);

	//	return noise.GetNoise(position.x, position.z) * scale;
	//}

	//float GenerateLargeDetailNoise(uint32_t seed, const math::Vector3& position, float scale)
	//{
	//	static FastNoiseLite noise;
	//	noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
	//	noise.SetFrequency(0.008f);
	//	noise.SetFractalOctaves(4);
	//	noise.SetSeed(seed /*^ 0xab7a46ab*/);

	//	return noise.GetNoise(position.x, position.z) * (scale * 50.0f);
	//}

	//float GenerateMegaDetailNoise(uint32_t seed, const math::Vector3& position, float scale)
	//{
	//	static FastNoiseLite noise;
	//	noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
	//	noise.SetFrequency(0.0014f);
	//	noise.SetFractalOctaves(8);
	//	noise.SetSeed(seed /*^ 0xff730aee*/);

	//	return noise.GetNoise(position.x, position.z) *(scale * 25.0f);
	//}


	float GenerateTerrainHeight(uint32_t seed, const math::Vector3& position, float scale)
	{
		FastNoiseLite noiseGen;
		noiseGen.SetSeed(seed);
		noiseGen.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
		//noiseGen.SetFractalType(FastNoiseLite::FractalType::FractalType_Ridged);
		//noiseGen.SetFractalGain(0.9f);
		//noiseGen.SetFractalOctaves(2);

		float noise = 0.0f;

		// big mountains above ground
		noise = GenerateNoise(noiseGen, 0.00005f, 4, position); seed += 1340912830; seed ^= 654351351; noiseGen.SetSeed(seed);
		noise += GenerateNoise(noiseGen, 0.0003f, 2, position); seed += 1340912830; seed ^= 654351351; noiseGen.SetSeed(seed);

		noise += GenerateNoise(noiseGen, 0.0004f, 2, position); seed += 78673; seed ^= 7486453; noiseGen.SetSeed(seed);
		noise += GenerateNoise(noiseGen, 0.00001f, 2, position) * 10.6f; seed += 1340912830; seed ^= 645453; noiseGen.SetSeed(seed);
		noise += GenerateNoise(noiseGen, 0.004f, 2, position) * 0.02f; seed += 123132; seed ^= 91293741; noiseGen.SetSeed(seed);
		//noise += GenerateNoise(noiseGen, 0.2f, 1, position); seed += 86782; seed ^= 976444; noiseGen.SetSeed(seed);

		//noiseGen.SetNoiseType(FastNoiseLite::NoiseType_Cellular);

		//noise += GenerateNoise(noiseGen, 0.0009f, 5, position, scale * 40.0f) ; seed += 4567864; seed ^= 456456; noiseGen.SetSeed(seed);

		/*float e = noise / (70.0f + 31.30f + 2.35f + 20.0f);

		noise = pow(noise, 1.2f);*/

		/*FastNoiseLite noiseGen2;
		noiseGen2.SetSeed(seed);
		noiseGen2.SetNoiseType(FastNoiseLite::NoiseType_Cellular);
		noiseGen2.SetFrequency(0.0003f);
		noiseGen2.SetCellularDistanceFunction(FastNoiseLite::CellularDistanceFunction::CellularDistanceFunction_EuclideanSq);
		noiseGen2.SetCellularReturnType(FastNoiseLite::CellularReturnType::CellularReturnType_Distance);
		noiseGen2.SetCellularJitter(1.1f);

		float nx = position.x, ny = position.z;
		noiseGen2.SetDomainWarpType(FastNoiseLite::DomainWarpType::DomainWarpType_OpenSimplex2);
		noiseGen2.SetDomainWarpAmp(110.0f);

		noiseGen2.DomainWarp(nx, ny);

		float mult = 1.0f - SHIFT_NOISE(noiseGen2.GetNoise(nx, ny));

		noise *= mult;*/
		//float noise = 
		//	GenerateSmallDetailNoise(seed, position, scale) +
		//	GenerateLargeDetailNoise(seed, position, scale) +
		//	(GenerateMegaDetailNoise(seed, position, scale) /*+ GenerateInsaneDetailNoise(position, scale)*/);

		//if (noise < 0.0f) noise *= -1.0f;

		return  noise * scale * 100.0f;
	}

	std::vector<float> HEX_API GenerateHeightMap(const HeightMapGenerationParams& params)
	{
		std::vector<float> heightMap;

		float halfWidth = params.width / 2.0f;
		float dx = params.width / ((float)params.resolution);
		float dz = params.width / ((float)params.resolution);

		float lowest = FLT_MAX;
		float highest = -FLT_MAX;					

		for (uint32_t i = 0; i < params.resolution+1; ++i)
		{
			float z = halfWidth - i * dz;

			for (uint32_t j = 0; j < params.resolution + 1; ++j)
			{
				float x = -halfWidth + j * dx;

				math::Vector3 position(x + params.position.x, 0.0f, z + params.position.z);

				float y = GenerateTerrainHeight(params.seed, position, params.heightScale);

				if (params.setMinHeight)
				{
					if (y < params.minimumHeight + 200.0f)
					{
						auto easingFunction = getEasingFunction(EaseInCubic);

						float p = easingFunction(std::clamp((y - params.minimumHeight) / 200.0f, 0.0f, 1.0f));

						y = max(params.minimumHeight, std::lerp(params.minimumHeight, y, p));
					}
				}

				for (auto& biome : params.biomes)
				{
					math::Vector3 hyp = biome.position - position;

					if (float dist = sqrt(hyp.x * hyp.x + hyp.z * hyp.z); dist <= biome.radius)
					{
						auto easingFunction = getEasingFunction(EaseInOutExpo);

						float perc = easingFunction(std::clamp(((dist - biome.falloffStart) / (biome.radius - biome.falloffStart)/*biome.falloffRange*/), 0.2f, 1.0f));

						if (biome.flattenMode == (int32_t)Biome::TerrainFlattenMode::Additive)
							y = std::lerp(biome.position.y + y, y, perc);
						else if (biome.flattenMode == (int32_t)Biome::TerrainFlattenMode::Flatten)
							y = std::lerp(biome.position.y, y, perc);
					}
				}

				// weight the height based on the distance to the edhe
				if (params.edgeHeight != -1.0f)
				{
					float distanceToEdge = (position - math::Vector3::Zero).Length();

					if (distanceToEdge >= params.edgeWidth)
					{
						float distancePercentage = std::clamp((distanceToEdge - params.edgeWidth) / (params.width - params.edgeWidth), 0.0f, 1.0f);

						y *= 1.0f - distancePercentage;
					}
				}

				if (y > highest)
					highest = y;

				if (y < lowest)
					lowest = y;

				heightMap.push_back(y);
			}
		}

		LOG_DEBUG("Highest value = %f, lowest value = %f", highest, lowest);

		return heightMap;
	}
}