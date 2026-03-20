

#include "RTSCameraController.hpp"
#include "../Entity.hpp"
#include "../Component/Camera.hpp"
#include "../../Environment/IEnvironment.hpp"
#include "../../Input/InputSystem.hpp"
#include "../../Input/CommandManager.hpp"
#include "../../Environment/LogFile.hpp"
#include "../../Environment/TimeManager.hpp"
#include "../../Scene/SceneManager.hpp"
#include "../../Input/HCommand.hpp"
#include "RigidBody.hpp"

namespace HexEngine
{
	// Movement commands
	HEX_COMMAND(RTSMoveForward)
	{
		RTSCameraController* controller = (RTSCameraController*)param;

		if (pressed)
			controller->SetMovementFlag(RTSMoveFlag::RTSMoveForwards);
		else
			controller->ClearMovementFlag(RTSMoveFlag::RTSMoveForwards);
	}

	HEX_COMMAND(RTSMoveBackwards)
	{
		RTSCameraController* controller = (RTSCameraController*)param;

		if (pressed)
			controller->SetMovementFlag(RTSMoveFlag::RTSMoveBackwards);
		else
			controller->ClearMovementFlag(RTSMoveFlag::RTSMoveBackwards);
	}

	HEX_COMMAND(RTSMoveLeft)
	{
		RTSCameraController* controller = (RTSCameraController*)param;

		if (pressed)
			controller->SetMovementFlag(RTSMoveFlag::RTSMoveLeft);
		else
			controller->ClearMovementFlag(RTSMoveFlag::RTSMoveLeft);
	}

	HEX_COMMAND(RTSMoveRight)
	{
		RTSCameraController* controller = (RTSCameraController*)param;

		if (pressed)
			controller->SetMovementFlag(RTSMoveFlag::RTSMoveRight);
		else
			controller->ClearMovementFlag(RTSMoveFlag::RTSMoveRight);
	}

	RTSCameraController::RTSCameraController(Entity* entity) :
		UpdateComponent(entity)
	{
		g_pEnv->_inputSystem->AddInputListener(this, InputEvent::KeyDown | InputEvent::KeyUp | InputEvent::MouseDown | InputEvent::MouseUp | InputEvent::MouseWheel | InputEvent::MouseMove);

		Camera* camera = GetEntity()->GetComponent<Camera>();

		camera->SetPitch(-65.0f);

		/*g_pEnv->_commandManager->RegisterCommand(&cmd_movebackwards);
		g_pEnv->_commandManager->RegisterCommand(&cmd_moveforward);
		g_pEnv->_commandManager->RegisterCommand(&cmd_moveleft);
		g_pEnv->_commandManager->RegisterCommand(&cmd_moveright);*/

		g_pEnv->_commandManager->CreateBind('W', "RTSMoveForward", this);
		g_pEnv->_commandManager->CreateBind('S', "RTSMoveBackwards", this);
		g_pEnv->_commandManager->CreateBind('A', "RTSMoveLeft", this);
		g_pEnv->_commandManager->CreateBind('D', "RTSMoveRight", this);
	}

	RTSCameraController::RTSCameraController(Entity* entity, RTSCameraController* other) :
		UpdateComponent(entity)
	{
		g_pEnv->_inputSystem->AddInputListener(this, InputEvent::KeyDown | InputEvent::KeyUp | InputEvent::MouseDown | InputEvent::MouseUp | InputEvent::MouseWheel | InputEvent::MouseMove);

		Camera* camera = GetEntity()->GetComponent<Camera>();

		camera->SetPitch(-65.0f);

		/*g_pEnv->_commandManager->RegisterCommand(&cmd_movebackwards);
		g_pEnv->_commandManager->RegisterCommand(&cmd_moveforward);
		g_pEnv->_commandManager->RegisterCommand(&cmd_moveleft);
		g_pEnv->_commandManager->RegisterCommand(&cmd_moveright);*/

		g_pEnv->_commandManager->CreateBind('W', "RTSMoveForward");
		g_pEnv->_commandManager->CreateBind('S', "RTSMoveBackwards");
		g_pEnv->_commandManager->CreateBind('A', "RTSMoveLeft");
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

		if (_moveFlags & RTSMoveFlag::RTSMoveForwards)
		{
			auto right = -transform->GetRight();
			auto forward = right.Cross(math::Vector3::Up);
			direction += forward;
		}

		if (_moveFlags & RTSMoveFlag::RTSMoveBackwards)
		{
			auto right = -transform->GetRight();
			auto forward = right.Cross(math::Vector3::Up);
			direction += -forward;
		}

		if (_moveFlags & RTSMoveFlag::RTSMoveRight)
			direction += transform->GetRight();

		if (_moveFlags & RTSMoveFlag::RTSMoveLeft)
			direction -= transform->GetRight();

		const float AccelerationSpeed = 1200.0f;
		const float MaxSpeed = 1000.0f;

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
			_currentSpeed *= 0.9f;
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

	void RTSCameraController::SetLookAt(const math::Vector3& to)
	{
		_lookAtLocation = to;
	}

	bool RTSCameraController::OnInputEvent(InputEvent event, InputData* data)
	{
		if (event == InputEvent::MouseMove)
		{
			if (_mouseMovementEnabled && data->MouseMove.absolute == false)
			{
				Camera* camera = GetEntity()->GetComponent<Camera>();

				//if (g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera() == camera)
				{
					if (data->MouseMove.x != 0.0f)
					{
						camera->SetYaw(camera->GetYaw() - data->MouseMove.x);
					}
					if (data->MouseMove.y != 0.0f)
					{
						camera->SetPitch(camera->GetPitch() - data->MouseMove.y);
					}

					//math::Quaternion::LookRotation()
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
			_targetZoom += data->MouseWheel.delta * _zoomSpeed;
			_targetZoom = std::clamp(_targetZoom, -1000.0f, 1000.0f);
		}

		return false;
	}

	void RTSCameraController::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		
	}
}