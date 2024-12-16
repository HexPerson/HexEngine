
#include "HingeJoint.hpp"
#include "../Entity.hpp"
#include "../../Environment/LogFile.hpp"
#include "../../GUI/Elements/ComponentWidget.hpp"
#include "../../GUI/Elements/LineEdit.hpp"
#include "../../GUI/Elements/EntitySelector.hpp"

namespace HexEngine
{
	HingeJoint::HingeJoint(Entity* entity) :
		Joint(entity)
	{
		if (GetEntity()->HasA<RigidBody>() == false)
		{
			GetEntity()->AddComponent<RigidBody>();
		}
	}

	HingeJoint::HingeJoint(Entity* entity, HingeJoint* other) : 
		Joint(entity, other)
	{
		if (GetEntity()->HasA<RigidBody>() == false)
		{
			GetEntity()->AddComponent<RigidBody>();
		}
	}

	void HingeJoint::AttachTo(RigidBody* body, const math::Vector3& axis)
	{
		RigidBody* thisBody = GetEntity()->GetComponent<RigidBody>();

		if (!thisBody)
		{
			LOG_CRIT("Cannot create hinge joint when attached to an entity without an existing RigidBody");
			return;
		}

		if (!body)
		{
			LOG_CRIT("Cannot create hinge joint when attaching to an entity without an existing RigidBody");
			return;
		}

		if (thisBody->GetIRigidBody()->GetBodyType() == IRigidBody::BodyType::Dynamic)
		{
			thisBody->GetIRigidBody()->SetGravityEnabled(false);
		}

		IRigidBody* bodies[2] = { thisBody->GetIRigidBody(), nullptr/*body->GetIRigidBody()*/ };
		math::Vector3 axes[2] = { axis, axis };
		math::Vector3 offsets[2] = { math::Vector3::Zero/*GetEntity()->GetWorldTM().Translation()*/, GetEntity()->GetPosition() };

		g_pEnv->_physicsSystem->CreateHingeJoint(
			bodies,
			axes,
			offsets);

		SetRigidBodies(thisBody, body);
	}

	bool HingeJoint::CreateWidget(ComponentWidget* widget)
	{
		std::wstring targetEntName;

		if (_body2)
		{
			targetEntName = std::wstring(_body2->GetEntity()->GetName().begin(), _body2->GetEntity()->GetName().end());
		}

		LineEdit* entitySelect = new LineEdit(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Connected Entity");
		entitySelect->_onClick = std::bind(&HingeJoint::CreateEntityList, this, widget, entitySelect);
		
		return true;
	}

	void HingeJoint::CreateEntityList(ComponentWidget* widget, LineEdit* entitySelect)
	{
		EntitySelector* selector = new EntitySelector(widget, Point(0, 0), Point(widget->GetSize().x, 250), L"Select Rigid Body", (ComponentSignature)(1 << RigidBody::_GetComponentId()));

		selector->BringToFront();

		selector->GetList()->_onEntityClicked = std::bind(
			[selector, entitySelect, this](EntityList* list, Entity* entity) -> void
			{
				entitySelect->SetValue(std::wstring(entity->GetName().begin(), entity->GetName().end()));

				this->AttachTo(entity->GetComponent<RigidBody>(), math::Vector3::Up);

				selector->DeleteMe();
			}, std::placeholders::_1, std::placeholders::_2);
	}
}