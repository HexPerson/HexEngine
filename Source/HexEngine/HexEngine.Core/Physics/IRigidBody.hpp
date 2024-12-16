

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
			float rollingResistance;
			float bounciness;
			float frictionCoefficient;
			float massDensity;
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

		virtual ICollider* AddSphereCollider(Transform* transform, float radius) = 0;

		virtual ICollider* AddCapsuleCollider(Transform* transform, float radius, float height) = 0;

		virtual ICollider* AddHeightFieldCollider(const int32_t columns, const int32_t rows, const float minHeight, const float maxHeight, float* heightValues, const math::Vector3& position, float scale=1.0f) = 0;

		virtual ICollider* AddBoxCollider(const dx::BoundingBox& box, bool exclusive) = 0;

		virtual ICollider* AddTriangleMeshCollider(const std::vector<math::Vector3>& vertices, const std::vector<MeshIndexFormat>& indices, uint32_t faceCount, bool exclusive) = 0;

		virtual ICollider* AddConvexMeshCollider(const std::vector<math::Vector3>& vertices, const std::vector<MeshIndexFormat>& indices, uint32_t faceCount, bool exclusive) = 0;

		virtual void UpdateBoxExtents(const dx::BoundingBox& box) = 0;

		virtual void UpdateTriangleMeshScale(const math::Vector3& scale) = 0;

		virtual void RemoveCollider() = 0;

		virtual void ApplyForceToCenterOfMass(const math::Vector3& force) = 0;

		virtual void ApplyTorque(const math::Vector3& torque) = 0;

		virtual void SetMass(float mass) = 0;

		virtual float GetMass() const = 0;

		virtual void SetGravityEnabled(bool enabled) = 0;

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
	};
}
