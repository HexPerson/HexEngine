

#include "FoliageTool.hpp"

void FoliageTool::Create()
{
	//_models.push_back((HexEngine::Model*)HexEngine::g_pEnv->_resourceSystem->LoadResource("Models/World/Trees/Tree_Aesculus-hippocastanum_A_autumn.obj"));
	//_models.push_back((HexEngine::Model*)HexEngine::g_pEnv->_resourceSystem->LoadResource("Models/World/Trees/Tree_Pinus-pinaster_A_spring-summer-autumn.obj"));
}

void FoliageTool::Update(int x, int y)
{
	if (HexEngine::g_pEnv->_timeManager->GetTime() - _lastPlaceTime >= 0.1f)
	{
		_lastPlaceTime = HexEngine::g_pEnv->_timeManager->GetTime();

		auto scene = HexEngine::g_pEnv->_sceneManager->GetCurrentScene();

		auto camera = scene->GetMainCamera();

		const std::pair<int, int> PixelDirs[] = {
			/*{-25, -25},
			{-25, 25},
			{25, -25},
			{25, 25},*/
			{0, 0},
		};

		for (auto& deviations : PixelDirs)
		{
			auto ray = HexEngine::g_pEnv->_inputSystem->GetScreenToWorldRay(camera, x + deviations.first, y + deviations.second);

			HexEngine::RayHit hit;

			if (scene->CameraPickEntity(ray, hit, (uint32_t)HexEngine::Layer::StaticGeometry))
			{
				//auto lookAt = math::Matrix::CreateWorld(hit.position, hit.normal, math::Vector3::Up);

				float angle = acos(hit.normal.Dot(math::Vector3::Up));

				auto rotation = math::Quaternion::CreateFromAxisAngle(hit.normal, angle);

				HexEngine::Entity* foliage = scene->CreateEntity("Foliage_" + std::to_string(_placedEntities.size()), hit.position, rotation);

				auto meshRenderer = foliage->AddComponent<HexEngine::MeshRenderer>();

				meshRenderer->SetMeshes(_models.at(HexEngine::GetRandomInt(0, _models.size()-1))->GetMeshes());

				foliage->SetCastsShadows(true); // we don't want foliage to cast shadows

				auto transform = foliage->GetComponent<HexEngine::Transform>();
				transform->SetScale(math::Vector3(HexEngine::GetRandomFloat(1.0f, 2.0f)));
				//transform->SetYaw(ToRadian(HexEngine::GetRandomFloat(0.0f, 360.0f)));

				_placedEntities.push_back(foliage);
			}
		}
	}
}