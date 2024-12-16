
#pragma once

#include "UpdateComponent.hpp"

namespace HexEngine
{
	class MotorComponent : public UpdateComponent
	{
	public:
		CREATE_COMPONENT_ID(MotorComponent);

		MotorComponent(Entity* entity) :
			UpdateComponent(entity)
		{}

		MotorComponent(Entity* entity, MotorComponent* copy) : UpdateComponent(entity) {}

		virtual void Update(float frameTime) override;

		void SetSpeed(float speed);
		void SetDirection(const math::Vector3& direction);

	private:
		float _speed = 1.0f;
		math::Vector3 _direction;
	};
}
