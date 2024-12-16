
#include "MotorComponent.hpp"
#include "Transform.hpp"
#include "../Entity.hpp"

namespace HexEngine
{
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