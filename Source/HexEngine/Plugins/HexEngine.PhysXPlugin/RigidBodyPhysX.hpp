

#pragma once

#include <HexEngine.Core/Physics/IRigidBody.hpp>
#include "ColliderPhysX.hpp"
#include <PxPhysicsAPI.h>
#include <atomic>
#include <future>
#include <memory>
#include <vector>

namespace HexEngine
{
	class RigidBody;
}

class RigidBodyPhysX : public HexEngine::IRigidBody
{
public:
	friend class PhysicsSystemPhysX;

	RigidBodyPhysX(physx::PxRigidActor* body, HexEngine::RigidBody* bodyComponent, HexEngine::Entity* entity);

	virtual void Create(HexEngine::Transform* transform) override;

	virtual void Destroy() override;

	virtual void SetBodyType(BodyType type) override;

	virtual BodyType GetBodyType() const override;

	virtual math::Vector3 GetPhysicsPosition() override;

	virtual math::Quaternion GetPhysicsRotation() override;

	virtual math::Vector3 GetLinearVelocity() override;

	virtual HexEngine::ICollider* AddSphereCollider(HexEngine::Transform* transform, float radius) override;

	virtual HexEngine::ICollider* AddCapsuleCollider(HexEngine::Transform* transform, float radius, float height) override;

	virtual HexEngine::ICollider* AddHeightFieldCollider(const int32_t columns, const int32_t rows, const float minHeight, const float maxHeight, float* heightValues, const math::Vector3& position, float scale = 1.0f) override;

	virtual HexEngine::ICollider* AddBoxCollider(const dx::BoundingBox& box, bool exclusive) override;

	virtual HexEngine::ICollider* AddTriangleMeshCollider(const std::vector<math::Vector3>& vertices, const std::vector<HexEngine::MeshIndexFormat>& indices, uint32_t faceCount, bool exclusive) override;

	virtual bool BeginAddTriangleMeshColliderAsync(
		const std::vector<math::Vector3>& vertices,
		const std::vector<HexEngine::MeshIndexFormat>& indices,
		uint32_t faceCount,
		bool exclusive) override;

	virtual HexEngine::ICollider* AddTriangleMeshColliderFromCookedBuffer(
		const std::vector<uint8_t>& cookedBuffer,
		bool exclusive) override;

	virtual bool TryFinishAsyncCollider() override;

	virtual bool HasAsyncColliderInFlight() const override;

	virtual const std::vector<uint8_t>& GetLastCookedBuffer() const override { return _lastCookedBuffer; }

	virtual HexEngine::ICollider* AddConvexMeshCollider(const std::vector<math::Vector3>& vertices, const std::vector<HexEngine::MeshIndexFormat>& indices, uint32_t faceCount, bool exclusive) override;

	virtual void UpdateBoxExtents(const dx::BoundingBox& box) override;

	virtual void UpdateTriangleMeshScale(const math::Vector3& scale) override;

	virtual void RemoveCollider() override;

	virtual void ApplyForceToCenterOfMass(const math::Vector3& force) override;

	virtual void ApplyTorque(const math::Vector3& torque) override;

	virtual void SetMass(float mass) override;

	virtual float GetMass() const override;

	virtual void SetGravityEnabled(bool enabled, bool resetVelocity = false) override;

	virtual void OnCollision(IRigidBody* body) override;

	virtual void SetPhysicalProperties(const PhysicalProperties& props) override;

	virtual void SetLinearVelocityDamping(float damping) override;

	virtual void SetAngularVelocityDamping(float damping) override;

	virtual HexEngine::ICollider* GetICollider() override;

	virtual HexEngine::Transform* GetTransform() override;

	virtual bool IsOnGround() const { return false; }

	virtual void WakeUp() override;

	virtual void PutToSleep() override;

	physx::PxRigidActor* GetRigidActor();

	virtual void Move(const math::Vector3& dir, float minLength, float frameTime) override;

	HexEngine::RigidBody* GetBodyComponent() { return _bodyComponent; }

	virtual void SetIsTrigger(bool istrigger) override;

	virtual bool GetIsTrigger() override;

	virtual HexEngine::Entity* GetEntity() { return _entity; }

	virtual float GetMaxAngularVelocity() override;

	virtual void SetMaxAngularVelocity(float maxVel) override;

	virtual float GetMaxLinearVelocity() override;

	virtual void SetMaxLinearVelocity(float maxVel) override;

	virtual void SetIsSimulated(bool simulated) override;

	virtual bool GetIsSimulated() override;

	virtual void UpdatePosePosition(const math::Vector3& position) override;

	virtual void UpdatePoseRotation(const math::Quaternion& rotation) override;

private:
	// Snapshot of the input data + cook params for an in-flight async triangle
	// mesh cook. We copy the vertex/index arrays into the snapshot rather than
	// taking spans because the calling code (volumetric terrain) may free or
	// rebuild the source vectors before the worker finishes.
	struct AsyncTriMeshCookInput
	{
		std::vector<math::Vector3> vertices;
		std::vector<HexEngine::MeshIndexFormat> indices;
		uint32_t faceCount = 0;
		bool exclusive = true;
	};

	// Worker-thread output. cookedBuffer is the binary cooked mesh produced by
	// PxCookTriangleMesh; main thread feeds it back into createTriangleMesh to
	// produce the live PxTriangleMesh.
	struct AsyncTriMeshCookOutput
	{
		std::vector<uint8_t> cookedBuffer;
		bool success = false;
	};

	std::future<AsyncTriMeshCookOutput> _asyncCookFuture;
	std::shared_ptr<AsyncTriMeshCookInput> _asyncCookInput;
	std::atomic<bool> _asyncCookInFlight{ false };
	// Cooked-bytes archive from the most recently completed async cook.
	// Stays valid past finalise so the caller can read it (e.g. the
	// volumetric terrain stashes it for save-to-disk caching).
	std::vector<uint8_t> _lastCookedBuffer;

	HexEngine::Transform* _transform = nullptr;
	physx::PxRigidActor* _body = nullptr;
	ColliderPhysX* _collider = nullptr;
	BodyType _bodyType = BodyType::None; // default to static
	physx::PxShape* _shape = nullptr;
	HexEngine::RigidBody* _bodyComponent = nullptr;
	physx::PxGeometry* _geometry = nullptr;
	HexEngine::Entity* _entity = nullptr;
	float _mass = 1.0f;
	physx::PxMaterial* _customMaterial = nullptr;
};