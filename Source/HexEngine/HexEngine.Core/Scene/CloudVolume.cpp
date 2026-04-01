

#include "CloudVolume.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../Graphics/IGraphicsDevice.hpp"
#include "../Environment/LogFile.hpp"
#include "../Math/FloatMath.hpp"
#include "../Scene/MeshPrimitives.hpp"
#include "../Scene/SceneManager.hpp"
#include "../Entity/Component/StaticMeshComponent.hpp"

#include <fastnoiselite/Cpp/FastNoiseLite.h>

#if 0
namespace HexEngine
{
	
	CloudVolume::CloudVolume(const math::Vector3& mins, const math::Vector3& maxs, const int32_t resolution) :
		_mins(mins),
		_maxs(maxs),
		_resolution(resolution),
		_entity(nullptr),
		_texture(nullptr),
		_shader(nullptr)
	{
		// create the bounding box
		dx::BoundingBox::CreateFromPoints(_bbox, mins, maxs);		
	}

	CloudVolume::~CloudVolume()
	{
		SAFE_DELETE(_texture);
		SAFE_UNLOAD(_shader);

		if (_entity)
		{
			g_pEnv->_sceneManager->GetCurrentScene()->DestroyEntity(_entity);
			_entity = nullptr;
		}
	}

	bool CloudVolume::Generate()
	{
		_shader = (IShader*)g_pEnv->_resourceSystem->LoadResource("EngineData.Shaders/VolumetricClouds.hcs");

		// Create the mesh
		std::vector<MeshVertex> vertices;
		std::vector<uint32_t> indices;
		g_pEnv->_meshPrimitives->GenerateBox(_bbox, vertices, indices);

		Mesh* boxMesh = new Mesh(nullptr, "CloudVolume");
		boxMesh->AddVertices(vertices);
		boxMesh->AddIndices(indices);
		boxMesh->CreateBuffers(false);
		boxMesh->CreateInstance();
		

		_entity = g_pEnv->_sceneManager->GetCurrentScene()->CreateEntity("VolumetricClouds");
		auto meshRenderer = _entity->AddComponent<MeshRenderer>();
		meshRenderer->SetMeshes({ boxMesh });
		meshRenderer->SetShader(_shader);

		_entity->SetPosition(math::Vector3(0.0f, 1500.0f, 0.0f));
		_entity->SetAABB(_bbox);

		FastNoiseLite noiseGen;
		noiseGen.SetSeed(GetRandomInt());
		noiseGen.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
		noiseGen.SetFrequency(0.006f);
		noiseGen.SetFractalOctaves(5);

		float* colorBuffer = new float[_resolution * _resolution * _resolution];

		const float scale = 1.0f;

		//int32_t idx = 0;
		for (int y = 0; y < _resolution; ++y)
		{
			//UINT depStart = mappedTex.DepthPitch / 4 * dep;

			for (int z = 0; z < _resolution; ++z)
			{
				for (int x = 0; x < _resolution; ++x)
				{
					float xpos = (float)x * scale;
					float zpos = (float)z * scale;
					float ypos = (float)y * scale;

					float n = (noiseGen.GetNoise(xpos, zpos, ypos));// / 2.0f) + 0.5f; // shift from -1 -> 1 to 0 -> 1

					int32_t idx = y * (_resolution * _resolution) + z * _resolution + x;

					colorBuffer[idx] = n;// (uint8_t)(n * 255.0f);

					//idx++;
				}
			}
		}

		D3D11_SUBRESOURCE_DATA initialData;
		initialData.pSysMem = colorBuffer;
		initialData.SysMemPitch = _resolution * 4;
		initialData.SysMemSlicePitch = _resolution * _resolution * 4;

		_texture = g_pEnv->_graphicsDevice->CreateTexture3D(
			_resolution,
			_resolution,
			_resolution,
			DXGI_FORMAT_R32_FLOAT,
			1,
			D3D11_BIND_SHADER_RESOURCE,
			0, 1, 0,
			&initialData,
			D3D11_RTV_DIMENSION_UNKNOWN,
			D3D11_UAV_DIMENSION_UNKNOWN,
			D3D11_SRV_DIMENSION_TEXTURE3D);

		if (!_texture)
		{
			LOG_CRIT("Could not create cloud volume texture");
			return false;
		}

		auto material = boxMesh->CreateMaterial();
		material->SetVolumeTexture(_texture);

		material->_properties.hasTransparency = true;

		return true;
	}
}
#endif
