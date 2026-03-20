

#pragma once

#include "UpdateComponent.hpp"
#include "../../Input/InputSystem.hpp"

namespace HexEngine
{
	enum MoveFlag
	{
		MoveNone = 0,
		MoveForwards = HEX_BITSET(0),
		MoveBackwards = HEX_BITSET(1),
		MoveLeft = HEX_BITSET(2),
		MoveRight = HEX_BITSET(3),
		MoveUp = HEX_BITSET(4),
		MoveDown = HEX_BITSET(5),
		MoveSprint = HEX_BITSET(6),
	};
	DEFINE_ENUM_FLAG_OPERATORS(MoveFlag);

	class HEX_API FirstPersonCameraController : public UpdateComponent, public IInputListener
	{
	public:
		CREATE_COMPONENT_ID(FirstPersonCameraController);

		FirstPersonCameraController(Entity* entity);

		FirstPersonCameraController(Entity* entity, FirstPersonCameraController* clone);

		virtual ~FirstPersonCameraController();

		virtual bool OnInputEvent(InputEvent event, InputData* data) override;

		virtual void Update(float frameTime) override;

		void AddInputFlag(MoveFlag flag);
		void RemoveInputFlag(MoveFlag flag);

	private:
		float _movementSpeed = 40.0f;
		float _strafeMovementSpeed = 30.0f;
		MoveFlag _flags = MoveNone;
		//float _pitchSensitivity = 200.0f;
		//float _yawSensitivity = 200.0f;
	};
}
