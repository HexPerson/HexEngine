

#include "CharacterController.hpp"
#include <HexEngine.Core/Environment/IEnvironment.hpp>
#include "PhysicsSystemPhysX.hpp"

CharacterController::CharacterController(physx::PxController* controller, physx::PxRigidActor* body, HexEngine::Entity* entity) :
	RigidBodyPhysX(body, nullptr, entity),
	_controller(controller)
{
}

void CharacterController::Destroy()
{
	if (_controller != nullptr)
	{
		// Releases the controller's actor + shape too (PhysX owns them).
		_controller->release();
		_controller = nullptr;
		ForgetActor();
	}
	RigidBodyPhysX::Destroy();
}

namespace
{
	// Overlap filter: only KINEMATIC actors (moving platforms), never our own capsule.
	struct KinematicOnlyFilter : public physx::PxQueryFilterCallback
	{
		const physx::PxRigidActor* ignore = nullptr;
		virtual physx::PxQueryHitType::Enum preFilter(const physx::PxFilterData&, const physx::PxShape*, const physx::PxRigidActor* actor, physx::PxHitFlags&) override
		{
			if (actor == nullptr || actor == ignore)
				return physx::PxQueryHitType::eNONE;
			const auto* dynamic = actor->is<physx::PxRigidDynamic>();
			if (dynamic == nullptr || !(dynamic->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC))
				return physx::PxQueryHitType::eNONE;
			return physx::PxQueryHitType::eTOUCH;
		}
		virtual physx::PxQueryHitType::Enum postFilter(const physx::PxFilterData&, const physx::PxQueryHit&, const physx::PxShape*, const physx::PxRigidActor*) override
		{
			return physx::PxQueryHitType::eTOUCH;
		}
	};
}

void CharacterController::Move(const math::Vector3& dir, float minLength, float frameTime)
{
	if (!_controller)
		return;

	physx::PxControllerFilters filters = {};

	// The controller caches the geometry around it and refreshes that cache only
	// when STATIC shapes change (or, with PhysX's own riding, for the object it
	// stands on - which we replace with eCCT_USER_DEFINED_RIDE). A kinematic
	// vehicle that drove up is therefore still where it was cached, metres
	// away: the player walked straight through a bus at a stop. Flush the cache
	// whenever a kinematic body is near.
	{
		const physx::PxExtendedVec3 c = _controller->getPosition();
		KinematicOnlyFilter filter;
		filter.ignore = _controller->getActor();
		physx::PxOverlapHit touches[1];
		physx::PxOverlapBuffer hit(touches, 1);
		physx::PxQueryFilterData filterData(physx::PxQueryFlag::eDYNAMIC | physx::PxQueryFlag::ePREFILTER);
		auto* scene = g_pPhysx->GetScene();
		scene->lockRead();
		scene->overlap(physx::PxSphereGeometry(4.0f), physx::PxTransform(physx::PxVec3((float)c.x, (float)c.y, (float)c.z)), hit, filterData, &filter);
		scene->unlockRead();
		if (hit.getNbAnyHits() > 0)
			_controller->invalidateCache();
	}

	g_pPhysx->_lastControllerMoveDt = frameTime > 1e-4f ? frameTime : 1.0f / 120.0f;

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

namespace
{
	// Scene-query filter that skips one actor (the controller's own capsule).
	struct IgnoreActorFilter : public physx::PxQueryFilterCallback
	{
		const physx::PxRigidActor* ignore = nullptr;
		virtual physx::PxQueryHitType::Enum preFilter(const physx::PxFilterData&, const physx::PxShape* shape, const physx::PxRigidActor* actor, physx::PxHitFlags&) override
		{
			(void)shape;
			return actor == ignore ? physx::PxQueryHitType::eNONE : physx::PxQueryHitType::eBLOCK;
		}
		virtual physx::PxQueryHitType::Enum postFilter(const physx::PxFilterData&, const physx::PxQueryHit&, const physx::PxShape*, const physx::PxRigidActor*) override
		{
			return physx::PxQueryHitType::eBLOCK;
		}
	};
}

HexEngine::IRigidBody* CharacterController::GetGroundBody() const
{
	if (_controller == nullptr)
		return nullptr;

	// PhysX only tracks a "touched object" for its own riding, which is turned
	// off (eCCT_USER_DEFINED_RIDE), so look for it ourselves: a short ray down
	// from the foot against MOVABLE (dynamic / kinematic) actors - statics
	// never carry anyone.
	const physx::PxExtendedVec3 foot = _controller->getFootPosition();
	const physx::PxVec3 origin((float)foot.x, (float)foot.y + 0.3f, (float)foot.z);
	IgnoreActorFilter filter;
	filter.ignore = _controller->getActor();
	physx::PxRaycastBuffer hit;
	physx::PxQueryFilterData filterData(physx::PxQueryFlag::eDYNAMIC | physx::PxQueryFlag::ePREFILTER);

	auto* scene = g_pPhysx->GetScene();
	scene->lockRead();
	const bool found = scene->raycast(origin, physx::PxVec3(0.0f, -1.0f, 0.0f), 0.6f, hit, physx::PxHitFlag::eDEFAULT, filterData, &filter);
	scene->unlockRead();
	if (!found || !hit.hasBlock || hit.block.actor == nullptr)
		return nullptr;
	return static_cast<RigidBodyPhysX*>(hit.block.actor->userData);
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