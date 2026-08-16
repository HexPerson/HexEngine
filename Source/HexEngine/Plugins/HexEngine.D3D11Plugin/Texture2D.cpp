

#include "Texture2D.hpp"
#include "GraphicsDeviceD3D11.hpp"
#include <HexEngine.Core/HexEngine.hpp>

#include <DirectXTex\DirectXTex.h>

#include <algorithm>

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

	// CaptureTexture internally allocates a staging texture, CopyResource's
	// the source into it, then Map/Unmap's the staging - all on the immediate
	// context. Without the device lock, any other thread driving the context
	// (background streaming, async loaders, etc.) races us and the D3D11
	// debug layer flags MISCELLANEOUS CORRUPTION #28 / CORRUPTED_MULTITHREADING.
	DirectX::ScratchImage scratch;
	g_pGraphics->Lock();
	const HRESULT captureHr = DirectX::CaptureTexture(
		gfxDevice,
		gfxContext,
		_texture,
		scratch);
	g_pGraphics->Unlock();
	CHECK_HR(captureHr);

	// Block-compressed sources capture as raw BC blocks, which no CPU caller can
	// texel-read (consumers validate against a tight width*height*4 layout and
	// silently treat the texture as absent). Decompress to RGBA8 so compressed
	// albedo/emissive textures are readable; sRGB decode stays the caller's job.
	DirectX::ScratchImage decompressed;
	if (DirectX::IsCompressed(scratch.GetMetadata().format))
	{
		const HRESULT decompressHr = DirectX::Decompress(
			scratch.GetImages(),
			scratch.GetImageCount(),
			scratch.GetMetadata(),
			DXGI_FORMAT_R8G8B8A8_UNORM,
			decompressed);
		if (SUCCEEDED(decompressHr))
		{
			scratch = std::move(decompressed);
		}
	}

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

bool Texture2D::GetPixelsScaled(std::vector<uint8_t>& buffer, int32_t maxDimension, int32_t& outWidth, int32_t& outHeight)
{
	auto gfxContext = (ID3D11DeviceContext*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDeviceContext();
	auto gfxDevice = (ID3D11Device*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDevice();

	DirectX::ScratchImage scratch;
	g_pGraphics->Lock();
	const HRESULT captureHr = DirectX::CaptureTexture(gfxDevice, gfxContext, _texture, scratch);
	g_pGraphics->Unlock();
	if (FAILED(captureHr))
		return false;

	const auto& meta = scratch.GetMetadata();
	if (meta.mipLevels == 0 || meta.width == 0 || meta.height == 0)
		return false;

	// Smallest existing mip whose larger dimension still covers maxDimension
	// (or the smallest mip there is). Only this one image gets decoded.
	const size_t maxDim = static_cast<size_t>(std::max(1, maxDimension));
	size_t chosenMip = 0;
	for (size_t mip = 0; mip < meta.mipLevels; ++mip)
	{
		chosenMip = mip;
		const size_t w = std::max<size_t>(1u, meta.width >> mip);
		const size_t h = std::max<size_t>(1u, meta.height >> mip);
		if (std::max(w, h) <= maxDim)
			break;
	}

	const DirectX::Image* mipImage = scratch.GetImage(chosenMip, 0, 0);
	if (mipImage == nullptr || mipImage->pixels == nullptr)
		return false;

	DirectX::ScratchImage decompressed;
	const DirectX::Image* source = mipImage;
	if (DirectX::IsCompressed(meta.format))
	{
		if (FAILED(DirectX::Decompress(*mipImage, DXGI_FORMAT_R8G8B8A8_UNORM, decompressed)))
			return false;
		source = decompressed.GetImage(0, 0, 0);
		if (source == nullptr || source->pixels == nullptr)
			return false;
	}
	else if (DirectX::BitsPerPixel(meta.format) != 32)
	{
		// Callers treat the payload as 4 bytes per pixel; other layouts would
		// be misread - report unsupported so they fall back to GetPixels.
		return false;
	}

	outWidth = static_cast<int32_t>(source->width);
	outHeight = static_cast<int32_t>(source->height);
	const size_t tightPitch = source->width * 4u;
	buffer.resize(tightPitch * source->height);
	for (size_t row = 0; row < source->height; ++row)
	{
		memcpy(buffer.data() + row * tightPitch, source->pixels + row * source->rowPitch, tightPitch);
	}
	return true;
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

	g_pGraphics->Lock();
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
	g_pGraphics->Unlock();

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
