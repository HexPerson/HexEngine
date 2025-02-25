

#pragma once

#include "../Required.hpp"

namespace HexEngine
{
#define B2F(x) ((float)x / 255.0f)

	float HEX_API RoundUpToNearest(float val, float round);

	float HEX_API RoundToNearest(float val, float round);

	float HEX_API RoundDownToNearest(float val, float round);

	float HEX_API GetRandomFloat(float min, float max);

	int32_t HEX_API GetRandomInt(int32_t min, int32_t max);

	int32_t HEX_API GetRandomInt();

	math::Vector3 HEX_API GetRandomVector(float min, float max);
}
