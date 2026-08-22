#pragma once

#include <HexEngine.Core/HexEngine.hpp>
#include <algorithm>
#include <vector>

// Shared polyline corner smoothing for traffic lanes. Interior corners are
// replaced with quadratic-bezier fillets so vehicles (and route previews)
// sweep round corners instead of point-turning at each lane node.
namespace LanePathSmoothing
{
	// Appends bezier samples from `from` -> `to` with `control` as the mid
	// control point. Skips the start point (assumed already emitted).
	inline void AppendBezierSamples(const math::Vector3& from, const math::Vector3& control, const math::Vector3& to, int32_t sampleCount, std::vector<math::Vector3>& out)
	{
		for (int32_t s = 1; s <= sampleCount; ++s)
		{
			const float t = float(s) / float(sampleCount);
			const float u = 1.0f - t;
			out.push_back(from * (u * u) + control * (2.0f * u * t) + to * (t * t));
		}
	}

	// Smooths `in` into `out`. `cornerRadii[i]` is the fillet radius used at
	// interior node i (first/last nodes are kept as-is). A radius <= 0 keeps
	// the hard corner.
	inline void SmoothPolyline(const std::vector<math::Vector3>& in, const std::vector<float>& cornerRadii, std::vector<math::Vector3>& out)
	{
		out.clear();
		if (in.size() < 3)
		{
			out = in;
			return;
		}

		out.reserve(in.size() * 4);
		out.push_back(in.front());

		for (size_t i = 1; i + 1 < in.size(); ++i)
		{
			const math::Vector3& prev = in[i - 1];
			const math::Vector3& node = in[i];
			const math::Vector3& next = in[i + 1];

			math::Vector3 dirIn = node - prev;
			math::Vector3 dirOut = next - node;
			const float lenIn = dirIn.Length();
			const float lenOut = dirOut.Length();
			const float radius = i < cornerRadii.size() ? cornerRadii[i] : 0.0f;
			// 0.45 caps adjacent fillets so two neighbouring corners can never
			// trim past each other on a shared segment.
			const float trim = std::min(radius, std::min(lenIn, lenOut) * 0.45f);
			if (lenIn <= 0.001f || lenOut <= 0.001f || trim <= 0.05f)
			{
				out.push_back(node);
				continue;
			}

			dirIn *= (1.0f / lenIn);
			dirOut *= (1.0f / lenOut);
			const float cosTurn = dirIn.Dot(dirOut);
			if (cosTurn > 0.99f) // < ~8 degrees - not worth a fillet
			{
				out.push_back(node);
				continue;
			}

			const math::Vector3 entry = node - dirIn * trim;
			const math::Vector3 exit = node + dirOut * trim;
			// More samples for sharper turns: ~4 for a gentle bend, ~11 for a U-turn.
			const float turnAlpha = std::clamp((1.0f - cosTurn) * 0.5f, 0.0f, 1.0f);
			const int32_t samples = 4 + static_cast<int32_t>(turnAlpha * 7.0f);
			out.push_back(entry);
			AppendBezierSamples(entry, node, exit, samples, out);
		}

		out.push_back(in.back());
	}

	inline void SmoothPolyline(const std::vector<math::Vector3>& in, float cornerRadius, std::vector<math::Vector3>& out)
	{
		const std::vector<float> radii(in.size(), cornerRadius);
		SmoothPolyline(in, radii, out);
	}
}
