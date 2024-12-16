

#include "RigidBodyWidget.h"


#include <HexEngine.Core\Entity\Component\MeshRenderer.hpp>

RigidBodyWidget::RigidBodyWidget(HexEngine::Entity* entity, QWidget* parent) :
	BaseWidget(parent),
	_entity(entity)
{
	ui.setupUi(this);	

	_rigidBody = entity->GetComponent<HexEngine::RigidBody>();

	ui.bodyType->addItem("Static");
	ui.bodyType->addItem("Kinematic");
	ui.bodyType->addItem("Dynamic");

	ui.collider->addItem("None");
	ui.collider->addItem("Box");
	ui.collider->addItem("Sphere");
	ui.collider->addItem("Capsule");
	ui.collider->addItem("Terrain");	
	ui.collider->addItem("Mesh");

	ui.collider->setCurrentIndex((int)_rigidBody->GetColliderShape());

	connect(ui.collider, &QComboBox::currentIndexChanged, this, &RigidBodyWidget::OnChangeCollider);

	resize(0, 0);
}

void RigidBodyWidget::OnChangeCollider(int index)
{
	auto rb = _entity->GetComponent<HexEngine::RigidBody>();

	rb->RemoveCollider();

	switch ((HexEngine::IRigidBody::ColliderShape)index)
	{
		case HexEngine::IRigidBody::ColliderShape::Box:
		{
			auto box = _entity->GetAABB();
			rb->AddBoxCollider(box);
			break;
		}

		case HexEngine::IRigidBody::ColliderShape::Sphere:
		{
			rb->AddSphereCollider(_entity->GetBoundingSphere().Radius);
			break;
		}

		case HexEngine::IRigidBody::ColliderShape::Capsule:
		{
			rb->AddCapsuleCollider(_entity->GetBoundingSphere().Radius / 2.0f, _entity->GetBoundingSphere().Radius);
			break;
		}

		case HexEngine::IRigidBody::ColliderShape::HeightField:
		{
			//rb->AddTerrainCollider()
			break;
		}

		case HexEngine::IRigidBody::ColliderShape::TriangleMesh:
		{
			for (auto&& mesh : _entity->GetComponent<HexEngine::MeshRenderer>()->GetMeshes())
			{
				rb->AddTriangleMeshCollider(mesh->GetRawVertices(), mesh->GetIndices(), mesh->GetNumFaces(), true);
			}
			break;
		}
	}
}