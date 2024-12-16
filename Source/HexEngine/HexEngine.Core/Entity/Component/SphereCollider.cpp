

#include "SphereCollider.hpp"
#include "../Entity.hpp"

namespace HexEngine
{
	SphereCollider::SphereCollider(Entity* entity, float radius) :
		Collider(entity)
	{
		// Add the sphere collider to the rigidbody
		//
		auto rigidBody = (RigidBody*)entity->GetComponent<RigidBody>();

		rigidBody->AddSphereCollider(radius);
		rigidBody->GetIRigidBody()->SetMass(0.1f);

		// Set the radius of the collider
		//
		SetRadius(radius);		
	}

	void SphereCollider::SetRadius(float radius)
	{
		_radius = radius;
	}
}