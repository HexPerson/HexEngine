

#include "FirstPersonCameraController.hpp"
#include "RigidBody.hpp"
#include "../../Environment/IEnvironment.hpp"
#include "../../Input/InputSystem.hpp"
#include "../Entity.hpp"
#include "../Component/Camera.hpp"
#include "../../Environment/TimeManager.hpp"
#include "../../Scene/SceneManager.hpp"
#include "../../Input/CommandManager.hpp"
#include "../../Input/HCommand.hpp"
#include <cfloat>

namespace HexEngine
{
	HEX_COMMAND(MoveForwards)
	{
		auto controller = reinterpret_cast<FirstPersonCameraController*>(param);

		if (pressed)
			controller->AddInputFlag(MoveFlag::MoveForwards);
		else
			controller->RemoveInputFlag(MoveFlag::MoveForwards);
	}

	HEX_COMMAND(MoveBackwards)
	{
		auto controller = reinterpret_cast<FirstPersonCameraController*>(param);

		if (pressed)
			controller->AddInputFlag(MoveFlag::MoveBackwards);
		else
			controller->RemoveInputFlag(MoveFlag::MoveBackwards);
	}

	HEX_COMMAND(MoveLeft)
	{
		auto controller = reinterpret_cast<FirstPersonCameraController*>(param);

		if (pressed)
			controller->AddInputFlag(MoveFlag::MoveLeft);
		else
			controller->RemoveInputFlag(MoveFlag::MoveLeft);
	}

	HEX_COMMAND(MoveRight)
	{
		auto controller = reinterpret_cast<FirstPersonCameraController*>(param);

		if (pressed)
			controller->AddInputFlag(MoveFlag::MoveRight);
		else
			controller->RemoveInputFlag(MoveFlag::MoveRight);
	}

	HEX_COMMAND(MoveUp)
	{
		auto controller = reinterpret_cast<FirstPersonCameraController*>(param);

		if (pressed)
			controller->AddInputFlag(MoveFlag::MoveUp);
		else
			controller->RemoveInputFlag(MoveFlag::MoveUp);
	}

	HEX_COMMAND(MoveDown)
	{
		auto controller = reinterpret_cast<FirstPersonCameraController*>(param);

		if (pressed)
			controller->AddInputFlag(MoveFlag::MoveDown);
		else
			controller->RemoveInputFlag(MoveFlag::MoveDown);
	}

	HEX_COMMAND(MoveRun)
	{
		auto controller = reinterpret_cast<FirstPersonCameraController*>(param);

		if (pressed)
			controller->AddInputFlag(MoveFlag::MoveSprint);
		else
			controller->RemoveInputFlag(MoveFlag::MoveSprint);
	}

	FirstPersonCameraController::FirstPersonCameraController(Entity* entity) :
		UpdateComponent(entity)
	{
		if (auto* transform = entity->GetComponent<Transform>())
			transform->EnableInterpolation(true);

		g_pEnv->_inputSystem->AddInputListener(this, InputEvent::KeyDown | InputEvent::KeyUp | InputEvent::MouseDown | InputEvent::MouseUp | InputEvent::MouseWheel | InputEvent::MouseMove);

		g_pEnv->_commandManager->CreateBind('W', "MoveForwards", this);
		g_pEnv->_commandManager->CreateBind('S', "MoveBackwards", this);
		g_pEnv->_commandManager->CreateBind('A', "MoveLeft", this);
		g_pEnv->_commandManager->CreateBind('D', "MoveRight", this);
		g_pEnv->_commandManager->CreateBind(VK_SPACE, "MoveUp", this);
		g_pEnv->_commandManager->CreateBind(VK_CONTROL, "MoveDown", this);
		g_pEnv->_commandManager->CreateBind(VK_SHIFT, "MoveRun", this);
	}

