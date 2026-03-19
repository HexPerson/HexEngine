

#pragma once

#include "INativeGraphicsResource.hpp"

namespace HexEngine
{
	/** @brief Shared texture interface for 2D/3D texture implementations. */
	class HEX_API ITexture : public INativeGraphicsResource
	{
	public:
        virtual ~ITexture() {}

		/** @brief Returns texture width in pixels. */
		virtual int32_t GetWidth() = 0;
		/** @brief Reads texture contents into an 8-bit buffer. */
		virtual void	GetPixels(std::vector<uint8_t>& buffer) = 0;
		/** @brief Reads texture contents into a floating-point buffer. */
		virtual void	GetPixels(std::vector<float>& buffer) = 0;
		/** @brief Returns an OS-level shared handle when supported by the backend. */
		virtual void*	GetSharedHandle() = 0;
		/** @brief Maps texture memory for CPU access and returns the mapped pointer. */
		virtual void*	LockPixels(int32_t* rowPitch = nullptr) = 0;
		/** @brief Unmaps previously locked texture memory. */
		virtual void	UnlockPixels() = 0;
	};
}
