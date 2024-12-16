

#pragma once

#include "BaseComponent.hpp"
#include "RigidBody.hpp"

namespace HexEngine
{
	class Collider : public BaseComponent
	{
	public:
		Collider(Entity* entity);

		virtual void Destroy() override;

		//virtual void Update(float frameTime) override;

	private:
		RigidBody* _rigidBody = nullptr;
	};
}
