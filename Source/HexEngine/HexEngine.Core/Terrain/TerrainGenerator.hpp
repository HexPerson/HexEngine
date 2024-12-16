
#pragma once

#include "../Required.hpp"
#include "../FileSystem/JsonFile.hpp"

namespace HexEngine
{
	class Mesh;

	struct TerrainGenerationParams
	{
		void Save(json& data, JsonFile* file)
		{
			file->Serialize(data, "ident", ident);
			file->Serialize(data, "resolution", resolution);
			file->Serialize(data, "position", position);
			file->Serialize(data, "uvScale", uvScale);
			file->Serialize(data, "width", width);
			file->Serialize(data, "createInstance", createInstance);

			data["heightMap"] = heightMap;

			/*file->WriteString(ident);
			file->Write(&resolution, sizeof(uint32_t));
			file->Write(&position.x, sizeof(math::Vector3));
			file->Write(&uvScale, sizeof(float));
			file->Write(&width, sizeof(float));
			file->Write(&createInstance, sizeof(bool));
			
			size_t heightMapSize = heightMap.size();
			file->Write(&heightMapSize, sizeof(size_t));
			file->Write(heightMap.data(), (uint32_t)(sizeof(float) * heightMapSize));*/
		}

		void Load(json& data, JsonFile* file)
		{
			file->Deserialize(data, "ident", ident);
			file->Deserialize(data, "resolution", resolution);
			file->Deserialize(data, "position", position);
			file->Deserialize(data, "uvScale", uvScale);
			file->Deserialize(data, "width", width);
			file->Deserialize(data, "createInstance", createInstance);

			auto& hm = data["heightMap"];

			heightMap.insert(heightMap.begin(), hm.begin(), hm.end());

			/*ident = file->ReadString();
			file->Read(&resolution, sizeof(uint32_t));
			file->Read(&position.x, sizeof(math::Vector3));
			file->Read(&uvScale, sizeof(float));
			file->Read(&width, sizeof(float));
			file->Read(&createInstance, sizeof(bool));

			size_t heightMapSize;
			file->Read(&heightMapSize, sizeof(size_t));

			heightMap.resize(heightMapSize);
			file->Read(heightMap.data(), (uint32_t)(sizeof(float) * heightMapSize));*/
		}

		std::string ident;
		uint32_t resolution = 8;
		math::Vector3 position;		
		float uvScale = 1.0f;
		float width = 1.0f;
		bool createInstance = false;

		std::vector<float> heightMap;

		float modulo = 0.0f;
	};

	Mesh* CreateTerrain(const TerrainGenerationParams& params);
}
