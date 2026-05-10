

#include "Texture3D.hpp"
#include "GraphicsDeviceD3D11.hpp"
#include <HexEngine.Core/HexEngine.hpp>

#include <DirectXTex\DirectXTex.h>

Texture3D::~Texture3D()
{
	Destroy();
}

void Texture3D::Destroy()
{
	SAFE_RELEASE(_texture);
	SAFE_RELEASE(_renderTargetView);
	SAFE_RELEASE(_shaderResourceView);
	SAFE_RELEASE(_unorderedAccessView);
	SAFE_RELEASE(_depthStencilView);
}

void* Texture3D::GetNativePtr()
{
	return reinterpret_cast<void*>(_texture);
}

void Texture3D::SetPixels(uint8_t* data, uint32_t size, int32_t slice)
{
	if (_texture == nullptr || data == nullptr || size == 0)
	{
		return;
	}

	auto* gfxContext = (ID3D11DeviceContext*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDeviceContext();
	const uint32_t rowPitch = static_cast<uint32_t>(_width * sizeof(float));
	const uint32_t depthPitch = static_cast<uint32_t>(_width * _height * sizeof(float));
	gfxContext->UpdateSubresource(_texture, 0, nullptr, data, rowPitch, depthPitch);
}

void* Texture3D::LockPixels(int32_t* rowPitch)
{
	g_pGraphics->Lock();

	D3D11_MAPPED_SUBRESOURCE mapped = {};

	auto gfxDevice = (ID3D11DeviceContext*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDeviceContext();

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

	auto gfxDevice = (ID3D11DeviceContext*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDeviceContext();

	gfxDevice->Unmap(_texture, 0);

	g_pGraphics->Unlock();
}

void Texture3D::GetPixels(std::vector<uint8_t>& buffer)
{
	auto gfxContext = (ID3D11DeviceContext*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDeviceContext();
	auto gfxDevice = (ID3D11Device*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDevice();

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

void Texture3D::GetPixels(std::vector<float>& buffer)
{
	if (_texture == nullptr || _width <= 0 || _height <= 0 || _depth <= 0)
	{
		buffer.clear();
		return;
	}

	auto* gfxContext = (ID3D11DeviceContext*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDeviceContext();
	auto* gfxDevice = (ID3D11Device*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDevice();

	D3D11_TEXTURE3D_DESC desc = {};
	_texture->GetDesc(&desc);

	D3D11_TEXTURE3D_DESC stagingDesc = desc;
	stagingDesc.Usage = D3D11_USAGE_STAGING;
	stagingDesc.BindFlags = 0;
	stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	stagingDesc.MiscFlags = 0;

	ID3D11Texture3D* staging = nullptr;
	if (FAILED(gfxDevice->CreateTexture3D(&stagingDesc, nullptr, &staging)) || staging == nullptr)
	{
		buffer.clear();
		return;
	}

	gfxContext->CopyResource(staging, _texture);

	D3D11_MAPPED_SUBRESOURCE mapped = {};
	if (FAILED(gfxContext->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)) || mapped.pData == nullptr)
	{
		SAFE_RELEASE(staging);
		buffer.clear();
		return;
	}

	const size_t voxelCount = static_cast<size_t>(_width) * static_cast<size_t>(_height) * static_cast<size_t>(_depth);
	buffer.resize(voxelCount);
	const size_t rowBytes = static_cast<size_t>(_width) * sizeof(float);
	const uint8_t* srcBase = reinterpret_cast<const uint8_t*>(mapped.pData);

	for (int32_t z = 0; z < _depth; ++z)
	{
		for (int32_t y = 0; y < _height; ++y)
		{
			const uint8_t* srcRow = srcBase + (static_cast<size_t>(z) * mapped.DepthPitch) + (static_cast<size_t>(y) * mapped.RowPitch);
			float* dstRow = buffer.data() + (static_cast<size_t>(z) * static_cast<size_t>(_height) * static_cast<size_t>(_width)) + (static_cast<size_t>(y) * static_cast<size_t>(_width));
			memcpy(dstRow, srcRow, rowBytes);
		}
	}

	gfxContext->Unmap(staging, 0);
	SAFE_RELEASE(staging);
}

void Texture3D::SaveToFile(const fs::path& path)
{
	auto gfxContext = (ID3D11DeviceContext*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDeviceContext();
	auto gfxDevice = (ID3D11Device*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDevice();

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
		HexEngine::g_pEnv->GetFileSystem().GetLocalAbsolutePath(path).c_str());
}

uint32_t Texture3D::GetFormat()
{
	return static_cast<uint32_t>(_format);
}

void Texture3D::ClearDepth(uint32_t flags)
{
	auto gfxContext = (ID3D11DeviceContext*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDeviceContext();
	auto gfxDevice = (ID3D11Device*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDevice();

	gfxContext->ClearDepthStencilView(_depthStencilView, flags, 1.0f, 0);
}

void Texture3D::ClearRenderTargetView(const math::Color& colour)
{
	if (_renderTargetView != nullptr)
	{
		auto gfxContext = (ID3D11DeviceContext*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDeviceContext();

		gfxContext->ClearRenderTargetView(_renderTargetView, colour);
	}
}

void Texture3D::CopyTo(ITexture3D* other)
{
	auto gfxContext = (ID3D11DeviceContext*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDeviceContext();
	auto gfxDevice = (ID3D11Device*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDevice();

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
