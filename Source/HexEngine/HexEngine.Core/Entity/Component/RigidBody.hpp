

#pragma once

#include "BaseComponent.hpp"
#include "../../Physics/IRigidBody.hpp"
#include "../../Audio/SoundEffect.hpp"

namespace HexEngine
{
	class Terrain;
	class Mesh;

	

	class DropDown;

	class RigidBody : public BaseComponent
	{
	public:
		CREATE_COMPONENT_ID(RigidBody);

		RigidBody(Entity* entity, IRigidBody::BodyType bodyType = IRigidBody::BodyType::Static);

		//RigidBody(Entity* entity, IRigidBody::BodyType bodyType, const math::Vector3& bodyOffset);

		RigidBody(Entity* entity, RigidBody* copy);

		//virtual void FixedUpdate(float frameTime) override;

		virtual void Destroy() override;

		void AddSphereCollider(float radius);

		void AddCapsuleCollider(float radius, float height);

		void AddTerrainCollider(Terrain* terrain);

		void AddBoxCollider(const dx::BoundingBox& box);

		void AddTriangleMeshCollider(const std::vector<math::Vector3>& vertices, const std::vector<MeshIndexFormat>& indices, uint32_t faceCount, bool exclusive);

		void AddConvexMeshCollider(const std::vector<math::Vector3>& vertices, const std::vector<MeshIndexFormat>& indices, uint32_t faceCount, bool exclusive);

		void AddTriangleMeshCollider(Mesh* mesh, bool exclusive);

		void AddConvexMeshCollider(Mesh* mesh, bool exclusive);

		void RemoveCollider();

		bool IsExclusive() const;

		void ForceUpdatePose();

		/*void ApplyForceToCenterOfMass(const math::Vector3& force);

		void SetMass(float mass);

		float GetMass();*/

		virtual void OnMessage(Message* message, MessageListener* sender);

		//void OnCollision(RigidBody* body, const math::Vector3& point);

		//void OnTrigger(RigidBody* trigger);

		IRigidBody* GetIRigidBody() { return _rigidBody; }

		IRigidBody::ColliderShape GetColliderShape() const;

		virtual void Serialize(json& data, JsonFile* file) override;

		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;

		virtual bool CreateWidget(ComponentWidget* widget) override;

		void SetMass(float mass);

		void EnableForcePoseUpdates(bool enable);

	private:
		void SetBodyTypeFromWidget(IRigidBody::BodyType type, DropDown* element);

		void AddTriangleColliderFromWidget(DropDown* widget);

		void AddBoxColliderFromWidget(DropDown* widget);

		void OnSetIsTriggerFromWidget(bool value);

		void OnSetIsGravityFromWidget(bool value);

	private:
		IRigidBody::BodyType _bodyType;
		IRigidBody* _rigidBody = nullptr;
		IRigidBody::ColliderShape _colliderShape = IRigidBody::ColliderShape::None;
		IRigidBody::ColliderData _colliderData;
		bool _exclusive = false;
		bool _isTrigger = false;
		bool _isGravityApplied = true;
		float _massAdjust = 1.0f;
		bool _forcePoseEnabled = true;
		//SoundEffect* _collisionSound = nullptr;
	};
}
