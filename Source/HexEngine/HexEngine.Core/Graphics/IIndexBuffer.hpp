
#pragma once

#include "INativeGraphicsResource.hpp"

namespace HexEngine
{
	class IIndexBuffer : public INativeGraphicsResource
	{
	public:
		virtual ~IIndexBuffer() {};

		virtual void SetIndexData(uint8_t* data, uint32_t size, uint32_t offset = 0) = 0;
	};
}
