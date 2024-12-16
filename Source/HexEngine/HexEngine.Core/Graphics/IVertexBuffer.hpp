

#pragma once

#include "INativeGraphicsResource.hpp"

namespace HexEngine
{
	class IVertexBuffer : public INativeGraphicsResource
	{
	public:
		virtual ~IVertexBuffer() {};

		virtual void SetVertexData(uint8_t* data, uint32_t size, uint32_t offset = 0) = 0;

		virtual uint32_t GetStride() = 0;
	};
}
