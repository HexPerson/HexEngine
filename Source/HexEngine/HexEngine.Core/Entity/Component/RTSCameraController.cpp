

#include "RTSCameraController.hpp"
#include "../Entity.hpp"
#include "../Component/Camera.hpp"
#include "../../Environment/IEnvironment.hpp"
#include "../../Input/InputSystem.hpp"
#include "../../Input/CommandManager.hpp"
#include "../../Environment/LogFile.hpp"
#include "../../Environment/TimeManager.hpp"
#include "../../Input/HCommand.hpp"
#include "RigidBody.hpp"

namespace HexEngine
{
	// Movement commands
	void IN_RTSMoveForward(CommandArgs* args, bool pressed, void* param)
	{
		RTSCameraController* controller = (RTSCameraController*)param;

		if (pressed)
			controller->SetMovementFlag(RTSMoveFlag::MoveForwards);
		else
			controller->ClearMovementFlag(RTSMoveFlag::MoveForwards);
	}
	HCommand cmd_moveforward("rtsmoveforward", IN_RTSMoveForward);

	void IN_RTSMoveBackwards(CommandArgs* args, bool pressed, void* param)
	{
		RTSCameraController* controller = (RTSCameraController*)param;

		if (pressed)
			controller->SetMovementFlag(RTSMoveFlag::MoveBackwards);
		else
			controller->ClearMovementFlag(RTSMoveFlag::MoveBackwards);
	}
	HCommand cmd_movebackwards("rtsmovebackwards", IN_RTSMoveBackwards);

	void IN_RTSMoveLeft(CommandArgs* args, bool pressed, void* param)
	{
		RTSCameraController* controller = (RTSCameraController*)param;

		if (pressed)
			controller->SetMovementFlag(RTSMoveFlag::MoveLeft);
		else
			controller->ClearMovementFlag(RTSMoveFlag::MoveLeft);
	}
	HCommand cmd_moveleft("rtsmoveleft", IN_RTSMoveLeft);

	void IN_RTSMoveRight(CommandArgs* args, bool pressed, void* param)
	{
		RTSCameraController* controller = (RTSCameraController*)param;

		if (pressed)
			controller->SetMovementFlag(RTSMoveFlag::MoveRight);
		else
			controller->ClearMovementFlag(RTSMoveFlag::MoveRight);
	}
	HCommand cmd_moveright("rtsmoveright", IN_RTSMoveRight);

	RTSCameraController::RTSCameraController(Entity* entity) :
		UpdateComponent(entity)
	{
		g_pEnv->_inputSystem->AddInputListener(this, InputEvent::KeyDown | InputEvent::KeyUp | InputEvent::MouseDown | InputEvent::MouseUp | InputEvent::MouseWheel | InputEvent::MouseMove);

		Camera* camera = GetEntity()->GetComponent<Camera>();

		camera->SetPitch(-65.0f);

		g_pEnv->_commandManager->CreateBind('W', "rtsmoveforward", this);
		g_pEnv->_commandManager->CreateBind('S', "rtsmovebackwards", this);
		g_pEnv->_commandManager->CreateBind('A', "rtsmoveleft", this);
		g_pEnv->_commandManager->CreateBind('D', "rtsmoveright", this);
	}

	RTSCameraController::RTSCameraController(Entity* entity, RTSCameraController* other) :
		UpdateComponent(entity)
	{
		g_pEnv->_inputSystem->AddInputListener(this, InputEvent::KeyDown | InputEvent::KeyUp | InputEvent::MouseDown | InputEvent::MouseUp | InputEvent::MouseWheel | InputEvent::MouseMove);

		Camera* camera = GetEntity()->GetComponent<Camera>();

		camera->SetPitch(-65.0f);

		g_pEnv->_commandManager->CreateBind('W', "rtsmoveforward");
		g_pEnv->_commandManager->CreateBind('S', "rtsmovebackwards");
		g_pEnv->_commandManager->CreateBind('A', "rtsmoveleft");
		g_pEnv->_commandManager->CreateBind('D', "rtsmoveright");
	}

