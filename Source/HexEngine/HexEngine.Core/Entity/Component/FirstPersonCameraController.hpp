

#pragma once

#include "UpdateComponent.hpp"
#include "../../Input/InputSystem.hpp"

namespace HexEngine
{
	class HEX_API FirstPersonCameraController : public UpdateComponent, public IInputListener
	{
	public:
		CREATE_COMPONENT_ID(FirstPersonCameraController);

		FirstPersonCameraController(Entity* entity);

		FirstPersonCameraController(Entity* entity, FirstPersonCameraController* clone);

		virtual ~FirstPersonCameraController();

		virtual bool OnInputEvent(InputEvent event, InputData* data) override;

		virtual void Update(float frameTime) override;

	private:
		float _movementSpeed = 80.0f;
		float _strafeMovementSpeed = 70.0f;
		//float _pitchSensitivity = 200.0f;
		//float _yawSensitivity = 200.0f;
	};
}
