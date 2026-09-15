#pragma once

#include "UpdateComponent.hpp"

namespace HexEngine
{
	// Opt-in authoring component (Phase 3 Part B): an entity carrying this leaves
	// discrete, alternating left/right foot-shaped prints in the snow as it walks.
	// Nothing marks snow unless this component is present. If the entity has a
	// character-controller RigidBody the prints track the capsule foot position
	// and only stamp while grounded; otherwise the entity's world position is
	// used. Prints are emitted into the scene's SnowFootprintSystem ring buffer,
	// which the renderer stamps into the snow deformation map.
	class HEX_API SnowFootprintsComponent : public UpdateComponent
	{
	public:
		CREATE_COMPONENT_ID(SnowFootprintsComponent);
		DEFINE_COMPONENT_CTOR(SnowFootprintsComponent);

		virtual void Update(float frameTime) override;

		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;
		virtual bool CreateWidget(class ComponentWidget* widget) override;

	private:
		// Serialized config.
		bool  _enabled       = true;
		float _strideLength  = 0.7f;   // metres of travel between successive prints
		float _footWidth     = 0.18f;  // lateral L<->R spacing (metres)
		float _footHalfLen   = 0.13f;  // print half length along travel (metres)
		float _footHalfWidth = 0.06f;  // print half width (metres)
		float _lifetime      = 20.0f;  // seconds until snowfall refills the print

		// Runtime state (not serialized).
		math::Vector3 _prevPos = math::Vector3::Zero;
		bool  _havePrev = false;
		float _distanceSinceStep = 0.0f;
		float _side = 0.0f; // 0 = left, 1 = right (alternates each print)
	};
}
