

#pragma once

#include "../Required.hpp"

namespace HexEngine
{
	class Mesh;

	class RenderList
	{
	public:
		virtual void SubmitMesh(Mesh* meshToRender) = 0;

		virtual void RenderAll() = 0;
	};
}
