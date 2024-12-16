
#include "Joint.hpp"

namespace HexEngine
{
	Joint::Joint(Entity* entity) :
		BaseComponent(entity),
		_body1(nullptr),
		_body2(nullptr)
	{}

	Joint::Joint(Entity* entity, Joint* other) :
		BaseComponent(entity)
	{
		SetRigidBodies(other->_body1, other->_body2);
	}

	void Joint::SetRigidBodies(RigidBody* body1, RigidBody* body2)
	{
		_body1 = body1;
		_body2 = body2;
	}

	void Joint::SetJointType(JointType type)
	{
		_type = type;
	}
}