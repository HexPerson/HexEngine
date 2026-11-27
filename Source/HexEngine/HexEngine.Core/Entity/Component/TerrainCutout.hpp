#pragma once

#include "BaseComponent.hpp"
#include <cmath>

namespace HexEngine
{
	// World-space form of one TerrainCutout: a box that stands upright (yaw only).
	// A point p is inside when |dot(p - centre, axisX)| < halfExtents.x,
	// |p.y - centre.y| < halfExtents.y and |dot(p - centre, axisZ)| < halfExtents.z,
	// where axisZ = (-axisX.z, 0, axisX.x).
	struct TerrainCutoutBox
	{
		math::Vector3 centre;
		math::Vector3 halfExtents;
		math::Vector2 axisX = math::Vector2(1.0f, 0.0f);	// (x, z), unit length

		bool Contains(const math::Vector3& p, float grow = 0.0f) const
		{
			const float dx = p.x - centre.x, dz = p.z - centre.z;
			const float lx = dx * axisX.x + dz * axisX.y;
			const float lz = -dx * axisX.y + dz * axisX.x;
			return std::fabs(lx) < halfExtents.x + grow && std::fabs(p.y - centre.y) < halfExtents.y + grow &&
				std::fabs(lz) < halfExtents.z + grow;
		}
	};

	// Shader-side limit (Global.shader g_terrainCutouts). Extra cutouts are ignored.
	constexpr int32_t kMaxTerrainCutouts = 16;

	// A hole in the ground: an upright box (the entity's position, yaw and scale,
	// plus _centerOffset / _halfExtents) inside which the terrain surface and the
	// sea-level water plane are not drawn and the terrain has no collision. The
	// walls and floor of the hole are whatever geometry you build there - a
	// stairwell, a cutting for a railway, a basement.
	//
	// Works at any size: the terrain's voxels (metres across) never limit the
	// opening, because nothing is carved from the voxels - the surface is clipped
	// per pixel, and the collision triangles are cut exactly at the box walls.
	class HEX_API TerrainCutout : public BaseComponent
	{
	public:
		CREATE_COMPONENT_ID(TerrainCutout);

		TerrainCutout(Entity* entity);
		TerrainCutout(Entity* entity, TerrainCutout* copy);

		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;
		virtual bool CreateWidget(class ComponentWidget* widget) override;
		virtual void OnRenderEditorGizmo(bool isSelected, bool& isHovering) override;

		bool GetWorldBox(TerrainCutoutBox& out) const;

		const math::Vector3& GetHalfExtents() const { return _halfExtents; }
		void SetHalfExtents(const math::Vector3& v) { _halfExtents = v; }

	private:
		math::Vector3 _centerOffset = math::Vector3(0.0f, 0.0f, 0.0f);
		math::Vector3 _halfExtents = math::Vector3(2.0f, 3.0f, 2.0f);
		bool _enabled = true;
	};

	// Bumped whenever any cutout is added, removed, moved or edited, so systems
	// that bake against cutouts (terrain collision) know to re-check.
	HEX_API uint64_t GetTerrainCutoutRevision();
	HEX_API void BumpTerrainCutoutRevision();
}