	FirstPersonCameraController::FirstPersonCameraController(Entity* entity, FirstPersonCameraController* clone) :
		UpdateComponent(entity)
	{
		if (auto* transform = entity->GetComponent<Transform>())
			transform->EnableInterpolation(true);

		g_pEnv->_inputSystem->AddInputListener(this, InputEvent::KeyDown | InputEvent::KeyUp | InputEvent::MouseDown | InputEvent::MouseUp | InputEvent::MouseWheel | InputEvent::MouseMove);

		g_pEnv->_commandManager->CreateBind('W', "MoveForwards", this);
		g_pEnv->_commandManager->CreateBind('S', "MoveBackwards", this);
		g_pEnv->_commandManager->CreateBind('A', "MoveLeft", this);
		g_pEnv->_commandManager->CreateBind('D', "MoveRight", this);
		g_pEnv->_commandManager->CreateBind(VK_SPACE, "MoveUp", this);
		g_pEnv->_commandManager->CreateBind(VK_CONTROL, "MoveDown", this);
		g_pEnv->_commandManager->CreateBind(VK_SHIFT, "MoveRun", this);
	}

	FirstPersonCameraController::~FirstPersonCameraController()
	{
		g_pEnv->_inputSystem->RemoveInputListener(this);
	}

	void FirstPersonCameraController::Update(float frameTime)
	{
		(void)frameTime;
	}

	void FirstPersonCameraController::FixedUpdate(float frameTime)
	{
		if (frameTime <= 0.0f)
			return;

		auto* transform = GetEntity()->GetComponent<Transform>();
		auto* rigidBody = GetEntity()->GetComponent<RigidBody>();
		auto* bodyController = rigidBody ? rigidBody->GetIRigidBody() : nullptr;

		if (!transform || !bodyController)
			return;

		math::Vector3 planarRight = transform->GetRight();
		planarRight.y = 0.0f;

		if (planarRight.Length() <= FLT_EPSILON)
			planarRight = math::Vector3::Right;
		else
			planarRight.Normalize();

		math::Vector3 planarForward = -planarRight.Cross(math::Vector3::Up);

		if (planarForward.Length() <= FLT_EPSILON)
			planarForward = math::Vector3::Forward;
		else
			planarForward.Normalize();

		math::Vector3 planarVelocity;

		if ((_flags & MoveFlag::MoveForwards) != 0)
		{
			bool run = (_flags & MoveFlag::MoveSprint) != 0;
			const float moveSpeed = run ? _movementSpeed * _runMultiplier : _movementSpeed;

			planarVelocity += planarForward * moveSpeed;
		}

		if ((_flags & MoveFlag::MoveBackwards) != 0)
			planarVelocity -= planarForward * _movementSpeed;

		if ((_flags & MoveFlag::MoveRight) != 0)
			planarVelocity += planarRight * _strafeMovementSpeed;

		if ((_flags & MoveFlag::MoveLeft) != 0)
			planarVelocity -= planarRight * _strafeMovementSpeed;

		const bool isOnGround = bodyController->IsOnGround();
		if (isOnGround && _verticalVelocity < 0.0f)
			_verticalVelocity = 0.0f;
		else if (!isOnGround)
			_verticalVelocity -= _gravityAcceleration * frameTime;

		float manualVerticalVelocity = 0.0f;
		if ((_flags & MoveFlag::MoveUp) != 0)
			manualVerticalVelocity += _verticalMovementSpeed;

		if ((_flags & MoveFlag::MoveDown) != 0)
			manualVerticalVelocity -= _verticalMovementSpeed;

		math::Vector3 displacement = (planarVelocity + (math::Vector3::Up * (_verticalVelocity + manualVerticalVelocity))) * frameTime;
		bodyController->Move(displacement, 0.0f, frameTime);
	}

	void FirstPersonCameraController::AddInputFlag(MoveFlag flag)
	{
		_flags |= flag;
	}

	void FirstPersonCameraController::RemoveInputFlag(MoveFlag flag)
	{
		_flags &= ~flag;
	}

	bool FirstPersonCameraController::OnInputEvent(InputEvent event, InputData* data)
	{
		if (event == InputEvent::MouseMove && data->MouseMove.absolute == false)
		{
			Camera* camera = GetEntity()->GetComponent<Camera>();

			if (g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera() == camera)
			{
				if (data->MouseMove.x != 0.0f)
				{
					camera->SetYaw(camera->GetYaw() - data->MouseMove.x);
				}
				if (data->MouseMove.y != 0.0f)
				{
					camera->SetPitch(camera->GetPitch() - data->MouseMove.y);
				}
			}
		}

		return false;
	}
}
