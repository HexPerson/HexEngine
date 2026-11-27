
#include "MotorComponent.hpp"
#include "Transform.hpp"
#include "../Entity.hpp"

namespace HexEngine
{
	void MotorComponent::Serialize(json& data, JsonFile* file)
	{
		SERIALIZE_VALUE(_speed);
		SERIALIZE_VALUE(_direction);
	}

	void MotorComponent::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		DESERIALIZE_VALUE(_speed);
		DESERIALIZE_VALUE(_direction);
	}

	void MotorComponent::SetSpeed(float speed)
	{
		_speed = speed;
	}

	void MotorComponent::SetDirection(const math::Vector3& direction)
	{
		_direction = direction;
	}

	void MotorComponent::Update(float frameTime)
	{
		UpdateComponent::Update(frameTime);

		Transform* transform = GetEntity()->GetComponent<Transform>();

		if (!transform)
			return;

		if (_speed == 0.0f || _direction.Length() == 0.0f)
			return;

		auto position = transform->GetPosition();

		position += _direction * _speed * frameTime;

		transform->SetPosition(position);
	}
}