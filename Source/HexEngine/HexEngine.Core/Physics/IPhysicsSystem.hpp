
#pragma once

#include "../Plugin/IPlugin.hpp"
#include "IRigidBody.hpp"
#include "IController.hpp"
#include "../Entity/Component/Transform.hpp"

namespace HexEngine
{
	class RigidBody;
	class Material;

	struct ControllerParameters
	{
		math::Vector3 initialPosition;
		float maxSlope;
		float height;
		float radius;
		float density;
		float stepOffset;
	};

	struct RayHit
	{
		math::Vector3 start;
		math::Vector3 position;
		math::Vector3 normal;
		Entity* entity = nullptr;
		float distance = 0.0f;
		Material* material = nullptr;
	};

	enum class JointType
	{
		Hinge
	};

	class IPhysicsSystem : public IPluginInterface
	{
	public:
		DECLARE_PLUGIN_INTERFACE(IPhysicsSystem, 001);

		virtual IRigidBody* CreateRigidBody(Transform* transform, RigidBody* bodyComponent, IRigidBody::BodyType type, const math::Vector3& offset = math::Vector3::Zero) = 0;

		virtual IRigidBody* CloneRigidBody(IRigidBody* physicsBody, Transform* transform, RigidBody* bodyComponent, IRigidBody::BodyType type) = 0;

		virtual void DestroyRigidBody(IRigidBody* body) = 0;

		virtual void Update(float frameTime) = 0;

		virtual void Simulate(float simulationTime) = 0;

		virtual void DebugRender() = 0;

		virtual IRigidBody* CreateController(const ControllerParameters& params, Transform* transform) = 0;

		virtual void LockWrite() = 0;

		virtual void UnlockWrite() = 0;

		virtual uint32_t RayCast(const math::Vector3& from, const math::Vector3& unitDir, float maxDist, IRigidBody* body, RayHit* hitInfo) = 0;

		virtual math::Vector3 GetGravity() = 0;

		virtual void SetGravity(const math::Vector3& gravity) = 0;

		virtual void CreateHingeJoint(IRigidBody* bodies[2], const math::Vector3 axes[2], const math::Vector3 offsets[2]) = 0;
	};
}
