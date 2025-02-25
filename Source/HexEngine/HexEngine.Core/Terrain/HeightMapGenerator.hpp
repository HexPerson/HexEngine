
#pragma once

#include "../Required.hpp"
#include "../FileSystem/SceneSaveFile.hpp"

namespace HexEngine
{
	struct Biome
	{
		enum class TerrainFlattenMode : int32_t
		{
			Flatten,
			Additive,
		};

		math::Vector3 position;
		float radius;
		float falloffStart;
		int32_t flattenMode;

		void Save(SceneSaveFile* file)
		{
			file->Write(position);
			file->Write(radius);
			file->Write(falloffStart);
			file->Write(flattenMode);
		}

		void Load(SceneSaveFile* file)
		{
			file->Read(&position);
			file->Read(&radius);
			file->Read(&falloffStart);
			file->Read(&flattenMode);
		}
	};
	struct HeightMapGenerationParams
	{
		uint32_t seed;
		uint32_t resolution;
		float width;
		math::Vector3 position;
		float heightScale;

		float edgeHeight = -1.0f;
		float edgeWidth = 0.0f;

		std::vector<Biome> biomes;

		float minimumHeight = FLT_MIN;
		bool setMinHeight = false;

		void Save(SceneSaveFile* file)
		{
			file->Write(seed);
			file->Write(resolution);
			file->Write(width);
			file->Write(position);
			file->Write(heightScale);
			file->Write(edgeHeight);
			file->Write(edgeWidth);
			file->Write(minimumHeight);
			file->Write(setMinHeight);

			for (auto& biome : biomes)
			{
				biome.Save(file);
			}
		}
		void Load(SceneSaveFile* file)
		{
			file->Read(&seed);
			file->Read(&resolution);
			file->Read(&width);
			file->Read(&position);
			file->Read(&heightScale);
			file->Read(&edgeHeight);
			file->Read(&edgeWidth);
			file->Read(&minimumHeight);
			file->Read(&setMinHeight);

			for (auto& biome : biomes)
			{
				biome.Load(file);
			}
		}
	};

	std::vector<float> HEX_API GenerateHeightMap(const HeightMapGenerationParams& params);
}
