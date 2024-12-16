

#include "PlayerMovement.hpp"
#include "../HexEngine.Core/Environment/IEnvironment.hpp"

namespace CityBuilder
{
	void PlayerMovement::Update(float frameTime, HexEngine::Transform* transform, HexEngine::IRigidBody* controller)
	{
		auto pos = transform->GetPosition();

		auto forward = transform->GetForward();
		auto right = transform->GetRight();
		auto up = transform->GetUp();

		auto state = dx::Keyboard::Get().GetState();
		auto tracker = HexEngine::g_pEnv->_inputSystem->GetKeyboardTracker();

		const float moveForce = 220.0f;
		const float jumpForce = 6200.0f;
		const float acceleration = 8.0f;

		math::Vector3 desiredDirection;

		if (state.W)
		{
			desiredDirection += forward * moveForce * frameTime;
		}
		if (state.S)
		{
			desiredDirection += forward * -moveForce * frameTime;
		}
		if (state.A)
		{
			desiredDirection += right * -moveForce * frameTime;
		}
		if (state.D)
		{
			desiredDirection += right * moveForce * frameTime;
		}
		if (tracker.IsKeyPressed(dx::Keyboard::Keys::Space))
		{
			desiredDirection += math::Vector3::Up * jumpForce * frameTime;
		}

		if (desiredDirection.Length() <= FLT_EPSILON)
		{
			_acceleration -= frameTime * acceleration;
		}
		else
		{
			_acceleration += frameTime * acceleration;
			_currentDirection = desiredDirection;
		}

		_acceleration = std::clamp(_acceleration, 0.0f, 1.0f);

		math::Vector3 gravity = math::Vector3(0.0f, -9.81f, 0.0f) * frameTime * controller->GetMass() * 0.000012f;

		//_targetDirection = desiredDirection;

		/*if (_currentDirection != _targetDirection)
		{
			_currentDirection = math::Vector3::Lerp(_currentDirection, _targetDirection, _acceleration);			
		}*/
		//_currentDirection *= _acceleration;

		//if (_targetDirection.Length() > 0.0f)
		{
			controller->Move((_currentDirection * _acceleration) + gravity, 0.0f, frameTime);
		}
	}
}