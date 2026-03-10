

#pragma once

#include <HexEngine.Core/Physics/ICollider.hpp>
#include <PxPhysicsAPI.h>

class RigidBodyPhysX;

class ColliderPhysX : public HexEngine::ICollider
{
public:
	ColliderPhysX(physx::PxShape* shape, RigidBodyPhysX* body);
	~ColliderPhysX();

	physx::PxShape* GetShape();

private:
	physx::PxShape* _shape;
	RigidBodyPhysX* _body;
};
