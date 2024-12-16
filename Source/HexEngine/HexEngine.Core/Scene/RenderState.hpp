

#pragma once

#include "../Required.hpp"

namespace Required
{
	enum FaceCulling
	{
		None,
		Back,
		Front
	};

	struct RenderState
	{
		bool depthTest = true;
		FaceCulling backFaceCulling = FaceCulling::Back;


		bool operator == (const RenderState& rhs)
		{
			if (depthTest == rhs.depthTest && backFaceCulling == rhs.backFaceCulling)
				return true;

			return false;
		}
	};
}
