

#pragma once

#include "INativeGraphicsResource.hpp"

namespace HexEngine
{
	class IConstantBuffer : public INativeGraphicsResource
	{
	public:
		virtual bool Write(void* data, uint32_t size) = 0;
	};
}
