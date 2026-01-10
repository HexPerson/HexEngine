

#include "ThirdPersonCameraController.hpp"
#include "RigidBody.hpp"
#include "../../Environment/IEnvironment.hpp"
#include "../../Input/InputSystem.hpp"
#include "../Entity.hpp"
#include "../Component/Camera.hpp"
#include "../../Environment/TimeManager.hpp"
#include "../../Physics/IPhysicsSystem.hpp"
#include "../../Scene/Scene.hpp"
#include "../../Scene/SceneManager.hpp"
#include "../../Physics/PhysUtils.hpp"
#include "../../Input/Console.hpp"
#include "../../Input/CommandManager.hpp"
#include "../../Math/easing.h"

namespace HexEngine
{
	ThirdPersonCameraController::ThirdPersonCameraController(Entity* entity) :
		UpdateComponent(entity)
	{
		g_pEnv->_inputSystem->AddInputListener(this, InputEvent::KeyDown | InputEvent::KeyUp | InputEvent::MouseDown | InputEvent::MouseUp | InputEvent::MouseWheel | InputEvent::MouseMove);
	}

	ThirdPersonCameraController::ThirdPersonCameraController(Entity* entity, ThirdPersonCameraController* clone) :
		UpdateComponent(entity)
	{
		g_pEnv->_inputSystem->AddInputListener(this, InputEvent::KeyDown | InputEvent::KeyUp | InputEvent::MouseDown | InputEvent::MouseUp | InputEvent::MouseWheel | InputEvent::MouseMove);
	}

	ThirdPersonCameraController::~ThirdPersonCameraController()
	{
		g_pEnv->_inputSystem->RemoveInputListener(this);
	}

	void ThirdPersonCameraController::Update(float frameTime)
	{
		auto transform = GetEntity()->GetComponent<Transform>();
		auto camera = GetEntity()->GetComponent<Camera>();

		if (_lookAtTarget)
		{
			math::Vector3 targetPosition = _lookAtTarget->GetPosition() + _targetOffset;

			_thirdPersonPosition = targetPosition + (_lookAtTarget->GetForward() * _viewOffset.z) + (_lookAtTarget->GetRight() * _viewOffset.x) + (math::Vector3::Up * _viewOffset.y);

			auto dirFromCameraToTarget = (_thirdPersonPosition - targetPosition);
			float distance = dirFromCameraToTarget.Length();
			dirFromCameraToTarget.Normalize();

			math::Ray ray;
			ray.position = targetPosition;
			ray.direction = dirFromCameraToTarget;

#if 0
			RayHit hit;
			if(PhysUtils::RayCast(
				ray,
				distance + _physicsRadius,
				LAYERMASK(Layer::StaticGeometry),
				&hit))
			{
				if ((hit.position - targetPosition).Length() < 2.0f)
				{
					//__debugbreak();
				}
				math::Vector3 newPos;
				newPos.x = hit.position.x - dirFromCameraToTarget.x * _physicsRadius;
				newPos.y = hit.position.y;
				newPos.z = hit.position.z - dirFromCameraToTarget.z * _physicsRadius;

				math::Vector3 targetPos = newPos;
				targetPos.y = _thirdPersonPosition.y;

				_thirdPersonPosition = targetPos;

				/*if (PhysUtils::RayCast(newPos, targetPos, LAYERMASK(Layer::StaticGeometry), &hit))
				{
					auto dir = (newPos = targetPos);
					dir.Normalize();

					
					_thirdPersonPosition.y = hit.position.y - dir.y * 6.0f;
				}*/
			}
			else
			{
				bool a = false;
			}
#endif

			if (g_pEnv->_timeManager->_currentTime - _lastMouseInputTime > 1.7f || _lastMouseInputTime == 0.0f)
			{
				if (_viewRestoreTime == 0.0f)
				{
					_viewRestoreTime = g_pEnv->_timeManager->_currentTime;
				}

				float viewRestoreDelta = std::clamp(g_pEnv->_timeManager->_currentTime - _viewRestoreTime, 0.0f, 1.0f);
				const auto easingFunc = getEasingFunction(EaseInExpo);
				float newTargetYaw = ToDegree(_lookAtTarget->GetYaw());
				
				float currentYaw = camera->GetYaw();
				float delta = newTargetYaw - currentYaw;

				if (delta > 180.0f)
					newTargetYaw -= 360.0f;
				else if (delta < -180.0f)
					newTargetYaw += 360.0f;

				camera->SetYaw(std::lerp(currentYaw, newTargetYaw, frameTime * _rotationSpringSpeed * easingFunc(viewRestoreDelta)));

				// pitch

				float newTargetPitch = _targetPitch;

				float currentPitch = camera->GetPitch();

				delta = newTargetPitch - currentPitch;

				if (delta > 180.0f)
					newTargetPitch -= 360.0f;
				else if (delta < -180.0f)
					newTargetPitch += 360.0f;

				camera->SetPitch(std::lerp(currentPitch, newTargetPitch, frameTime * _rotationSpringSpeed * easingFunc(viewRestoreDelta)));
			}
			else
			{
				_cameraResetStartTime = g_pEnv->_timeManager->_currentTime + 1.0f;
			}
		}
		else
		{
			_thirdPersonPosition = transform->GetPosition();
		}

		if (_forceCameraReset)
		{
			transform->SetPosition(_thirdPersonPosition);
			_forceCameraReset = false;
		}
		else
		{
			auto cameraPos = math::Vector3::Lerp(transform->GetPosition(), _thirdPersonPosition, frameTime * _positionSpringSpeed);
			transform->SetPosition(cameraPos);
		}
		
	}

	void ThirdPersonCameraController::SetSpringSpeeds(float rotation, float position)
	{
		_rotationSpringSpeed = rotation;
		_positionSpringSpeed = position;
	}

	const math::Vector3& ThirdPersonCameraController::GetThirdPersonPosition() const
	{
		return _thirdPersonPosition;
		
	}

	void ThirdPersonCameraController::SetLookAtTarget(Transform* transform)
	{
		_lookAtTarget = transform;
	}

	Transform* ThirdPersonCameraController::GetLookAtTarget() const
	{
		return _lookAtTarget;
	}

	void ThirdPersonCameraController::SetViewOffset(float right, float up, float forward)
	{
		_viewOffset = math::Vector3(right, up, forward);
	}

	void ThirdPersonCameraController::SetTargetOffset(const math::Vector3& offset)
	{
		_targetOffset = offset;
	}

	void ThirdPersonCameraController::SetTargetPitch(float pitch)
	{
		_targetPitch = pitch;
	}

	bool ThirdPersonCameraController::OnInputEvent(InputEvent event, InputData* data)
	{
		if (event == InputEvent::MouseMove && data->MouseMove.absolute == false)
		{
			Camera* camera = GetEntity()->GetComponent<Camera>();

			if (data->MouseMove.x != 0.0f)
			{
				camera->SetYaw(camera->GetYaw() - data->MouseMove.x);

				_lastMouseInputTime = g_pEnv->_timeManager->_currentTime;
				_viewRestoreTime = 0.0f;
			}
			if (data->MouseMove.y != 0.0f)
			{
				camera->SetPitch(camera->GetPitch() - data->MouseMove.y);

				_lastMouseInputTime = g_pEnv->_timeManager->_currentTime;
				_viewRestoreTime = 0.0f;
			}
		}

		return false;
	}
}