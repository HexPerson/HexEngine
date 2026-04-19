

#include "Texture2D.hpp"
#include "GraphicsDeviceD3D11.hpp"
#include <HexEngine.Core/HexEngine.hpp>

#include <DirectXTex\DirectXTex.h>

Texture2D::~Texture2D()
{
	Destroy();
}

void Texture2D::Destroy()
{
	SAFE_RELEASE(_texture);
	SAFE_RELEASE(_renderTargetView);
	SAFE_RELEASE(_shaderResourceView);
	SAFE_RELEASE(_depthStencilView);
}

void* Texture2D::GetNativePtr()
{
	return reinterpret_cast<void*>(_texture);
}

void Texture2D::SetPixels(uint8_t* data, uint32_t size)
{
	g_pGraphics->Lock();

	D3D11_MAPPED_SUBRESOURCE mapped = {};

	auto gfxDevice = (ID3D11DeviceContext*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDeviceContext();

	if (gfxDevice->Map(_texture, 0, D3D11_MAP_WRITE/*D3D11_MAP_WRITE_DISCARD*/, 0, &mapped) == S_OK)
	{
		memcpy(mapped.pData, data, size);

		gfxDevice->Unmap(_texture, 0);
	}

	g_pGraphics->Unlock();
}

void Texture2D::GetPixels(std::vector<uint8_t>& buffer)
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

	//D3D11_MAPPED_SUBRESOURCE mapped = {};

	//if (gfxContext->Map(_texture, 0, D3D11_MAP_READ/*D3D11_MAP_WRITE_DISCARD*/, 0, &mapped) == S_OK)
	//{
	//	if (buffer.size() > 0)
	//	{
	//		memcpy((void*)buffer.data(), mapped.pData, GetWidth() * GetHeight() * 4 * sizeof(uint8_t));
	//	}
	//	else
	//	{
	//		buffer.insert(buffer.end(), (uint8_t*)mapped.pData, (uint8_t*)mapped.pData + (GetWidth() * GetHeight() * 4 * sizeof(uint8_t)));
	//	}

	//	gfxContext->Unmap(_texture, 0);
	//}
}

