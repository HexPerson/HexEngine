

#pragma once

#include <HexEngine.Core/Physics/ICollider.hpp>
#include <PxPhysicsAPI.h>

namespace HexEngine
{
	class RigidBodyPhysX;

	class ColliderPhysX : public ICollider
	{
	public:
		ColliderPhysX(physx::PxShape* shape, RigidBodyPhysX* body);
		~ColliderPhysX();

		physx::PxShape* GetShape();

	private:
		physx::PxShape* _shape;
		RigidBodyPhysX* _body;
	};
}
