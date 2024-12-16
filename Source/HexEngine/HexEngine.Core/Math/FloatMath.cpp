

#include "FloatMath.hpp"
#include <random>

namespace HexEngine
{
	//std::default_random_engine gRng;
	std::random_device dev;
	std::mt19937 rng(dev());

	float RoundUpToNearest(float val, float round)
	{
		return std::ceil(val / round) * round;
	};

	float RoundToNearest(float val, float round)
	{
		return std::round(val / round) * round;
	};

	float RoundDownToNearest(float val, float round)
	{
		return std::floor(val / round) * round;
	};

	float GetRandomFloat(float min, float max)
	{		
		std::uniform_real_distribution<> dis(min, max); // rage 0 - 1
		return dis(dev);
	}

	int32_t GetRandomInt(int32_t min, int32_t max)
	{
		std::uniform_int_distribution<std::mt19937::result_type> dis(min, max); // rage 0 - 1
		return dis(dev);
	}

	int32_t GetRandomInt()
	{
		return GetRandomInt(0, INT_MAX);
	}

	math::Vector3 GetRandomVector(float min, float max)
	{
		return math::Vector3(
			GetRandomFloat(min, max),
			GetRandomFloat(min, max),
			GetRandomFloat(min, max));
	}
}