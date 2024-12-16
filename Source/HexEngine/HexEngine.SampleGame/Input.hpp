

#pragma once

#include "../HexEngine.Core/Input/InputSystem.hpp"
#include "../HexEngine.Core/Entity/Camera.hpp"

namespace CityBuilder
{
	enum class ThirdPersonCameraMovement
	{
		None = 0,
		ScrollForwards = BITSET(0),
		ScrollBackwards = BITSET(1),
		ScrollLeft = BITSET(2),
		ScrollRight = BITSET(3),
		ZoomIn = BITSET(4),
		ZoomOut = BITSET(5),
		PanLeft = BITSET(6),
		PanRight = BITSET(7),
		PanUp = BITSET(8),
		PanDown = BITSET(9)
	};

	DEFINE_ENUM_FLAG_OPERATORS(ThirdPersonCameraMovement);

	class Input : public HexEngine::IInputListener
	{
	public:
		void Create(HexEngine::Camera* camera);

		virtual void OnInputEvent(HexEngine::InputEvent event, HexEngine::InputData* data) override;

		void HandleKeyDown(int32_t key);
		void HandleKeyUp(int32_t key);
		void HandleMouseDown(int32_t key, int32_t xPos, int32_t yPos);
		void HandleMouseUp(int32_t key);
		void HandleMouseWheel(float delta);
		void HandleMouseMove(int32_t x, int32_t y);

		void Update(float frameTime);

	private:
		HexEngine::Camera* _camera = nullptr;
		ThirdPersonCameraMovement _movementFlags = ThirdPersonCameraMovement::None;
		float _cameraMovementSpeed = 100.0f;
		float _cameraScrollSpeed = 220.0f;
		math::Vector3 _cameraVelocity;
		float _scrollSpeed = 0.0f;
		bool _rotatingCamera = false;
	};
}
