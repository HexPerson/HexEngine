

#include "FirstPersonCameraController.hpp"
#include "RigidBody.hpp"
#include "../../Environment/IEnvironment.hpp"
#include "../../Input/InputSystem.hpp"
#include "../Entity.hpp"
#include "../Component/Camera.hpp"
#include "../../Environment/TimeManager.hpp"

namespace HexEngine
{
	FirstPersonCameraController::FirstPersonCameraController(Entity* entity) :
		UpdateComponent(entity)
	{
		g_pEnv->_inputSystem->AddInputListener(this, InputEvent::KeyDown | InputEvent::KeyUp | InputEvent::MouseDown | InputEvent::MouseUp | InputEvent::MouseWheel | InputEvent::MouseMove);
	}

	FirstPersonCameraController::FirstPersonCameraController(Entity* entity, FirstPersonCameraController* clone) :
		UpdateComponent(entity)
	{
		g_pEnv->_inputSystem->AddInputListener(this, InputEvent::KeyDown | InputEvent::KeyUp | InputEvent::MouseDown | InputEvent::MouseUp | InputEvent::MouseWheel | InputEvent::MouseMove);
	}

	FirstPersonCameraController::~FirstPersonCameraController()
	{
		g_pEnv->_inputSystem->RemoveInputListener(this);
	}

	void FirstPersonCameraController::Update(float frameTime)
	{
	}

	bool FirstPersonCameraController::OnInputEvent(InputEvent event, InputData* data)
	{
		if (event == InputEvent::MouseMove && data->MouseMove.absolute == false)
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

		return false;
	}
}