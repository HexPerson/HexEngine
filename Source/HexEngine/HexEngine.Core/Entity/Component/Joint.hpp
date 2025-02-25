
#pragma once

#include "BaseComponent.hpp"
#include "RigidBody.hpp"
#include "../../Physics/IPhysicsSystem.hpp"

namespace HexEngine
{
	class HEX_API Joint : public BaseComponent
	{
	public:
		Joint(Entity* entity);

		Joint(Entity* entity, Joint* other);

		void SetRigidBodies(RigidBody* body1, RigidBody* body2);
		void SetJointType(JointType type);

	protected:
		RigidBody* _body1;
		RigidBody* _body2;
		JointType _type;
	};
}
