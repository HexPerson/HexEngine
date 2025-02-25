

#pragma once

#include "UpdateComponent.hpp"
#include "../../Input/InputSystem.hpp"

namespace HexEngine
{
	enum RTSMoveFlag
	{
		MoveNone = 0,
		MoveForwards = HEX_BITSET(0),
		MoveBackwards = HEX_BITSET(1),
		MoveLeft = HEX_BITSET(2),
		MoveRight = HEX_BITSET(3),
	};
	DEFINE_ENUM_FLAG_OPERATORS(RTSMoveFlag);

	class HEX_API RTSCameraController : public UpdateComponent, public IInputListener
	{
	public:
		RTSCameraController(Entity* entity);

		RTSCameraController(Entity* entity, RTSCameraController* other);

		~RTSCameraController();

		CREATE_COMPONENT_ID(RTSCameraController);

		void SetWorldConstrainedArea(const dx::BoundingBox& box);

		virtual void Update(float frameTime) override;		

		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;

		virtual bool OnInputEvent(InputEvent event, InputData* data) override;

		void SetMovementFlag(RTSMoveFlag flag);
		void ClearMovementFlag(RTSMoveFlag flag);
		void SetMouseWheelZoomSpeed(float speed) { _zoomSpeed = speed; }
		

	private:
		float _movementSpeed = 70.0f;
		float _strafeMovementSpeed = 70.0f;
		float _pitchSensitivity = 1.6f;
		float _yawSensitivity = 1.6f;
		float _zoomSpeed = 900.0f;

		std::vector<std::pair<float, float>> _mouseMovementHistory;
		bool _enableMouseSmoothing = true;

		// scrolling
		float _targetSrollValue = 0.0f;
		float _currentScrollValue = 0.0f;

		float _lastMouseX = 0;
		float _lastMouseY = 0;

		bool _mouseMovementEnabled = false;
		float _currentSpeed = 0.0f;

		// bounding area
		dx::BoundingBox _constraint;
		bool _hasConstraint = false;

		math::Vector3 _desiredMovementDir;
		RTSMoveFlag _moveFlags = MoveNone;

		float _targetZoom = 0.0f;
		float _currentZoom = 0.0f;
	};
}
