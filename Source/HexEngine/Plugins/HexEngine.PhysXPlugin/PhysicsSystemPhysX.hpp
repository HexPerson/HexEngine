

#pragma once

#include <HexEngine.Core/Physics/IPhysicsSystem.hpp>
#include "RigidBodyPhysx.hpp"
#include "CharacterController.hpp"
#include <PxPhysicsAPI.h>

#define PX_RELEASE(x)	if(x)	{ x->release(); x = NULL;	}

class PhysicsSystemPhysX : public HexEngine::IPhysicsSystem, public physx::PxUserControllerHitReport, public physx::PxControllerBehaviorCallback, public physx::PxSimulationEventCallback, public physx::PxErrorCallback
{
public:
	virtual bool Create() override;

	virtual void Destroy() override;

	virtual HexEngine::IRigidBody* CreateRigidBody(HexEngine::Transform* transform, HexEngine::RigidBody* bodyComponent, HexEngine::IRigidBody::BodyType type, const math::Vector3& offset = math::Vector3::Zero) override;

	virtual HexEngine::IRigidBody* CloneRigidBody(HexEngine::IRigidBody* physicsBody, HexEngine::Transform* transform, HexEngine::RigidBody* bodyComponent, HexEngine::IRigidBody::BodyType type) override;

	virtual void DestroyRigidBody(HexEngine::IRigidBody* body) override;

	virtual void Update(float frameTime) override;

	virtual void Simulate(float simulationTime) override;

	virtual void DebugRender() override;

	virtual HexEngine::IRigidBody* CreateController(const HexEngine::ControllerParameters& params, HexEngine::Transform* transform) override;

	virtual uint32_t RayCast(const math::Vector3& from, const math::Vector3& unitDir, float maxDist, HexEngine::IRigidBody* body, HexEngine::RayHit* hitInfo) override;

	virtual uint32_t RayCastScene(const math::Vector3& from, const math::Vector3& unitDir, float maxDist, HexEngine::RayHit* hitInfo, HexEngine::IRigidBody* ignoreBody = nullptr) override;

	virtual uint32_t RayCastSceneFiltered(const math::Vector3& from, const math::Vector3& unitDir, float maxDist, HexEngine::RayHit* hitInfo, const HexEngine::RaySceneFilter& filter) override;

	virtual math::Vector3 GetGravity() override;

	virtual void SetGravity(const math::Vector3& gravity) override;

	virtual void CreateHingeJoint(HexEngine::IRigidBody* bodies[2], const math::Vector3 axes[2], const math::Vector3 offsets[2]) override;
	virtual void* CreateConeJoint(HexEngine::IRigidBody* parent, HexEngine::IRigidBody* child, const math::Matrix& jointWorld,
		float swingDegrees, float twistDegrees) override;
	virtual void DestroyJoint(void* joint) override;

	physx::PxPhysics* GetPhysics();
	physx::PxMaterial* GetDefaultMaterial();
	//physx::PxCooking* GetCooking();
	physx::PxScene* GetScene();

	void LockWrite();
	void UnlockWrite();

	// PxUserControllerHitReport
	//
	virtual void onShapeHit(const physx::PxControllerShapeHit& hit) override;
	virtual void onControllerHit(const physx::PxControllersHit& hit) override;
	virtual void onObstacleHit(const physx::PxControllerObstacleHit& hit) override;

	// PxControllerBehaviorCallback: riding moving platforms is done by the
	// game-side controller (FirstPersonCameraController), not PhysX's built-in
	// ride, so a kinematic bus floor never carries the capsule twice.
	virtual physx::PxControllerBehaviorFlags getBehaviorFlags(const physx::PxShape& shape, const physx::PxActor& actor) override;
	virtual physx::PxControllerBehaviorFlags getBehaviorFlags(const physx::PxController& controller) override;
	virtual physx::PxControllerBehaviorFlags getBehaviorFlags(const physx::PxObstacle& obstacle) override;

	// PxSimulationEventCallback
	//
	virtual void onConstraintBreak(physx::PxConstraintInfo* constraints, physx::PxU32 count) override {}
	virtual void onWake(physx::PxActor** actors, physx::PxU32 count) override {}
	virtual void onSleep(physx::PxActor** actors, physx::PxU32 count) override {}
	virtual void onContact(const physx::PxContactPairHeader& pairHeader, const physx::PxContactPair* pairs, physx::PxU32 nbPairs) override;
	virtual void onTrigger(physx::PxTriggerPair* pairs, physx::PxU32 count) override;
	virtual void onAdvance(const physx::PxRigidBody* const* bodyBuffer, const physx::PxTransform* poseBuffer, const physx::PxU32 count) override {}

	// PxErrorCallback
	virtual void reportError(physx::PxErrorCode::Enum code, const char* message, const char* file, int line) override;



private:
	physx::PxFoundation* _foundation = nullptr;
	physx::PxPvd* _pvd = nullptr;
	physx::PxPhysics* _physics = nullptr;
	physx::PxDefaultCpuDispatcher* _dispatcher = nullptr;
	physx::PxScene* _scene = nullptr;
	//physx::PxCooking* _cooking = nullptr;
	physx::PxMaterial* _defaultMaterial = nullptr;
	physx::PxControllerManager* _controllerManager = nullptr;



	std::vector<physx::PxDebugLine> _debugLines;
	std::vector< physx::PxDebugTriangle> _debugTriangles;
	bool _waitingForSimulationStepForDebug = false;
	bool _didDoDebugSimulationStep = false;

public:
	bool _resetDebug = false;
	// Time step of the controller move being processed (set by
	// CharacterController::Move) so onShapeHit can turn the hit's motion length
	// into the walker's speed.
	float _lastControllerMoveDt = 1.0f / 120.0f;

};

inline PhysicsSystemPhysX* g_pPhysx = nullptr;