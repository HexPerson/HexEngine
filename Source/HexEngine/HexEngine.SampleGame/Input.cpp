

#include "Input.hpp"
#include "../HexEngine.Core/Entity/Component/Transform.hpp"
#include "../HexEngine.Core/Environment/IEnvironment.hpp"
#include "../HexEngine.Core/Environment/LogFile.hpp"
#include "Game.hpp"

namespace CityBuilder
{
	void Input::Create(HexEngine::Camera* camera)
	{
		_camera = camera;
	}

	void Input::OnInputEvent(HexEngine::InputEvent event, HexEngine::InputData* data)
	{
		if (!_camera)
			return;

		_camera = HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera();

		switch (event)
		{
		case HexEngine::InputEvent::KeyDown:
			HandleKeyDown(data->KeyDown.key);
			break;

		case HexEngine::InputEvent::KeyUp:
			HandleKeyUp(data->KeyUp.key);
			break;

		case HexEngine::InputEvent::MouseDown:
			HandleMouseDown(data->MouseDown.button, data->MouseDown.xpos, data->MouseDown.ypos);
			break;

		case HexEngine::InputEvent::MouseUp:
			HandleMouseUp(data->MouseUp.button/*, data->MouseUp.xpos, data->MouseUp.ypos*/);
			break;

		case HexEngine::InputEvent::MouseWheel:
			HandleMouseWheel(data->MouseWheel.delta);
			break;

		case HexEngine::InputEvent::MouseMove:
			HandleMouseMove(data->MouseMove.x, data->MouseMove.y);
			break;
		}
	}

	void Input::Update(float frameTime)
	{
		auto camera = HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera();

		auto transform = camera->GetComponent<HexEngine::Transform>();

		auto pos = transform->GetPosition();

		auto lookDir = camera->GetLookDir();

		auto forward = transform->GetForward();
		auto right = transform->GetRight();
		auto up = transform->GetUp();

		bool moving = false;

		if (HASFLAG(_movementFlags, ThirdPersonCameraMovement::ScrollForwards))
		{
			_cameraVelocity += math::Vector3(forward.x, 0.0f, forward.z) * _cameraMovementSpeed * frameTime;
			moving = true;
		}
		if (HASFLAG(_movementFlags, ThirdPersonCameraMovement::ScrollBackwards))
		{
			_cameraVelocity -= math::Vector3(forward.x, 0.0f, forward.z) * _cameraMovementSpeed * frameTime;
			moving = true;
		}
		if (HASFLAG(_movementFlags, ThirdPersonCameraMovement::ScrollLeft))
		{
			_cameraVelocity -= math::Vector3(right.x, 0.0f, right.z) * _cameraMovementSpeed * frameTime;
			moving = true;
		}
		if (HASFLAG(_movementFlags, ThirdPersonCameraMovement::ScrollRight))
		{
			_cameraVelocity += math::Vector3(right.x, 0.0f, right.z) * _cameraMovementSpeed * frameTime;
			moving = true;
		}

		if (HASFLAG(_movementFlags, ThirdPersonCameraMovement::ZoomIn))
		{
			_cameraVelocity += lookDir * _cameraScrollSpeed * frameTime;// *_scrollSpeed;
			_movementFlags &= ~ThirdPersonCameraMovement::ZoomIn;
			moving = true;
		}
		if (HASFLAG(_movementFlags, ThirdPersonCameraMovement::ZoomOut))
		{
			_cameraVelocity -= lookDir * _cameraScrollSpeed * frameTime;// *-_scrollSpeed;
			_movementFlags &= ~ThirdPersonCameraMovement::ZoomOut;
			moving = true;
		}

		if (HASFLAG(_movementFlags, ThirdPersonCameraMovement::PanLeft))
		{
			camera->SetYaw(camera->GetYaw() + 70.0f * frameTime);
		}

		if (HASFLAG(_movementFlags, ThirdPersonCameraMovement::PanRight))
		{
			camera->SetYaw(camera->GetYaw() - 70.0f * frameTime);
		}

		if (HASFLAG(_movementFlags, ThirdPersonCameraMovement::PanUp))
		{
			camera->SetPitch(camera->GetPitch() + 70.0f * frameTime);
		}

		if (HASFLAG(_movementFlags, ThirdPersonCameraMovement::PanDown))
		{
			camera->SetPitch(camera->GetPitch() - 70.0f * frameTime);
		}
		
		
		// Slow down the camera to a halt
		//
		if (/*moving == false &&*/ _cameraVelocity.Length() > 0.0f)
		{
			const float drag = min(90.0f/100.0f, 90.0f * frameTime);

			_cameraVelocity *= drag;
		}

		/*if (_cameraVelocity.Length() > 7.0f)
			_cameraVelocity *= 7.0f / _cameraVelocity.Length();*/

		if (_cameraVelocity.Length() > FLT_EPSILON)
		{
			pos += _cameraVelocity;

			transform->SetPosition(pos);
		}
	}

