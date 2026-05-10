#pragma once

#include <HexEngine.Core/HexEngine.hpp>

namespace HexEngine::VolumetricTerrain
{
	struct MarchingCubesOutput
	{
		std::vector<MeshVertex> vertices;
		std::vector<MeshIndexFormat> indices;
		dx::BoundingBox aabb{};
		dx::BoundingOrientedBox obb{};
	};

	class MarchingCubes
	{
	public:
		// Density convention: density < 0 is solid, density >= 0 is empty.
		MarchingCubesOutput Build(
			const std::vector<float>& densities,
			int32_t resolution,
			float voxelSize,
			const math::Vector3& chunkOrigin,
			float uvScale) const;

	private:
		int32_t Index(int32_t x, int32_t y, int32_t z, int32_t pointsPerAxis) const;
		math::Vector3 Interpolate(const math::Vector3& a, const math::Vector3& b, float da, float db) const;
		void EmitTriangle(
			MarchingCubesOutput& output,
			const math::Vector3& a,
			const math::Vector3& b,
			const math::Vector3& c,
			float uvScale) const;
	};
}
