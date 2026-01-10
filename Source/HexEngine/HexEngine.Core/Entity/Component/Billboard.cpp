

#include "Billboard.hpp"
#include "Camera.hpp"
#include "../Entity.hpp"
#include "StaticMeshComponent.hpp"
#include "../../Environment/IEnvironment.hpp"
#include "../../Scene/SceneManager.hpp"
#include "Transform.hpp"

namespace HexEngine
{
	Billboard::Billboard(Entity* entity) :
		UpdateComponent(entity)
	{
		GetEntity()->SetLayer(Layer::Particle);
	}

	Billboard::Billboard(Entity* entity, Billboard* copy) :
		UpdateComponent(entity)
	{
		GetEntity()->SetLayer(Layer::Particle);
	}

	void Billboard::SetTexture(const std::shared_ptr<ITexture2D>& texture)
	{
		StaticMeshComponent* renderer = GetEntity()->GetComponent<StaticMeshComponent>();

		if (!_mesh)
		{
			if (!renderer)
				renderer = GetEntity()->AddComponent<StaticMeshComponent>();

			auto model = Model::Create("EngineData.Models/Primitives/billboard.obj");
			_mesh = model->GetMeshAtIndex(0);

			renderer->SetMaterial(Material::Create("EngineData.Materials/Billboard.hmat"));

			renderer->SetMesh(_mesh);
		}
		renderer->GetMaterial()->SetTexture(MaterialTexture::Albedo, texture);
		
	}

	void Billboard::Update(float frameTime)
	{
		UpdateComponent::Update(frameTime);

		auto camera = g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera();

		auto cameraTransform = camera->GetEntity()->GetComponent<Transform>();

		auto cameraForward = cameraTransform->GetForward();

		auto matrix = math::Matrix::CreateBillboard(GetEntity()->GetPosition(), cameraTransform->GetPosition(), math::Vector3::Up/*, &cameraForward*/);

		auto dir = cameraTransform->GetPosition() - GetEntity()->GetComponent<Transform>()->GetPosition();
		dir.Normalize();

		auto rot = math::Quaternion::LookRotation(-dir, math::Vector3::Up);

		GetEntity()->GetComponent<Transform>()->SetRotation(rot/*math::Quaternion::CreateFromRotationMatrix(matrix)*/);
	}
}