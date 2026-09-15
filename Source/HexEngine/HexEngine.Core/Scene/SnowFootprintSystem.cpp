#include "SnowFootprintSystem.hpp"

namespace HexEngine
{
	void SnowFootprintSystem::Emit(const math::Vector2& worldXZ, const math::Vector2& dirXZ,
		float side, float halfLen, float halfWidth, float lifetime, float now)
	{
		SnowFootprint& p = _prints[_head];
		p.worldXZ = worldXZ;
		p.dirXZ = dirXZ;
		p.side = side;
		p.halfLen = halfLen;
		p.halfWidth = halfWidth;
		p.lifetime = lifetime;
		p.birthTime = now;

		_head = (_head + 1u) % kCapacity;
		if (_count < kCapacity)
			++_count;
	}

	void SnowFootprintSystem::CollectActive(std::vector<SnowFootprintGpu>& out, float now) const
	{
		out.clear();
		for (uint32_t i = 0; i < _count; ++i)
		{
			const SnowFootprint& p = _prints[i];
			if (p.lifetime <= 0.0f)
				continue;
			const float age = now - p.birthTime;
			if (age < 0.0f || age >= p.lifetime)
				continue;

			SnowFootprintGpu g;
			g.worldX = p.worldXZ.x;
			g.worldZ = p.worldXZ.y;
			g.dirX = p.dirXZ.x;
			g.dirZ = p.dirXZ.y;
			g.halfLen = p.halfLen;
			g.halfWidth = p.halfWidth;
			g.side = p.side;
			g.fade = 1.0f - (age / p.lifetime); // linear snowfall refill
			out.push_back(g);
		}
	}

	void SnowFootprintSystem::Clear()
	{
		_head = 0;
		_count = 0;
	}
}
