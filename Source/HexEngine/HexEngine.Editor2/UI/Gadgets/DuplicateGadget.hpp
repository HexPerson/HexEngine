
#pragma once

#include "Gadget.hpp"

namespace HexEditor
{
	class DuplicateGadget : public Gadget
	{
	public:
		DuplicateGadget();

		virtual bool StartGadget() override;
		virtual void StopGadget(GadgetAction action) override;
		virtual void Update() override;

	private:
		HexEngine::Entity* _sourceEntity = nullptr;
		HexEngine::Entity* _duplicatedEntity = nullptr;
		// World-space position captured at gadget start. The drag delta is
		// computed in world space (camera-relative right/up vectors), so we
		// must add it to a world-space anchor and convert the result back to
		// local before writing via ForcePosition - otherwise a duplicate of
		// a child entity drifts in the parent's local axes instead of the
		// camera plane the user is actually dragging in.
		math::Vector3 _originalWorldPosition;
		math::Quaternion _cameraRotation;
		int32_t _adjustStartX;
		int32_t _adjustStartY;
	};
}
