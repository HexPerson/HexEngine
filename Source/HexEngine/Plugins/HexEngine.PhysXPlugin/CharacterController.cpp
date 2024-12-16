

#include "CharacterController.hpp"
#include <HexEngine.Core/Environment/IEnvironment.hpp>
#include "PhysicsSystemPhysX.hpp"

namespace HexEngine
{
	CharacterController::CharacterController(physx::PxController* controller, physx::PxRigidActor* body, Entity* entity) :
		RigidBodyPhysX(body, nullptr, entity),
		_controller(controller)
	{}

	void CharacterController::Move(const math::Vector3& dir, float minLength, float frameTime)
	{
		if (!_controller)
			return;

		physx::PxControllerFilters filters = {};

		g_pPhysx->GetScene()->lockRead();
	
		_controller->move(
			*(physx::PxVec3*)&dir.x,
			minLength,
			frameTime,
			filters,
			nullptr);

		g_pPhysx->GetScene()->unlockRead();
	}

	math::Vector3 CharacterController::GetPhysicsPosition()
	{
		auto pos = _controller->getFootPosition();

		return math::Vector3((float)pos.x, (float)pos.y, (float)pos.z);
	}

	bool CharacterController::IsOnGround() const
	{
		physx::PxControllerState cctState;
		_controller->getState(cctState);
		return (cctState.collisionFlags & physx::PxControllerCollisionFlag::eCOLLISION_DOWN) != 0;
	}
}