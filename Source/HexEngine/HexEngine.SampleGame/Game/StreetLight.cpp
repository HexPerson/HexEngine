

#include "StreetLight.hpp"
#include "../../HexEngine.Core/Entity/Component/MeshRenderer.hpp"
#include "../../HexEngine.Core/Environment/IEnvironment.hpp"
#include "../Game.hpp"

namespace CityBuilder
{
	StreetLight::StreetLight() :
		BaseChunkEntity()
	{
		SetName("StreetLight");
		SetLayer(HexEngine::Layer::StaticGeometry | HexEngine::Layer::StreetLight);
	}

	void StreetLight::Create()
	{
		BaseChunkEntity::Create();

		auto meshComponent = AddComponent<HexEngine::MeshRenderer>();

		HexEngine::Model* model = (HexEngine::Model*)HexEngine::g_pEnv->_resourceSystem->LoadResource("Models/Roads/street_light_01.obj");

		auto mesh = model->GetMeshAtIndex(0);// ->Clone();

		meshComponent->SetMeshes({ model->GetMeshes() });


		_lightOffset = math::Vector3(0.0f, 18.4f, -5.9f);

		_light = new HexEngine::SpotLight(35.0f);
		HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->AddEntity(_light);

		_light->SetPosition(this->GetPosition() + _lightOffset);
		//_light->SetDiffuseColour(math::Color(1.0f, 1.0f, 1.0f, 1.0f));
		_light->SetDiffuseColour(math::Color(0.992f, 0.913f, 0.747f, 1.4f));

		//GetComponent<HexEngine::Transform>()->SetScale(math::Vector3(2.0f));

		_billboard = new HexEngine::Billboard((HexEngine::ITexture2D*)HexEngine::g_pEnv->_resourceSystem->LoadResource("Textures/light-flare.png"));
		HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->AddEntity(_billboard);
		_billboard->SetPosition(_light->GetPosition());
		_billboard->SetScale(math::Vector3(5.0f));

		GetChunk()->AddChunkChild(_billboard);
	}

	void StreetLight::Destroy()
	{
		BaseChunkEntity::Destroy();

		if (_light)
		{
			HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->RemoveEntity(_light);
			SAFE_DELETE(_light);
		}

		if (_billboard)
		{
			GetChunk()->RemoveChunkChild(_billboard);

			HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->RemoveEntity(_billboard);
			SAFE_DELETE(_billboard);
		}
	}

	void StreetLight::OnTransformChanged(bool scaleChanged, bool rotationChanged, bool translationChanged)
	{
		BaseChunkEntity::OnTransformChanged(scaleChanged, rotationChanged, translationChanged);

		if (translationChanged)
		{
			_light->SetPosition(this->GetPosition() + _lightOffset);
			_billboard->SetPosition(_light->GetPosition());
		}

		if (rotationChanged || translationChanged)
		{
			auto transformedRelativePos = math::Vector3::Transform(_lightOffset, GetRotation());

			//currentPos += GetRotation()

			_light->SetPosition(GetPosition() + transformedRelativePos);
			_billboard->SetPosition(_light->GetPosition());
		}

		
	}

	//StreetLight* StreetLight::Load(HexEngine::DiskFile* file)
	//{
	//	// create a dummy entity first to get the basing data
	//	Entity dummyEnt;
	//	LoadBasicEntityData(file, &dummyEnt);

	//	HexEngine::Chunk* chunk = g_pGame->_world->GetTerrainChunkManager().GetChunkByPosition(dummyEnt.GetPosition());

	//	if (chunk == nullptr)
	//	{
	//		LOG_CRIT("Could not find a valid chunk at Road load position, maybe the save file is corrupted?");
	//		return nullptr;
	//	}

	//	StreetLight* light = new StreetLight(chunk);		

	//	HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->AddEntity(light);

	//	light->SetPosition(dummyEnt.GetPosition());
	//	light->SetRotation(dummyEnt.GetRotation());
	//	light->SetScale(dummyEnt.GetScale());

	//	// make sure its linked back into the chunk tile it came from
	//	//
	//	light->LoadIntoTile();

	//	return light;
	//}
}