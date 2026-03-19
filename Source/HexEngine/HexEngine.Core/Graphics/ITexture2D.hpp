
#pragma once

#include "ITexture.hpp"
#include "../FileSystem/IResource.hpp"

namespace HexEngine
{
	class IShader;

	/** @brief 2D texture abstraction used for render targets and sampled resources. */
	class HEX_API ITexture2D : public ITexture, public IResource
	{
	public:
		// Convenience static methods

		/**
		 * @brief Loads a 2D texture resource from disk.
		 * @param absolutePath Absolute texture path.
		 * @return Shared texture resource, or `nullptr` on failure.
		 */
		static std::shared_ptr<ITexture2D> Create(const fs::path& absolutePath);

		/** @brief Returns the engine default fallback texture. */
		static std::shared_ptr<ITexture2D> GetDefaultTexture();

		virtual ~ITexture2D() {}

		/** @brief Returns texture height in pixels. */
		virtual int32_t GetHeight() = 0;

		/** @brief Uploads raw pixel data to this texture. */
		virtual void SetPixels(uint8_t* data, uint32_t size) = 0;

		/** @brief Returns the underlying DXGI texture format. */
		virtual uint32_t GetFormat() = 0;

		/** @brief Saves texture data to a file on disk. */
		virtual void SaveToFile(const fs::path& path) = 0;

		/** @brief Clears depth/stencil content when bound as a depth texture. */
		virtual void ClearDepth(uint32_t flags) = 0;

		/** @brief Clears the render-target view with a solid color. */
		virtual void ClearRenderTargetView(const math::Color& colour) = 0;

		/** @brief Copies the full texture to another texture. */
		virtual void CopyTo(ITexture2D* other) = 0;

		/** @brief Copies a rectangle region from this texture to another texture. */
		virtual void CopyTo(ITexture2D* other, const RECT& srcRect, const RECT& dstRect) = 0;

		/** @brief Blends this texture additively into `other`. */
		virtual void BlendTo_Additive(ITexture2D* other, IShader* optionalShader = nullptr) = 0;

		/** @brief Performs the two-pass additive blend path used by selected effects. */
		virtual void BlendTo_Additive_Double(ITexture2D* other, IShader* optionalShader = nullptr) = 0;

		/** @brief Alpha-blends this texture into `other`. */
		virtual void BlendTo_Alpha(ITexture2D* other, IShader* optionalShader = nullptr) = 0;

		/** @brief Blends this texture into `other` using non-premultiplied alpha. */
		virtual void BlendTo_NonPremultiplied(ITexture2D* other, IShader* optionalShader = nullptr) = 0;

		virtual void SetDebugName(const std::string& name) override
		{
#ifdef _DEBUG
			((ID3D11Texture2D*)GetNativePtr())->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)name.length(), name.data());
#endif
		}		
	};
}
