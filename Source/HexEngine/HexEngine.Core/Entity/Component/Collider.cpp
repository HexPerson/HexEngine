

#include "Collider.hpp"
#include "../../Environment/IEnvironment.hpp"

namespace HexEngine
{
	Collider::Collider(Entity* entity) :
		BaseComponent(entity)
	{		
	}

	void Collider::Destroy()
	{
		//_rigidBody
	}

	
}