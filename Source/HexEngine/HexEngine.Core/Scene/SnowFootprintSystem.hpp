#pragma once
#include "../Required.hpp"
#include <array>
#include <vector>

namespace HexEngine
{
	// One footprint stamp authored into the snow (CPU-side ring record).
	// World-anchored: nothing here follows the camera, so the deformation map
	// can recentre for free (it is re-stamped from these each frame).
	struct SnowFootprint
	{
		math::Vector2 worldXZ;    // ground position (x,z)
		math::Vector2 dirXZ;      // facing (unit) - orients the foot shape
		float side = 0.0f;        // 0 = left foot, 1 = right (mirrors the SDF)
		float halfLen = 0.13f;    // foot half length (m)
		float halfWidth = 0.06f;  // foot half width (m)
		float lifetime = 20.0f;   // seconds until snowfall fully refills it
		float birthTime = 0.0f;   // scene time when stamped
	};

	// GPU instance layout - MUST match SnowFootstamp.shader's FootprintGpu
	// struct byte-for-byte (32 bytes).
	struct SnowFootprintGpu
	{
		float worldX, worldZ;
		float dirX, dirZ;
		float halfLen;
		float halfWidth;
		float side;
		float fade;   // 1 = fresh, 0 = fully refilled
	};

	// Fixed-capacity ring of recent footprints. Owned by Scene; walking
	// entities Emit() into it and the renderer CollectActive()s the live ones
	// each frame to stamp the deformation map. Oldest prints are overwritten
	// once the ring is full.
	class SnowFootprintSystem
	{
	public:
		static constexpr uint32_t kCapacity = 256u;

		void Emit(const math::Vector2& worldXZ, const math::Vector2& dirXZ,
			float side, float halfLen, float halfWidth, float lifetime, float now);

		// Append the still-alive prints to `out` (fade computed from each
		// print's own age/lifetime); expired entries are skipped.
		void CollectActive(std::vector<SnowFootprintGpu>& out, float now) const;

		void Clear();
		uint32_t GetCount() const { return _count; }

	private:
		std::array<SnowFootprint, kCapacity> _prints{};
		uint32_t _head = 0;   // next write slot
		uint32_t _count = 0;  // number of valid entries (<= kCapacity)
	};
}
