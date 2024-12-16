

#include "PlayerController.hpp"
#include "../HexEngine.Core/Environment/IEnvironment.hpp"
#include "../HexEngine.Core/Entity/InteractableEntity.hpp"

namespace CityBuilder
{
	PlayerController::PlayerController()
	{
		SetName("PlayerController");
		SetLayer(HexEngine::Layer::DynamicGeometry);
	}

	void PlayerController::Create()
	{
		Entity::Create();

		_mainCamera = HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera();

		SetPosition(math::Vector3(50.0f, 160.0f, 50.0f));

		// Create the rigid body
		//
		dx::BoundingBox playerBox;
		playerBox.Extents = math::Vector3(_bodyData.radius, _bodyData.height, _bodyData.radius);

		//_rigidBody = AddComponent<HexEngine::RigidBody>(HexEngine::IRigidBody::BodyType::Dynamic);

		HexEngine::ControllerParameters params;
		params.height = _bodyData.height;
		params.radius = _bodyData.radius;
		params.initialPosition = GetPosition();
		params.maxSlope = ToRadian(45.0f);
		params.stepOffset = 0.5f;
		params.density = _bodyData.mass;
		_controller = HexEngine::g_pEnv->_physicsSystem->CreateController(params, GetComponent<HexEngine::Transform>());
		//_rigidBody->AddBoxCollider(playerBox);
		//_rigidBody->GetIRigidBody()->SetMass(_bodyData.mass);

		/*HexEngine::IRigidBody::PhysicalProperties physicalProps;
		physicalProps.bounciness = 0.0f;
		physicalProps.frictionCoefficient = 0.6f;
		physicalProps.massDensity = 20000.1f;
		physicalProps.rollingResistance = 0.0f;

		_rigidBody->GetIRigidBody()->SetPhysicalProperties(physicalProps);*/

		//_rigidBody->GetIRigidBody()->SetGravityEnabled(false);
		//_rigidBody->GetIRigidBody()->SetLinearVelocityDamping(0.03f);
		
	}

	void PlayerController::Update(float frameTime)
	{
		Entity::Update(frameTime);

		// Update our interpolated position for smoother movement
		//
		GetComponent<HexEngine::Transform>()->UpdateInterpolatedPosition();

		auto transform = _mainCamera->GetComponent<HexEngine::Transform>();

		auto pos = transform->GetPosition();

		auto forward = transform->GetForward();
		auto right = transform->GetRight();
		auto up = transform->GetUp();

		_movement.Update(frameTime, transform, _controller);

		auto mouseState = HexEngine::g_pEnv->_inputSystem->GetMouseTracker();

		// check for pickups
		if(mouseState.leftButton == dx::Mouse::ButtonStateTracker::PRESSED)
		{
			math::Vector3 endPos;

			math::Ray ray;
			ray.position = _mainCamera->GetPosition();
			ray.direction = forward;

			auto hitEnt = HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->RayCast(
				ray,
				_pickupDistance,
				endPos,
				(uint32_t)HexEngine::Layer::DynamicGeometry,
				nullptr);

			if (hitEnt != nullptr)
			{
				hitEnt->DeleteMe();
			}
		}
		

		if (mouseState.rightButton == dx::Mouse::ButtonStateTracker::HELD)
		{
			HexEngine::InteractableEntity* bullet = new HexEngine::InteractableEntity("Models/World/Resources/WoodenPlank.obj");
			
			bullet->SetPosition(pos + forward * 16.0f);

			auto rigidBody = bullet->AddComponent<HexEngine::RigidBody>(HexEngine::IRigidBody::BodyType::Dynamic);

			auto pRigid = rigidBody->GetIRigidBody();

			

			HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->AddEntity(bullet);

			if (pRigid)
			{
				pRigid->ApplyForceToCenterOfMass(forward * 4000.0f);
				//pRigid->SetMass(40.0f);
				//pRigid->SetMass(1.0f);

				/*HexEngine::IRigidBody::PhysicalProperties physicalProps;
				physicalProps.bounciness = 0.3f;
				physicalProps.frictionCoefficient = 0.1f;
				physicalProps.massDensity = 24.0f;
				physicalProps.rollingResistance = 0.0f;

				pRigid->SetPhysicalProperties(physicalProps);*/
				bullet->SetScale(math::Vector3(3.0f));
			}
			
			//rigidBody->AddBoxCollider(bullet->GetAABB());
			
		}
	}

	void PlayerController::FixedUpdate(float frameTime)
	{
		Entity::FixedUpdate(frameTime);
	}

	void PlayerController::OnTransformChanged(bool scaleChanged, bool rotationChanged, bool translationChanged)
	{
		Entity::OnTransformChanged(scaleChanged, rotationChanged, translationChanged);

		if (translationChanged)
		{
			_mainCamera->SetPosition(GetComponent<HexEngine::Transform>()->GetInterpolatedPosition() + math::Vector3(0.0f, _bodyData.height, 0.0f));
		}

		if (rotationChanged)
		{
			//_mainCamera->SetRotation(GetRotation());
		}
	}
}