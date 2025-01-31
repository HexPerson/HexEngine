

#pragma once

#include "INativeGraphicsResource.hpp"

namespace HexEngine
{
	class ITexture : public INativeGraphicsResource
	{
	public:
        virtual ~ITexture() {}

		/// <summary>
		/// Get the width of the texture in pixels
		/// </summary>
		/// <returns>The width in pixels</returns>
		virtual int32_t GetWidth() = 0;

		virtual void GetPixels(std::vector<uint8_t>& buffer) = 0;

		virtual void GetPixels(std::vector<float>& buffer) = 0;

		virtual void* GetSharedHandle() = 0;

		virtual void* LockPixels(int32_t* rowPitch = nullptr) = 0;
		virtual void UnlockPixels() = 0;
	};
}
