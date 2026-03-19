

#pragma once

#include "ITexture.hpp"
#include "../FileSystem/IResource.hpp"

namespace HexEngine
{
	/** @brief 3D texture abstraction used for volume textures and 3D render targets. */
	class ITexture3D : public ITexture, public IResource
	{
	public:
		virtual ~ITexture3D() {}

		/** @brief Returns texture height in pixels. */
		virtual int32_t GetHeight() = 0;

		/** @brief Returns texture depth (slice count). */
		virtual int32_t GetDepth() = 0;

		/** @brief Uploads pixel data to a 3D texture slice or full texture. */
		virtual void SetPixels(uint8_t* data, uint32_t size, int32_t slice = 0) = 0;

		/** @brief Returns the underlying DXGI texture format. */
		virtual uint32_t GetFormat() = 0;

		/** @brief Saves texture data to a file on disk. */
		virtual void SaveToFile(const fs::path& path) = 0;

		/** @brief Clears depth/stencil content when bound as a depth texture. */
		virtual void ClearDepth(uint32_t flags) = 0;

		/** @brief Clears the render-target view with a solid color. */
		virtual void ClearRenderTargetView(const math::Color& colour) = 0;

		/** @brief Copies the full texture to another 3D texture. */
		virtual void CopyTo(ITexture3D* other) = 0;

		virtual void SetDebugName(const std::string& name) override
		{
#ifdef _DEBUG
			((ID3D11Texture2D*)GetNativePtr())->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)name.length(), name.data());
#endif
		}
	};
}
