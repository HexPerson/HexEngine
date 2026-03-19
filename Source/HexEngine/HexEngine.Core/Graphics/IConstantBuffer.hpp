

#pragma once

#include "INativeGraphicsResource.hpp"

namespace HexEngine
{
	/** @brief GPU constant/uniform buffer interface. */
	class IConstantBuffer : public INativeGraphicsResource
	{
	public:
		/**
		 * @brief Uploads CPU memory to this constant buffer.
		 * @param data Source bytes.
		 * @param size Number of bytes to write.
		 * @return True if write succeeded.
		 */
		virtual bool Write(void* data, uint32_t size) = 0;
	};
}
