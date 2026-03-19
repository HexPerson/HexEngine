
#pragma once

#include "INativeGraphicsResource.hpp"

namespace HexEngine
{
	/** @brief GPU index buffer interface. */
	class IIndexBuffer : public INativeGraphicsResource
	{
	public:
		virtual ~IIndexBuffer() {};

		/**
		 * @brief Updates index data in this buffer.
		 * @param data Source index bytes.
		 * @param size Number of bytes to upload.
		 * @param offset Byte offset from the start of the buffer.
		 */
		virtual void SetIndexData(uint8_t* data, uint32_t size, uint32_t offset = 0) = 0;
	};
}
