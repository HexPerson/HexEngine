

#pragma once

#include "ITexture.hpp"
#include "../FileSystem/IResource.hpp"

namespace HexEngine
{
	class ITexture3D : public ITexture, public IResource
	{
	public:
		virtual ~ITexture3D() {}

		virtual int32_t GetHeight() = 0;

		virtual int32_t GetDepth() = 0;

		virtual void SetPixels(uint8_t* data, uint32_t size, int32_t slice = 0) = 0;

		virtual uint32_t GetFormat() = 0;

		virtual void SaveToFile(const fs::path& path) = 0;

		virtual void ClearDepth(uint32_t flags) = 0;

		virtual void ClearRenderTargetView(const math::Color& colour) = 0;

		virtual void CopyTo(ITexture3D* other) = 0;

		virtual void SetDebugName(const std::string& name) override
		{
#ifdef _DEBUG
			((ID3D11Texture2D*)GetNativePtr())->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)name.length(), name.data());
#endif
		}
	};
}
