

#pragma once

#include "RigidBodyPhysX.hpp"
#include <PxPhysicsAPI.h>

class RigidBodyPhysX;

class CharacterController : public RigidBodyPhysX
{
public:
	CharacterController(physx::PxController* controller, physx::PxRigidActor* body, HexEngine::Entity* entity);

	virtual void Move(const math::Vector3& dir, float minLength, float frameTime) override;

	virtual math::Vector3 GetPhysicsPosition() override;

	virtual bool IsOnGround() const override;

	// Teleport the capsule when the transform is repositioned (editor drag,
	// PlayerStart spawn). The base class moves the underlying actor, which leaves
	// the PxController capsule behind.
	virtual void UpdatePosePosition(const math::Vector3& position) override;
	virtual void UpdatePoseRotation(const math::Quaternion& rotation) override;

	virtual bool IsCharacterController() const override { return true; }

private:
	physx::PxController* _controller;
};