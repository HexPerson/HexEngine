
#pragma once

#include "../UIManager.hpp"

namespace HexEngine
{
	class HEX_API IMGUI
	{
	public:
		static void FloatControl(int32_t x, int32_t y, int32_t w, int32_t h, Style* style, float* value);
		static void VectorControl(int32_t x, int32_t y, int32_t w, int32_t h, Style* style, math::Vector3* value);
	};
}