void* Texture2D::LockPixels(int32_t* rowPitch)
{
	g_pGraphics->Lock();

	D3D11_MAPPED_SUBRESOURCE mapped = {};

	auto gfxDevice = (ID3D11DeviceContext*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDeviceContext();

	if (gfxDevice->Map(_texture, 0, D3D11_MAP_WRITE/*_DISCARD*/, 0, &mapped) == S_OK)
	{
		g_pGraphics->Unlock();

		if (rowPitch)
			*rowPitch = mapped.RowPitch;
		return mapped.pData;
	}

	g_pGraphics->Unlock();
	return nullptr;
}

void Texture2D::UnlockPixels()
{
	g_pGraphics->Lock();

	auto gfxDevice = (ID3D11DeviceContext*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDeviceContext();

	gfxDevice->Unmap(_texture, 0);

	g_pGraphics->Unlock();
}

void Texture2D::GetPixels(std::vector<float>& buffer)
{
	auto gfxContext = (ID3D11DeviceContext*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDeviceContext();
	auto gfxDevice = (ID3D11Device*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDevice();
#if 1

	D3D11_MAPPED_SUBRESOURCE mapped = {};

	if (gfxContext->Map(_texture, 0, D3D11_MAP_READ/*D3D11_MAP_WRITE_DISCARD*/, 0, &mapped) == S_OK)
	{
		if (buffer.size() > 0)
		{
			memcpy((void*)buffer.data(), mapped.pData, GetWidth() * GetHeight() * 4 * sizeof(float));
		}
		else
		{
			buffer.insert(buffer.end(), (uint8_t*)mapped.pData, (uint8_t*)mapped.pData + (GetWidth() * GetHeight() * 4 * sizeof(float)));
		}

		gfxContext->Unmap(_texture, 0);
	}

#else
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

	if (buffer.size() > 0)
	{
		memcpy(buffer.data(), pixels, pixelsSize);
	}
	else
	{
		buffer.clear();
		//buffer.resize(pixelsSize);
		buffer.insert(buffer.end(), pixels, pixels + pixelsSize);
	}
#endif
}

void Texture2D::SaveToFile(const fs::path& path)
{
	g_pGraphics->Lock();

	auto gfxContext = (ID3D11DeviceContext*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDeviceContext();
	auto gfxDevice = (ID3D11Device*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDevice();

	DirectX::ScratchImage scratch;
	CHECK_HR(DirectX::CaptureTexture(
		gfxDevice,
		gfxContext,
		_texture,
		scratch));

	CHECK_HR(DirectX::SaveToWICFile(
		scratch.GetImages(),
		scratch.GetImageCount(),
		DirectX::WIC_FLAGS_NONE,
		DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),
		HexEngine::g_pEnv->GetFileSystem().GetLocalAbsolutePath(path).c_str()));

	g_pGraphics->Unlock();
}

uint32_t Texture2D::GetFormat()
{
	return static_cast<uint32_t>(_format);
}

void Texture2D::ClearDepth(uint32_t flags)
{
	g_pGraphics->Lock();

	auto gfxContext = (ID3D11DeviceContext*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDeviceContext();
	auto gfxDevice = (ID3D11Device*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDevice();

	gfxContext->ClearDepthStencilView(_depthStencilView, flags, 1.0f, 0);

	g_pGraphics->Unlock();
}

void Texture2D::ClearRenderTargetView(const math::Color& colour)
{
	g_pGraphics->Lock();

	if (_renderTargetView != nullptr)
	{
		auto gfxContext = (ID3D11DeviceContext*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDeviceContext();

		gfxContext->ClearRenderTargetView(_renderTargetView, colour);
	}

	g_pGraphics->Unlock();
}

void Texture2D::CopyTo(ITexture2D* other)
{
	g_pGraphics->Lock();

	auto gfxContext = (ID3D11DeviceContext*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDeviceContext();
	auto gfxDevice = (ID3D11Device*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDevice();

	gfxContext->CopyResource(((Texture2D*)other)->_texture, this->_texture);

	g_pGraphics->Unlock();
}

void Texture2D::CopyTo(ITexture2D* other, const RECT& srcRect, const RECT& dstRect)
{
	g_pGraphics->Lock();

	auto gfxContext = (ID3D11DeviceContext*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDeviceContext();
	auto gfxDevice = (ID3D11Device*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDevice();

	/*DirectX::ScratchImage scratchFrom, scratchTo;
	CHECK_HR(DirectX::CaptureTexture(
		gfxDevice,
		gfxContext,
		_texture,
		scratchFrom));

	CHECK_HR(DirectX::CaptureTexture(
		gfxDevice,
		gfxContext,
		(ID3D11Texture2D*)other->GetNativePtr(),
		scratchTo));

	auto image1 = scratchFrom.GetImage(0, 0, 0);
	auto image2 = scratchTo.GetImage(0, 0, 0);

	DirectX::CopyRectangle(
		*image1,
		srcRect,
		*image2,
		dx::TEX_FILTER_DEFAULT,
		dstRect.x,
		dstRect.y);*/

	D3D11_BOX srcBox;
	srcBox.left = srcRect.left;
	srcBox.top = srcRect.top;
	srcBox.right = srcRect.right;
	srcBox.bottom = srcRect.bottom;
	srcBox.front = 0;
	srcBox.back = 1;

	gfxContext->CopySubresourceRegion(
		(ID3D11Texture2D*)other->GetNativePtr(),
		0,
		dstRect.left, dstRect.top, 0,
		_texture,
		0,
		&srcBox);

	g_pGraphics->Unlock();
}

void Texture2D::BlendTo_Additive(ITexture2D* other, HexEngine::IShader* optionalShader)
{
	g_pGraphics->Lock();

	auto gfxContext = (ID3D11DeviceContext*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDeviceContext();
	auto gfxDevice = (ID3D11Device*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDevice();

	HexEngine::GuiRenderer* renderer = HexEngine::g_pEnv->GetUIManager().GetRenderer();
	g_pGraphics->SetRenderTarget(other);

	renderer->StartFrame();

	g_pGraphics->SetBlendState(HexEngine::BlendState::Additive);

	renderer->FullScreenTexturedQuad(this, optionalShader);

	g_pGraphics->SetBlendState(HexEngine::BlendState::Opaque);

	g_pGraphics->Unlock();
}

void Texture2D::BlendTo_Additive_Double(ITexture2D* other, HexEngine::IShader* optionalShader)
{
	g_pGraphics->Lock();

	auto gfxContext = (ID3D11DeviceContext*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDeviceContext();
	auto gfxDevice = (ID3D11Device*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDevice();

	HexEngine::GuiRenderer* renderer = HexEngine::g_pEnv->GetUIManager().GetRenderer();
	g_pGraphics->SetRenderTarget(other);

	renderer->StartFrame();

	g_pGraphics->SetBlendState(HexEngine::BlendState::Additive);

	renderer->DoubleScreenTexturedQuad(this, optionalShader);

	g_pGraphics->SetBlendState(HexEngine::BlendState::Opaque);

	g_pGraphics->Unlock();
}

void Texture2D::BlendTo_Alpha(ITexture2D* other, HexEngine::IShader* optionalShader)
{
	g_pGraphics->Lock();

	auto gfxContext = (ID3D11DeviceContext*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDeviceContext();
	auto gfxDevice = (ID3D11Device*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDevice();

	HexEngine::GuiRenderer* renderer = HexEngine::g_pEnv->GetUIManager().GetRenderer();
	g_pGraphics->SetRenderTarget(other);

	renderer->StartFrame();

	float blend[4] = { 1.0f };
	float blend2[4] = { 0.0f };
	gfxContext->OMSetBlendState(g_pGraphics->_states->AlphaBlend(), blend, 0xffffffff);

	renderer->FullScreenTexturedQuad(this, optionalShader);

	gfxContext->OMSetBlendState(g_pGraphics->_states->Opaque(), blend2, 0xffffffff);

	g_pGraphics->Unlock();
}

void Texture2D::BlendTo_NonPremultiplied(ITexture2D* other, HexEngine::IShader* optionalShader)
{
	g_pGraphics->Lock();

	auto gfxContext = (ID3D11DeviceContext*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDeviceContext();
	auto gfxDevice = (ID3D11Device*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDevice();

	HexEngine::GuiRenderer* renderer = HexEngine::g_pEnv->GetUIManager().GetRenderer();
	g_pGraphics->SetRenderTarget(other);

	renderer->StartFrame();

	float blend[4] = { 1.0f };
	float blend2[4] = { 0.0f };
	gfxContext->OMSetBlendState(g_pGraphics->_states->NonPremultiplied(), blend, 0xffffffff);

	renderer->FullScreenTexturedQuad(this, optionalShader);

	gfxContext->OMSetBlendState(g_pGraphics->_states->Opaque(), blend2, 0xffffffff);

	g_pGraphics->Unlock();
}

void* Texture2D::GetSharedHandle()
{
	auto gfxContext = (ID3D11DeviceContext*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDeviceContext();
	auto gfxDevice = (ID3D11Device*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDevice();

	IDXGIResource* resource = 0;
	_texture->QueryInterface(IID_PPV_ARGS(&resource));

	HANDLE shareHandle;
	resource->GetSharedHandle(&shareHandle);

	//resource->Release();

	return (void*)shareHandle;
}
