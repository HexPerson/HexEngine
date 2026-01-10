
#pragma once

#include "Gadget.hpp"

namespace HexEditor
{
	class PositionGadget : public Gadget
	{
	public:
		PositionGadget();

		virtual bool StartGadget() override;
		virtual void StopGadget(GadgetAction action) override;
		virtual void Update() override;
		virtual bool OnInputEvent(InputEvent event, InputData* data) override;

	private:
		math::Vector3 _originalPosition;
		math::Vector3 _movementFreedom;
		math::Quaternion _cameraRotation;
		int32_t _adjustStartX;
		int32_t _adjustStartY;
		bool _useTrace = false;
	};
}
