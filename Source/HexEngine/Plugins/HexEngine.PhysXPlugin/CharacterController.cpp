

#include "CharacterController.hpp"
#include <HexEngine.Core/Environment/IEnvironment.hpp>
#include "PhysicsSystemPhysX.hpp"

CharacterController::CharacterController(physx::PxController* controller, physx::PxRigidActor* body, HexEngine::Entity* entity) :
	RigidBodyPhysX(body, nullptr, entity),
	_controller(controller)
{
}

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

void CharacterController::UpdatePosePosition(const math::Vector3& position)
{
	// A CCT's collision position lives in the PxController capsule, not the
	// underlying actor's global pose. The base-class path (setGlobalPose on the
	// actor) leaves the capsule behind, so an external transform reposition
	// (editor drag, PlayerStart spawn) silently desyncs - the entity renders at
	// the new spot but the capsule stays put, and the next move() snaps the
	// entity back down to the stale capsule location (the "teleport under the
	// map" bug). Teleport the capsule itself instead. The transform tracks the
	// foot position (see PhysicsSystemPhysX read-back), so setFootPosition keeps
	// the round-trip consistent.
	if (_controller == nullptr)
	{
		RigidBodyPhysX::UpdatePosePosition(position);
		return;
	}

	g_pPhysx->GetScene()->lockWrite();
	_controller->setFootPosition(physx::PxExtendedVec3(position.x, position.y, position.z));
	g_pPhysx->GetScene()->unlockWrite();
}

void CharacterController::UpdatePoseRotation(const math::Quaternion& rotation)
{
	// A PxController capsule has no meaningful orientation - it stays upright and
	// facing is driven by the entity/camera (which is why the read-back sync skips
	// SetRotation for CCTs). Rotating the underlying actor would tilt the capsule,
	// so swallow rotation pose updates here.
	(void)rotation;
}