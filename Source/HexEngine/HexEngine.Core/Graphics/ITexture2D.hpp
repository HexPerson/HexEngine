
#pragma once

#include "ITexture.hpp"
#include "../FileSystem/IResource.hpp"

namespace HexEngine
{
	class IShader;

	class ITexture2D : public ITexture, public IResource
	{
	public:
		// Convenience static methods

		/// <summary>
		/// Create a 2D texture from disk using an absolute path
		/// </summary>
		/// <param name="absolutePath">The absolute path to the file on disk</param>
		/// <returns>A pointer to a ITexture2D instance or null if the resource wasn't loaded successfully</returns>
		static std::shared_ptr<ITexture2D> Create(const fs::path& absolutePath);

		static std::shared_ptr<ITexture2D> GetDefaultTexture();

		virtual ~ITexture2D() {}

		/// <summary>
		/// Get the height of the texture in pixels
		/// </summary>
		/// <returns>The height in pixels</returns>
		virtual int32_t GetHeight() = 0;

		virtual void SetPixels(uint8_t* data, uint32_t size) = 0;

		virtual uint32_t GetFormat() = 0;

		virtual void SaveToFile(const fs::path& path) = 0;

		virtual void ClearDepth(uint32_t flags) = 0;

		virtual void ClearRenderTargetView(const math::Color& colour) = 0;

		virtual void CopyTo(ITexture2D* other) = 0;

		virtual void CopyTo(ITexture2D* other, const RECT& srcRect, const RECT& dstRect) = 0;

		virtual void BlendTo_Additive(ITexture2D* other, IShader* optionalShader = nullptr) = 0;

		virtual void BlendTo_Additive_Double(ITexture2D* other, IShader* optionalShader = nullptr) = 0;

		virtual void BlendTo_Alpha(ITexture2D* other, IShader* optionalShader = nullptr) = 0;

		virtual void BlendTo_NonPremultiplied(ITexture2D* other, IShader* optionalShader = nullptr) = 0;

		virtual void SetDebugName(const std::string& name) override
		{
#ifdef _DEBUG
			((ID3D11Texture2D*)GetNativePtr())->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)name.length(), name.data());
#endif
		}		
	};
}
