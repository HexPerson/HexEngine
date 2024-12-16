

#pragma once

#include "../Required.hpp"

namespace HexEngine
{
	std::vector<math::Vector3> computeBesierCurve3D(const std::vector<float>& xX, const std::vector<float>& yY, const std::vector<float>& zZ, float dt);

	math::Vector3 CalculateBezierPoint(float t, math::Vector3 p0, math::Vector3 p1, math::Vector3 p2, math::Vector3 p3);
}
