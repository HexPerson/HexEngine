

#pragma once

#include "../Required.hpp"
#include "IRigidBody.hpp"

namespace HexEngine
{
	class IController
	{
	public:
		virtual void Move(const math::Vector3& dir, float minLength, float frameTime) = 0;
	};
}
