
#pragma once

#include "../../Required.hpp"

namespace HexEngine
{
	class HEX_API Point
	{
	public:
		Point();
		Point(int32_t x, int32_t y);
		Point(const Point& other);

		Point& operator =(const Point& other);
		Point& operator += (const Point& other);
		Point operator +(const Point& other) const;

		Point GetCenter(const Point& size) const;
		Point RelativeTo(const Point& other) const;

		static Point GetScreenCenter();
		static Point GetScreenCenterWithOffset(int32_t offsetx, int32_t offsety);

	public:
		int32_t x;
		int32_t y;
	};
}