	void Input::HandleMouseMove(int32_t x, int32_t y)
	{
		if (_rotatingCamera)
		{
			auto camera = HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera();

			auto transform = camera->GetComponent<HexEngine::Transform>();

			float yaw = camera->GetYaw();
			float pitch = camera->GetPitch();

			if (x != 0)
				yaw -= (float)x * 0.10f;

			if (y != 0)
				pitch -= (float)y * 0.10f;

			camera->SetYaw(yaw);
			camera->SetPitch(pitch);
		}
	}

	void Input::HandleKeyDown(int32_t key)
	{
		if (key == 'W')
		{
			_movementFlags |= ThirdPersonCameraMovement::ScrollForwards;
		}

		if (key == 'A')
		{
			_movementFlags |= ThirdPersonCameraMovement::ScrollLeft;
		}

		if (key == 'D')
		{
			_movementFlags |= ThirdPersonCameraMovement::ScrollRight;
		}

		if (key == 'S')
		{
			_movementFlags |= ThirdPersonCameraMovement::ScrollBackwards;
		}

		if (key == VK_LEFT)
			_movementFlags |= ThirdPersonCameraMovement::PanLeft;

		if (key == VK_RIGHT)
			_movementFlags |= ThirdPersonCameraMovement::PanRight;

		if (key == VK_UP)
			_movementFlags |= ThirdPersonCameraMovement::PanUp;

		if (key == VK_DOWN)
			_movementFlags |= ThirdPersonCameraMovement::PanDown;

		if (key == VK_ESCAPE)
		{
			//ShowCursor(TRUE);
		}
	}

	void Input::HandleKeyUp(int32_t key)
	{
		if (key == 'W')
		{
			_movementFlags &= ~ThirdPersonCameraMovement::ScrollForwards;
		}

		if (key == 'A')
		{
			_movementFlags &= ~ThirdPersonCameraMovement::ScrollLeft;
		}

		if (key == 'D')
		{
			_movementFlags &= ~ThirdPersonCameraMovement::ScrollRight;
		}

		if (key == 'S')
		{
			_movementFlags &= ~ThirdPersonCameraMovement::ScrollBackwards;
		}

		if (key == VK_LEFT)
			_movementFlags &= ~ThirdPersonCameraMovement::PanLeft;

		if (key == VK_RIGHT)
			_movementFlags &= ~ThirdPersonCameraMovement::PanRight;

		if (key == VK_UP)
			_movementFlags &= ~ThirdPersonCameraMovement::PanUp;

		if (key == VK_DOWN)
			_movementFlags &= ~ThirdPersonCameraMovement::PanDown;
	}

	void Input::HandleMouseDown(int32_t key, int32_t xPos, int32_t yPos)
	{
		if (key == VK_LBUTTON)
		{
			/*auto ray = HexEngine::g_pEnv->_inputSystem->GetScreenToWorldRay(_camera, xPos, yPos);

			if (auto scene = HexEngine::g_pEnv->_sceneManager->GetCurrentScene(); scene != nullptr)
			{
				math::Vector3 worldPos;
				auto entity = scene->CameraPickEntity(ray, worldPos);

				if (entity)
				{
					entity->OnClicked();

					auto building = HexEngine::g_pEnv->_entitySystem->CreateEntity(scene, new Building, worldPos);
				}

			}*/
		}
		else if (key == VK_MBUTTON)
		{
			_rotatingCamera = true;
		}
	}

	void Input::HandleMouseUp(int32_t key)
	{
		if (key == VK_MBUTTON)
		{
			_rotatingCamera = false;
		}
	}

	void Input::HandleMouseWheel(float delta)
	{
		if (delta > 0.0f)
			_movementFlags |= ThirdPersonCameraMovement::ZoomIn;
		else
			_movementFlags |= ThirdPersonCameraMovement::ZoomOut;

		_scrollSpeed = delta;
	}
}