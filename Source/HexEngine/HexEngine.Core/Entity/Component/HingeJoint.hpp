
#pragma once

#include "Joint.hpp"

namespace HexEngine
{
	class LineEdit;

	class HEX_API HingeJoint : public Joint
	{
	public:
		CREATE_COMPONENT_ID(HingeJoint);

		HingeJoint(Entity* entity);

		HingeJoint(Entity* entity, HingeJoint* other);

		void AttachTo(RigidBody* body, const math::Vector3& axis);

		virtual bool CreateWidget(ComponentWidget* widget) override;

	private:
		void CreateEntityList(ComponentWidget* widget, LineEdit* entitySelect);
	};
}
