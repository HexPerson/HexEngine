

#include "Bezier.hpp"

namespace HexEngine
{
	std::vector<math::Vector3> computeBesierCurve3D(const std::vector<float>& xX, const std::vector<float>& yY, const std::vector<float>& zZ, float dt)
	{

		//std::vector<float> bCurveX;
		//std::vector<float> bCurveY;
		float bCurveXt;
		float bCurveYt;
		float bCurveZt;

		std::vector<math::Vector3> curve;

		for (float t = dt; t <= 1; t += dt)
		{

			bCurveXt = std::pow((1 - t), 3) * xX[0] + 3 * std::pow((1 - t), 2) * t * xX[1] + 3 * std::pow((1 - t), 1) * std::pow(t, 2) * xX[2];// +std::pow(t, 3) * xX[3];
			bCurveYt = std::pow((1 - t), 3) * yY[0] + 3 * std::pow((1 - t), 2) * t * yY[1] + 3 * std::pow((1 - t), 1) * std::pow(t, 2) * yY[2];// +std::pow(t, 3) * yY[3];
			bCurveZt = std::pow((1 - t), 3) * zZ[0] + 3 * std::pow((1 - t), 2) * t * zZ[1] + 3 * std::pow((1 - t), 1) * std::pow(t, 2) * zZ[2];// +std::pow(t, 3) * zZ[3];

			curve.push_back(math::Vector3(bCurveXt, bCurveYt, bCurveZt));

			//bCurveX.push_back(bCurveXt);
			//bCurveY.push_back(bCurveYt);
		}

		return curve;

		//return std::make_tuple(bCurveX, bCurveY);
	}

	math::Vector3 CalculateBezierPoint(float t, math::Vector3 p0, math::Vector3 p1, math::Vector3 p2, math::Vector3 p3)
	{
		float u = 1.0f - t;
		float tt = t * t;
		float uu = u * u;
		float uuu = uu * u;
		float ttt = tt * t;

		math::Vector3 p = uuu * p0; //first term
		p += 3 * uu * t * p1; //second term
		p += 3 * u * tt * p2; //third term
		p += ttt * p3; //fourth term

		return p;
	}
}