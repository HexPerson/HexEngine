
#pragma once

#include "Gadget.hpp"

namespace HexEditor
{
	class ScaleGadget : public Gadget
	{
	public:
		ScaleGadget();

		virtual bool StartGadget() override;
		virtual void StopGadget(GadgetAction action) override;
		virtual void Update() override;

	private:
		math::Vector3 _originalScale;
		float _adjustSize = 0.0f;
		int32_t _adjustStartX;
		int32_t _adjustStartY;
	};
}
