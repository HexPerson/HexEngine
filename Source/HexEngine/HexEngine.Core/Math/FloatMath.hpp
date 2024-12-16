

#pragma once

#include "../Required.hpp"

namespace HexEngine
{
#define B2F(x) ((float)x / 255.0f)

	float RoundUpToNearest(float val, float round);

	float RoundToNearest(float val, float round);

	float RoundDownToNearest(float val, float round);

	float GetRandomFloat(float min, float max);

	int32_t GetRandomInt(int32_t min, int32_t max);

	int32_t GetRandomInt();

	math::Vector3 GetRandomVector(float min, float max);
}
