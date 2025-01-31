

#include "Texture3D.hpp"
#include "GraphicsDeviceD3D11.hpp"
#include <HexEngine.Core/HexEngine.hpp>

#include <DirectXTex\DirectXTex.h>

namespace HexEngine
{
	Texture3D::~Texture3D()
	{
		Destroy();
	}

	void Texture3D::Destroy()
	{
		SAFE_RELEASE(_texture);
		SAFE_RELEASE(_renderTargetView);
		SAFE_RELEASE(_shaderResourceView);
		SAFE_RELEASE(_depthStencilView);
	}

	void* Texture3D::GetNativePtr()
	{
		return reinterpret_cast<void*>(_texture);
	}

	void Texture3D::SetPixels(uint8_t* data, uint32_t size, int32_t slice)
	{
		D3D11_MAPPED_SUBRESOURCE mapped = {};

		auto gfxDevice = (ID3D11DeviceContext*)g_pEnv->_graphicsDevice->GetNativeDeviceContext();

		if (gfxDevice->Map(_texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped) == S_OK)
		{
			memcpy(mapped.pData, data, size);

			gfxDevice->Unmap(_texture, 0);
		}
	}

	void* Texture3D::LockPixels(int32_t* rowPitch)
	{
		g_pGraphics->Lock();

		D3D11_MAPPED_SUBRESOURCE mapped = {};

		auto gfxDevice = (ID3D11DeviceContext*)g_pEnv->_graphicsDevice->GetNativeDeviceContext();

		if (gfxDevice->Map(_texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped) == S_OK)
		{
			g_pGraphics->Unlock();
			return mapped.pData;
		}

		g_pGraphics->Unlock();
		return nullptr;
	}

	void Texture3D::UnlockPixels()
	{
		g_pGraphics->Lock();

		auto gfxDevice = (ID3D11DeviceContext*)g_pEnv->_graphicsDevice->GetNativeDeviceContext();

		gfxDevice->Unmap(_texture, 0);

		g_pGraphics->Unlock();
	}

	void Texture3D::GetPixels(std::vector<uint8_t>& buffer)
	{
		auto gfxContext = (ID3D11DeviceContext*)g_pEnv->_graphicsDevice->GetNativeDeviceContext();
		auto gfxDevice = (ID3D11Device*)g_pEnv->_graphicsDevice->GetNativeDevice();

		DirectX::ScratchImage scratch;
		CHECK_HR(DirectX::CaptureTexture(
			gfxDevice,
			gfxContext,
			_texture,
			scratch));

		auto pixelsSize = scratch.GetPixelsSize();

		if (pixelsSize <= 0)
			return;

		auto pixels = scratch.GetPixels();

		if (pixels == nullptr)
			return;

		buffer.clear();
		buffer.insert(buffer.end(), pixels, pixels + pixelsSize);
	}

	void Texture3D::SaveToFile(const fs::path& path)
	{
		auto gfxContext = (ID3D11DeviceContext*)g_pEnv->_graphicsDevice->GetNativeDeviceContext();
		auto gfxDevice = (ID3D11Device*)g_pEnv->_graphicsDevice->GetNativeDevice();

		DirectX::ScratchImage scratch;
		DirectX::CaptureTexture(
			gfxDevice,
			gfxContext,
			_texture,
			scratch);

		DirectX::SaveToWICFile(
			scratch.GetImages(),
			scratch.GetImageCount(),
			DirectX::WIC_FLAGS_NONE,
			DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),
			g_pEnv->_fileSystem->GetLocalAbsolutePath(path).c_str());
	}

	uint32_t Texture3D::GetFormat()
	{
		return static_cast<uint32_t>(_format);
	}

	void Texture3D::ClearDepth(uint32_t flags)
	{
		auto gfxContext = (ID3D11DeviceContext*)g_pEnv->_graphicsDevice->GetNativeDeviceContext();
		auto gfxDevice = (ID3D11Device*)g_pEnv->_graphicsDevice->GetNativeDevice();

		gfxContext->ClearDepthStencilView(_depthStencilView, flags, 1.0f, 0);
	}

	void Texture3D::ClearRenderTargetView(const math::Color& colour)
	{
		if (_renderTargetView != nullptr)
		{
			auto gfxContext = (ID3D11DeviceContext*)g_pEnv->_graphicsDevice->GetNativeDeviceContext();

			gfxContext->ClearRenderTargetView(_renderTargetView, colour);
		}
	}

	void Texture3D::CopyTo(ITexture3D* other)
	{
		auto gfxContext = (ID3D11DeviceContext*)g_pEnv->_graphicsDevice->GetNativeDeviceContext();
		auto gfxDevice = (ID3D11Device*)g_pEnv->_graphicsDevice->GetNativeDevice();

		gfxContext->CopyResource(((Texture3D*)other)->_texture, this->_texture);
	}

	void* Texture3D::GetSharedHandle()
	{
		IDXGIResource* resource = 0;
		_texture->QueryInterface(IID_PPV_ARGS(&resource));

		HANDLE shareHandle;
		resource->GetSharedHandle(&shareHandle);

		resource->Release();

		return (void*)shareHandle;
	}
}