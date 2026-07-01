

#pragma once

#include "BaseComponent.hpp"
#include "../../Scene/INavMeshProvider.hpp"

namespace HexEngine
{
	// A box volume that modifies the navmesh during the Recast bake. The box is the
	// entity's local AABB (centre offset + half-extents) transformed by the full
	// world TM, so it follows the owner's position / rotation / scale - drop it on a
	// prefab and every placed instance carries it.
	//
	//   Block         - carves the footprint out of the walkable navmesh. Use on
	//                   road-section prefabs so agents won't path across the road.
	//   ForceWalkable - restores walkability inside the footprint. Use on pedestrian
	//                   crossing prefabs so they stay walkable even where they sit on
	//                   top of a road's Block volume.
	//
	// During the bake all Block volumes are applied first, then ForceWalkable, so a
	// crossing always wins over an overlapping road block. The road-placement tool
	// just needs to place the prefabs - the volumes ride along.
	class HEX_API NavMeshBlockingVolume : public BaseComponent
	{
	public:
		CREATE_COMPONENT_ID(NavMeshBlockingVolume);

		enum class Mode : int32_t
		{
			Block         = 0,
			ForceWalkable = 1,
		};

		NavMeshBlockingVolume(Entity* entity);
		NavMeshBlockingVolume(Entity* entity, NavMeshBlockingVolume* copy);

		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;
		virtual bool CreateWidget(class ComponentWidget* widget) override;
		virtual void OnRenderEditorGizmo(bool isSelected, bool& isHovering) override;

		// Fills `out` with this volume's world-space footprint (base quad in winding
		// order + vertical range + mode) for the navmesh build.
		void GetWorldFootprint(NavMeshBlockingBox& out) const;

		Mode GetMode() const { return _mode; }
		void SetMode(Mode mode) { _mode = mode; }

		const math::Vector3& GetCenterOffset() const { return _centerOffset; }
		void SetCenterOffset(const math::Vector3& v) { _centerOffset = v; }

		const math::Vector3& GetHalfExtents() const { return _halfExtents; }
		void SetHalfExtents(const math::Vector3& v) { _halfExtents = v; }

	private:
		math::Vector3 _centerOffset = math::Vector3(0.0f, 0.0f, 0.0f);
		math::Vector3 _halfExtents  = math::Vector3(2.0f, 2.0f, 2.0f);
		Mode          _mode         = Mode::Block;

		// UI mirror for the mode checkbox. The Checkbox widget can only bind to a
		// bool* (its callback form is display-only), so this tracks _mode for the
		// editor; it's re-synced from _mode every time the widget is built.
		bool          _forceWalkableUI = false;
	};
}