	RTSCameraController::~RTSCameraController()
	{
		g_pEnv->_inputSystem->RemoveInputListener(this);
	}

	void RTSCameraController::SetWorldConstrainedArea(const dx::BoundingBox& box)
	{
		_hasConstraint = true;
		_constraint = box;
	}

	void RTSCameraController::SetMovementFlag(RTSMoveFlag flag)
	{
		_moveFlags |= flag;
	}

	void RTSCameraController::ClearMovementFlag(RTSMoveFlag flag)
	{
		_moveFlags &= ~flag;
	}

	void RTSCameraController::Update(float frameTime)
	{
		math::Vector3 direction;

		Transform* transform = GetEntity()->GetComponent<Transform>();

		if (_moveFlags & RTSMoveFlag::MoveForwards)
		{
			auto right = -transform->GetRight();
			auto forward = right.Cross(math::Vector3::Up);
			direction += forward;
		}

		if (_moveFlags & RTSMoveFlag::MoveBackwards)
		{
			auto right = -transform->GetRight();
			auto forward = right.Cross(math::Vector3::Up);
			direction += -forward;
		}

		if (_moveFlags & RTSMoveFlag::MoveRight)
			direction += transform->GetRight();

		if (_moveFlags & RTSMoveFlag::MoveLeft)
			direction -= transform->GetRight();

		const float AccelerationSpeed = 800.0f;
		const float MaxSpeed = 180.0f;

		if (_targetZoom != 0.0f)
		{
			direction += transform->GetForward() * (_targetZoom > 0.0f ? 1.0f : - 1.0f);

			float accelSpeed = frameTime * AccelerationSpeed;

			if (_targetZoom > 0.0f)
				_targetZoom -= accelSpeed > _targetZoom ? _targetZoom : accelSpeed;
			else if (_targetZoom < 0.0f)
				_targetZoom += accelSpeed > -_targetZoom ? -_targetZoom : accelSpeed;
		}

		if (direction.Length() > FLT_EPSILON)
		{
			direction.Normalize();

			_desiredMovementDir = direction;

			_currentSpeed += frameTime * AccelerationSpeed;
		}
		else
		{
			_currentSpeed -= frameTime * AccelerationSpeed;
		}

		

		_currentSpeed = std::clamp(_currentSpeed, 0.0f, MaxSpeed);

		if (_desiredMovementDir.Length() > FLT_EPSILON && _currentSpeed > FLT_EPSILON)
		{
			Transform* transform = GetEntity()->GetComponent<Transform>();

			auto currentPos = transform->GetPosition();

			currentPos += _desiredMovementDir * frameTime * _currentSpeed;

			transform->SetPosition(currentPos);

			//LOG_DEBUG("[%d] Updated camera position to %.3f %.3f %.3f", g_pEnv->_timeManager->_frameCount, currentPos.x, currentPos.y, currentPos.z);
		}
	}

	bool RTSCameraController::OnInputEvent(InputEvent event, InputData* data)
	{
		if (event == InputEvent::MouseMove)
		{
			if (_mouseMovementEnabled)
			{
				Camera* camera = GetEntity()->GetComponent<Camera>();

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
		else if (event == InputEvent::MouseDown && data->MouseDown.button == VK_RBUTTON)
		{
			_mouseMovementEnabled = true;

			g_pEnv->_inputSystem->SetMouseLockMode(MouseLockMode::Locked);
		}
		else if (event == InputEvent::MouseUp && data->MouseDown.button == VK_RBUTTON)
		{
			_mouseMovementEnabled = false;

			g_pEnv->_inputSystem->SetMouseLockMode(MouseLockMode::Free);
		}
		else if (event == InputEvent::MouseWheel)
		{
			_targetZoom += data->MouseWheel.delta * 50.0f;
		}

		return false;
	}

	void RTSCameraController::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		
	}
}