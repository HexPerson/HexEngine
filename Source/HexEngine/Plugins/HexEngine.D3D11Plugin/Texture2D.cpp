

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
	// This used to route through DirectX::CaptureTexture, which stages the
	// ENTIRE mip chain of the full-resolution texture to the CPU (a 2048^2
	// BC texture with mips is ~5.6 MB copied through a staging resource plus
	// a full pipeline flush and a blocking Map) and only then discarded all
	// but the one small mip it decoded. Profiled as the dominant render-thread
	// stall whenever GI pulled a new material's pixels at runtime. Copy ONLY
	// the chosen mip into a mip-sized staging texture instead: ~16 KB moved,
	// and any BC decode runs at the small mip's resolution.
	auto gfxContext = (ID3D11DeviceContext*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDeviceContext();
	auto gfxDevice = (ID3D11Device*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDevice();
	if (_texture == nullptr || gfxContext == nullptr || gfxDevice == nullptr)
		return false;

	D3D11_TEXTURE2D_DESC desc = {};
	_texture->GetDesc(&desc);
	if (desc.Width == 0 || desc.Height == 0 || desc.MipLevels == 0)
		return false;
	const DXGI_FORMAT format = desc.Format;
	const bool isCompressed = DirectX::IsCompressed(format);
	if (!isCompressed && DirectX::BitsPerPixel(format) != 32)
	{
		// Callers treat the payload as 4 bytes per pixel; other layouts would
		// be misread - report unsupported so they fall back to GetPixels.
		return false;
	}

	// Smallest existing mip whose larger dimension still covers maxDimension
	// (or the smallest mip there is). Only this one subresource is copied.
	const uint32_t maxDim = static_cast<uint32_t>(std::max(1, maxDimension));
	uint32_t chosenMip = 0;
	for (uint32_t mip = 0; mip < desc.MipLevels; ++mip)
	{
		chosenMip = mip;
		const uint32_t w = std::max(1u, desc.Width >> mip);
		const uint32_t h = std::max(1u, desc.Height >> mip);
		if (std::max(w, h) <= maxDim)
			break;
	}
	const uint32_t mipW = std::max(1u, desc.Width >> chosenMip);
	const uint32_t mipH = std::max(1u, desc.Height >> chosenMip);

	D3D11_TEXTURE2D_DESC stagingDesc = desc;
	stagingDesc.Width = mipW;
	stagingDesc.Height = mipH;
	stagingDesc.MipLevels = 1;
	stagingDesc.ArraySize = 1;
	stagingDesc.SampleDesc.Count = 1;
	stagingDesc.SampleDesc.Quality = 0;
	stagingDesc.Usage = D3D11_USAGE_STAGING;
	stagingDesc.BindFlags = 0;
	stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	stagingDesc.MiscFlags = 0;

	// Tight-pitch CPU copy of the raw (possibly block-compressed) mip.
	size_t rowPitch = 0;
	size_t slicePitch = 0;
	if (FAILED(DirectX::ComputePitch(format, mipW, mipH, rowPitch, slicePitch)) || slicePitch == 0)
		return false;
	std::vector<uint8_t> rawMip(slicePitch);
	const size_t copyRows = isCompressed ? static_cast<size_t>((mipH + 3u) / 4u) : static_cast<size_t>(mipH);

	bool copied = false;
	g_pGraphics->Lock();
	ID3D11Texture2D* staging = nullptr;
	if (SUCCEEDED(gfxDevice->CreateTexture2D(&stagingDesc, nullptr, &staging)) && staging != nullptr)
	{
		const UINT srcSubresource = D3D11CalcSubresource(chosenMip, 0, desc.MipLevels);
		gfxContext->CopySubresourceRegion(staging, 0, 0, 0, 0, _texture, srcSubresource, nullptr);
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (SUCCEEDED(gfxContext->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)) && mapped.pData != nullptr)
		{
			const size_t rowBytes = std::min<size_t>(rowPitch, mapped.RowPitch);
			for (size_t row = 0; row < copyRows; ++row)
			{
				memcpy(rawMip.data() + row * rowPitch,
					static_cast<const uint8_t*>(mapped.pData) + row * mapped.RowPitch,
					rowBytes);
			}
			gfxContext->Unmap(staging, 0);
			copied = true;
		}
		staging->Release();
	}
	g_pGraphics->Unlock();
	if (!copied)
		return false;

	DirectX::Image mipImage = {};
	mipImage.width = mipW;
	mipImage.height = mipH;
	mipImage.format = format;
	mipImage.rowPitch = rowPitch;
	mipImage.slicePitch = slicePitch;
	mipImage.pixels = rawMip.data();

	DirectX::ScratchImage decompressed;
	const DirectX::Image* source = &mipImage;
	if (isCompressed)
	{
		if (FAILED(DirectX::Decompress(mipImage, DXGI_FORMAT_R8G8B8A8_UNORM, decompressed)))
			return false;
		source = decompressed.GetImage(0, 0, 0);
		if (source == nullptr || source->pixels == nullptr)
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
			memcpy((void*)buffer.data(), mapped.pData, static_cast<size_t>(GetWidth()) * GetHeight() * 4 * sizeof(float));
		}
		else
		{
			buffer.insert(buffer.end(), (uint8_t*)mapped.pData, (uint8_t*)mapped.pData + (static_cast<size_t>(GetWidth()) * GetHeight() * 4 * sizeof(float)));
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
