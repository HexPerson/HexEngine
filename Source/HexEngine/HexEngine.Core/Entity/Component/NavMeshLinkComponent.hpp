

#pragma once

#include "BaseComponent.hpp"
#include "../../Scene/INavMeshProvider.hpp"

namespace HexEngine
{
	class ComponentWidget;

	// An off-mesh connection (Detour off-mesh link) for the navmesh. It lets agents
	// traverse from the start point to the end point - and back, when bidirectional -
	// even where there is no walkable polygon between them. Use it to stitch two
	// otherwise-separate navmesh regions across a gap (e.g. a lawn that's excluded
	// from the bake), or for jumps / planks / ladders.
	//
	// The two endpoints are LOCAL offsets from the entity, transformed by its world TM
	// (so it follows position/rotation/scale and rides on prefabs). Both endpoints must
	// land on or within `radius` of existing navmesh for Detour to connect them.
	// Gathered at bake time (Scene::GatherNavMeshLinks) and handed to Detour.
	class HEX_API NavMeshLinkComponent : public BaseComponent
	{
	public:
		CREATE_COMPONENT_ID(NavMeshLinkComponent);
		DEFINE_COMPONENT_CTOR(NavMeshLinkComponent);

		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;
		virtual bool CreateWidget(class ComponentWidget* widget) override;
		virtual void OnRenderEditorGizmo(bool isSelected, bool& isHovering) override;

		// Fills `out` with this link's world-space endpoints + params for the bake.
		void GetWorldLink(NavMeshLink& out) const;

		const math::Vector3& GetStartOffset() const { return _startOffset; }
		void SetStartOffset(const math::Vector3& v) { _startOffset = v; }
		const math::Vector3& GetEndOffset() const { return _endOffset; }
		void SetEndOffset(const math::Vector3& v) { _endOffset = v; }
		float GetRadius() const { return _radius; }
		void SetRadius(float r) { _radius = r; }
		bool IsBidirectional() const { return _bidirectional; }
		void SetBidirectional(bool b) { _bidirectional = b; }

	private:
		math::Vector3 _startOffset   = math::Vector3(0.0f, 0.0f, 0.0f);
		math::Vector3 _endOffset     = math::Vector3(0.0f, 0.0f, 3.0f);
		float         _radius        = 0.6f;
		bool          _bidirectional = true;
	};
}
