

#pragma once

#include "../Required.hpp"
#include "../Entity/Component/Transform.hpp"
#include "ICollider.hpp"
#include "../Scene/Mesh.hpp"

namespace HexEngine
{
	class IRigidBody
	{
	public:
		enum class BodyType
		{
			None,
			Static,
			Kinematic,
			Dynamic
		};

		enum class ColliderShape
		{
			None,
			Box,
			Sphere,
			Capsule,
			HeightField,			
			TriangleMesh
		};

		struct PhysicalProperties
		{
			float staticFriction;
			float dynamicFriction;
			float restitution;
		};

		struct ColliderData
		{
			struct Box
			{
				dx::BoundingBox aabb;
			} box;

			struct Sphere
			{
				float radius;
			} sphere;

			struct Capsule
			{
				float radius;
				float height;
			} capsule;

			struct Terrain
			{

			} terrain;

			struct Mesh
			{
				std::vector<math::Vector3> vertices;
				std::vector<MeshIndexFormat> indices;
				uint32_t faceCount;
			};

			std::vector<Mesh> meshes;
		};

		virtual void Create(Transform* transform) = 0;

		virtual void Destroy() = 0;

		virtual void SetBodyType(BodyType type) = 0;

		virtual BodyType GetBodyType() const = 0;

		virtual math::Vector3 GetPhysicsPosition() = 0;

		virtual math::Quaternion GetPhysicsRotation() = 0;

		virtual math::Vector3 GetLinearVelocity() = 0;

		virtual ICollider* AddSphereCollider(Transform* transform, float radius) = 0;

		virtual ICollider* AddCapsuleCollider(Transform* transform, float radius, float height) = 0;

		virtual ICollider* AddHeightFieldCollider(const int32_t columns, const int32_t rows, const float minHeight, const float maxHeight, float* heightValues, const math::Vector3& position, float scale=1.0f) = 0;

		virtual ICollider* AddBoxCollider(const dx::BoundingBox& box, bool exclusive) = 0;

		virtual ICollider* AddTriangleMeshCollider(const std::vector<math::Vector3>& vertices, const std::vector<MeshIndexFormat>& indices, uint32_t faceCount, bool exclusive) = 0;

		/**
		 * @brief Async two-phase triangle-mesh collider construction.
		 *
		 * PxCookTriangleMesh is pure CPU work that produces a memory blob
		 * (~50-70ms per mesh) and doesn't need the PhysX scene; the only
		 * main-thread bottleneck is the subsequent createTriangleMesh /
		 * createShape / attach calls (~5-15ms). This API lets callers (the
		 * volumetric terrain especially, with dozens of chunks all needing
		 * collision baked) kick the cook to a worker thread and finalise
		 * on the main thread when the worker reports done.
		 *
		 * Sync vs async semantics:
		 *   - sync AddTriangleMeshCollider blocks the main thread for the
		 *     full cook + finalise (~75ms).
		 *   - BeginAddTriangleMeshColliderAsync returns immediately. The
		 *     collider is NOT attached until TryFinishAsyncCollider returns
		 *     true; calls into the rigid body in the meantime see no
		 *     collider (this is correct - the chunk's collision is just
		 *     "still cooking").
		 *
		 * Returns false if the call could not be queued (already a cook
		 * in flight on this body, invalid input, plugin not init'd).
		 */
		virtual bool BeginAddTriangleMeshColliderAsync(
			const std::vector<math::Vector3>& vertices,
			const std::vector<MeshIndexFormat>& indices,
			uint32_t faceCount,
			bool exclusive) { return false; }

		/**
		 * @brief Attach a triangle-mesh collider using a pre-cooked buffer
		 *        (output of a previous PxCookTriangleMesh call).
		 *
		 * Skips the slow cook step entirely - just feeds the buffer into
		 * createTriangleMesh, builds a shape, and attaches. ~5ms per call
		 * on the main thread, vs ~75ms for the sync cook path. Used by the
		 * volumetric terrain to apply cached collision blobs read off disk.
		 *
		 * Returns the new collider on success or nullptr on failure (cooked
		 * buffer corrupt, plugin not init'd, etc.).
		 */
		virtual ICollider* AddTriangleMeshColliderFromCookedBuffer(
			const std::vector<uint8_t>& cookedBuffer,
			bool exclusive) { return nullptr; }

		/**
		 * @brief Polls the in-flight async cook started by
		 *        BeginAddTriangleMeshColliderAsync. When the worker has
		 *        finished, finalises on the main thread (createTriangleMesh
		 *        + createShape + attach) and returns true. Returns false
		 *        when the cook is still in flight or no cook was queued.
		 */
		virtual bool TryFinishAsyncCollider() { return false; }

		/** @brief True while an async cook is in flight on this body. */
		virtual bool HasAsyncColliderInFlight() const { return false; }

		/**
		 * @brief Returns the most recently completed async cook's cooked
		 *        buffer, intended for caching to disk so subsequent loads
		 *        can use AddTriangleMeshColliderFromCookedBuffer instead of
		 *        re-cooking. Cleared whenever a new async cook begins.
		 *        Empty when no cook has completed yet.
		 */
		virtual const std::vector<uint8_t>& GetLastCookedBuffer() const
		{
			static const std::vector<uint8_t> empty;
			return empty;
		}

		virtual ICollider* AddConvexMeshCollider(const std::vector<math::Vector3>& vertices, const std::vector<MeshIndexFormat>& indices, uint32_t faceCount, bool exclusive) = 0;

		virtual void UpdateBoxExtents(const dx::BoundingBox& box) = 0;

		virtual void UpdateTriangleMeshScale(const math::Vector3& scale) = 0;

		virtual void RemoveCollider() = 0;

		virtual void ApplyForceToCenterOfMass(const math::Vector3& force) = 0;

		virtual void ApplyTorque(const math::Vector3& torque) = 0;

		virtual void SetMass(float mass) = 0;

		virtual float GetMass() const = 0;

		virtual void SetGravityEnabled(bool enabled, bool resetVelocity = false) = 0;

		virtual void OnCollision(IRigidBody* body) = 0;

		virtual void SetPhysicalProperties(const PhysicalProperties& props) = 0;

		virtual void SetLinearVelocityDamping(float damping) = 0;

		virtual void SetAngularVelocityDamping(float damping) = 0;

		virtual ICollider* GetICollider() = 0;

		virtual Transform* GetTransform() = 0;

		virtual void Move(const math::Vector3& dir, float minLength, float frameTime) = 0;

		virtual bool IsOnGround() const = 0;

		virtual void WakeUp() = 0;

		virtual void PutToSleep() = 0;

		virtual void SetIsTrigger(bool istrigger) = 0;

		virtual bool GetIsTrigger() = 0;

		virtual float GetMaxAngularVelocity() = 0;

		virtual void SetMaxAngularVelocity(float maxVel) = 0;

		virtual float GetMaxLinearVelocity() = 0;

		virtual void SetMaxLinearVelocity(float maxVel) = 0;

		virtual void SetIsSimulated(bool simulated) = 0;

		virtual bool GetIsSimulated() = 0;

		virtual void UpdatePosePosition(const math::Vector3& position) = 0;

		virtual void UpdatePoseRotation(const math::Quaternion& rotation) = 0;

		// True only for character-controller bodies. Their pose is authoritative
		// from Move()/the physics read-back, so transform-change force-pose updates
		// must NOT drive them - doing so fights the controller and (because each
		// setFootPosition runs overlap recovery against world geometry) is very
		// expensive when fired every frame by e.g. mouse-look rotation.
		virtual bool IsCharacterController() const { return false; }
	};
}
