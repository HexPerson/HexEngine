
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
		math::Vector3 _originalPosition;
		math::Quaternion _cameraRotation;
		int32_t _adjustStartX;
		int32_t _adjustStartY;
	};
}
