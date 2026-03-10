

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

private:
	physx::PxController* _controller;
};