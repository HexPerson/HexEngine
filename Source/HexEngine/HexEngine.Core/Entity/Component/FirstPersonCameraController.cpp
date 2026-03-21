

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

	FirstPersonCameraController::FirstPersonCameraController(Entity* entity) :
		UpdateComponent(entity)
	{
		g_pEnv->_inputSystem->AddInputListener(this, InputEvent::KeyDown | InputEvent::KeyUp | InputEvent::MouseDown | InputEvent::MouseUp | InputEvent::MouseWheel | InputEvent::MouseMove);

		g_pEnv->_commandManager->CreateBind('W', "MoveForwards", this);
		g_pEnv->_commandManager->CreateBind('S', "MoveBackwards", this);
		g_pEnv->_commandManager->CreateBind('A', "MoveLeft", this);
		g_pEnv->_commandManager->CreateBind('D', "MoveRight", this);
		g_pEnv->_commandManager->CreateBind(VK_SPACE, "MoveUp", this);
		g_pEnv->_commandManager->CreateBind(VK_CONTROL, "MoveDown", this);
	}

	FirstPersonCameraController::FirstPersonCameraController(Entity* entity, FirstPersonCameraController* clone) :
		UpdateComponent(entity)
	{
		g_pEnv->_inputSystem->AddInputListener(this, InputEvent::KeyDown | InputEvent::KeyUp | InputEvent::MouseDown | InputEvent::MouseUp | InputEvent::MouseWheel | InputEvent::MouseMove);

		g_pEnv->_commandManager->CreateBind('W', "MoveForwards", this);
		g_pEnv->_commandManager->CreateBind('S', "MoveBackwards", this);
		g_pEnv->_commandManager->CreateBind('A', "MoveLeft", this);
		g_pEnv->_commandManager->CreateBind('D', "MoveRight", this);
		g_pEnv->_commandManager->CreateBind(VK_SPACE, "MoveUp", this);
		g_pEnv->_commandManager->CreateBind(VK_CONTROL, "MoveDown", this);
	}

	FirstPersonCameraController::~FirstPersonCameraController()
	{
		g_pEnv->_inputSystem->RemoveInputListener(this);
	}

	void FirstPersonCameraController::Update(float frameTime)
	{
		math::Vector3 moveDir;
		auto transform = GetEntity()->GetComponent<Transform>();
		auto rigidBody = GetEntity()->GetComponent<RigidBody>();
		auto bodyController = rigidBody ? rigidBody->GetIRigidBody() : nullptr;

		if (!bodyController)
			return;

		const float kMoveDilation = 0.001f;

		auto right = transform->GetRight();
		auto absForwards = -right.Cross(math::Vector3::Up);

		if ((_flags & MoveFlag::MoveForwards) != 0)
		{
			moveDir = absForwards * _movementSpeed * kMoveDilation;
		}

		if ((_flags & MoveFlag::MoveBackwards) != 0)
		{
			moveDir = -absForwards * _movementSpeed * kMoveDilation;
		}

		if ((_flags & MoveFlag::MoveRight) != 0)
		{
			moveDir = right * _movementSpeed * kMoveDilation;
		}

		if ((_flags & MoveFlag::MoveLeft) != 0)
		{
			moveDir = -right * _movementSpeed * kMoveDilation;
		}

		if (bodyController->IsOnGround() == false)
		{
			moveDir.y -= 9.81f * kMoveDilation * 4.0f;
		}

		if (moveDir.Length() > 0.0f)
		{
			bool a = false;
		}

		bodyController->Move(moveDir, 0.0f, frameTime);
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