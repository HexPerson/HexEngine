
#include "Point.hpp"
#include "../../Environment/IEnvironment.hpp"

namespace HexEngine
{
	Point::Point() :
		x(0),
		y(0)
	{}

	Point::Point(int32_t x, int32_t y) :
		x(x),
		y(y)
	{}

	Point::Point(const Point& other) :
		x(other.x),
		y(other.y)
	{}

	Point& Point::operator =(const Point& other)
	{
		x = other.x;
		y = other.y;

		return *this;
	}

	Point Point::operator +(const Point& other) const
	{
		Point p = *this;

		p.x += other.x;
		p.y += other.y;

		return p;;
	}

	Point& Point::operator += (const Point& other)
	{
		x += other.x;
		y += other.y;

		return *this;
	}

	Point Point::GetCenter(const Point& size) const
	{
		Point p(*this);

		p.x += size.x / 2;
		p.y += size.y / 2;

		return p;
	}

	Point Point::RelativeTo(const Point& other) const
	{
		Point p = *this;

		p.x -= other.x;
		p.y -= other.y;

		return p;
	}

	Point Point::GetScreenCenter()
	{
		static Point p;
		static bool valid = false;

		if (!valid)
		{
			uint32_t width, height;
			g_pEnv->GetScreenSize(width, height);

			p.x = width >> 1;
			p.y = height >> 1;
		}

		return p;
	}

	Point Point::GetScreenCenterWithOffset(int32_t offsetx, int32_t offsety)
	{
		Point p = GetScreenCenter();

		p.x += offsetx;
		p.y += offsety;

		return p;
	}
}