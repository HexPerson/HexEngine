

#pragma once

#include "../HexEngine.Core/Entity/Component/Transform.hpp"
#include "../HexEngine.Core/Physics/IRigidBody.hpp"

namespace CityBuilder
{
	class PlayerMovement
	{
	public:
		void Update(float frameTime, HexEngine::Transform* transform, HexEngine::IRigidBody* controller);

	private:
		math::Vector3 _targetDirection;
		math::Vector3 _currentDirection;
		float _acceleration = 0.0f;
	};
}
