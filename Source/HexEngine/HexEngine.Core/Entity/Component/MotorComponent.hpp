
#pragma once

#include "UpdateComponent.hpp"

namespace HexEngine
{
	class HEX_API MotorComponent : public UpdateComponent
	{
	public:
		CREATE_COMPONENT_ID(MotorComponent);

		MotorComponent(Entity* entity) :
			UpdateComponent(entity)
		{}

		MotorComponent(Entity* entity, MotorComponent* copy) :
			UpdateComponent(entity),
			_speed(copy ? copy->_speed : 1.0f),
			_direction(copy ? copy->_direction : math::Vector3(0.0f, 0.0f, 0.0f))
		{}

		virtual void Update(float frameTime) override;

		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;

		void SetSpeed(float speed);
		void SetDirection(const math::Vector3& direction);

	private:
		float _speed = 1.0f;
		math::Vector3 _direction;
	};
}
