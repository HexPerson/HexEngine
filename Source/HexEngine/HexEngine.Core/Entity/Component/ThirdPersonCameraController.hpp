

#pragma once

#include "UpdateComponent.hpp"
#include "../../Input/InputSystem.hpp"

namespace HexEngine
{
	class HEX_API ThirdPersonCameraController : public UpdateComponent, public IInputListener
	{
	public:
		CREATE_COMPONENT_ID(ThirdPersonCameraController);

		ThirdPersonCameraController(Entity* entity);

		ThirdPersonCameraController(Entity* entity, ThirdPersonCameraController* clone);

		~ThirdPersonCameraController();

		virtual bool OnInputEvent(InputEvent event, InputData* data) override;

		virtual void Update(float frameTime) override;

		void SetLookAtTarget(Transform* transform);
		const math::Vector3& GetThirdPersonPosition() const;
		Transform* GetLookAtTarget() const;

		void SetViewOffset(float right, float up, float forward);
		void SetTargetOffset(const math::Vector3& offset);

		void SetSpringSpeeds(float rotation, float position);

	private:
		float _movementSpeed = 80.0f;
		float _strafeMovementSpeed = 70.0f;
		math::Vector3 _thirdPersonPosition;
		Transform* _lookAtTarget = nullptr;
		math::Vector3 _viewOffset;
		math::Vector3 _targetOffset;

		float _physicsRadius = 5.0f;
		float _lastMouseInputTime = 0.0f;
		float _cameraResetStartTime = 0.0f;
		float _targetYaw = 0.0f;
		float _lastTargetYawChange = 0.0f;
		bool _forceCameraReset = true;

		float _rotationSpringSpeed = 1.8f;
		float _positionSpringSpeed = 1.3f;
		//float _pitchSensitivity = 200.0f;
		//float _yawSensitivity = 200.0f;
	};
}
