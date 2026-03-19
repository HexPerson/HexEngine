

#pragma once

#include "INativeGraphicsResource.hpp"

namespace HexEngine
{
	/** @brief GPU vertex buffer interface. */
	class IVertexBuffer : public INativeGraphicsResource
	{
	public:
		virtual ~IVertexBuffer() {};

		/**
		 * @brief Updates vertex data in this buffer.
		 * @param data Source vertex bytes.
		 * @param size Number of bytes to upload.
		 * @param offset Byte offset from the start of the buffer.
		 */
		virtual void SetVertexData(uint8_t* data, uint32_t size, uint32_t offset = 0) = 0;

		/** @brief Returns the stride (bytes per vertex). */
		virtual uint32_t GetStride() = 0;
	};
}
