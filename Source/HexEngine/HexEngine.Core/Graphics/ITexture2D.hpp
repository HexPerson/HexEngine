
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

		// SetDebugName is inherited from INativeGraphicsResource and implemented
		// in each backend plugin (Texture2D::SetDebugName in HexEngine.D3D11Plugin
		// for example). The previous inline implementation here cast GetNativePtr()
		// to ID3D11Texture2D* which only worked under the D3D11 backend.

		/**
		 * @brief Reads a reduced-resolution copy of the texture as tightly
		 * packed RGBA8 (BC formats decompressed; non-BC data returned in its
		 * native 8-bit channel order, e.g. BGRA stays BGRA - check GetFormat).
		 * The backend picks the smallest existing mip whose larger dimension is
		 * still >= maxDimension (or the smallest mip available) so only that
		 * single mip is decoded - full-chain decompression of large BC
		 * textures on the CPU is far too slow for callers that only need a
		 * coarse tint/average (GI material proxies).
		 *
		 * Appended defaulted virtual (vtable end): backends without an
		 * implementation return false and callers must fall back to
		 * GetPixels.
		 */
		virtual bool GetPixelsScaled(std::vector<uint8_t>& buffer, int32_t maxDimension, int32_t& outWidth, int32_t& outHeight)
		{
			(void)buffer; (void)maxDimension; (void)outWidth; (void)outHeight;
			return false;
		}
	};
}
