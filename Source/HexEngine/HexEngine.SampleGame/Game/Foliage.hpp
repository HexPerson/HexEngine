
#pragma once

#include "../../HexEngine.Core/Entity/Entity.hpp"

namespace CityBuilder
{
	// {8989110F-3078-413C-AEDD-B07641DF1527}
	DEFINE_HEX_GUID(FoliageGUID,
		0x8989110f, 0x3078, 0x413c, 0xae, 0xdd, 0xb0, 0x76, 0x41, 0xdf, 0x15, 0x27);

	class Foliage : public HexEngine::Entity
	{
	public:
		DEFINE_OBJECT_GUID(Foliage);

		virtual void Update(float frameTime) override;

	public:
		
	};
}