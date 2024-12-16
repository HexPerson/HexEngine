

#pragma once

#include "../../HexEngine.Core/Required.hpp"
#include "../../HexEngine.Core/Entity/DirectionalLight.hpp"

namespace CityBuilder
{
	class DayNightCycle
	{
	public:
		void Update(float frameTime, HexEngine::DirectionalLight* sunLight);

	private:
		//float _timeOfDay = 0.0f; // 0 = mid day
		float _lightMultiplier = 1.0f;
		HexEngine::DirectionalLight* _sunLight = nullptr;
		math::Color _ambientColour;
		
	};
}
