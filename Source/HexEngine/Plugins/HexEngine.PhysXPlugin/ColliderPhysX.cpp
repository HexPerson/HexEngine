

#include "ColliderPhysX.hpp"
#include "PhysicsSystemPhysX.hpp"

namespace HexEngine
{
	ColliderPhysX::ColliderPhysX(physx::PxShape* shape, RigidBodyPhysX* body) :
		_shape(shape),
		_body(body)
	{}

	ColliderPhysX::~ColliderPhysX()
	{
		//PX_RELEASE(_shape);
	}

	physx::PxShape* ColliderPhysX::GetShape()
	{
		return _shape;
	}
}