

#pragma once

#include "Collider.hpp"

namespace HexEngine
{
	class SphereCollider : public Collider
	{
	public:		
		SphereCollider(Entity* entity, float radius);

		CREATE_COMPONENT_ID(SphereCollider);

		void SetRadius(float radius);

	private:
		float _radius = 1.0f;
	};
}
