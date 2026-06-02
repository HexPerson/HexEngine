
#include "TextureImporterD3D12.hpp"
#include "GraphicsDeviceD3D12.hpp"
#include "Texture2DD3D12.hpp"
#include "FormatsD3D12.hpp"

#include <HexEngine.Core/HexEngine.hpp>
#include <directxtex/DirectXTex/DirectXTex.h>
#include <algorithm>
#include <cwctype>

TextureImporterD3D12::TextureImporterD3D12()
{
	HexEngine::g_pEnv->GetResourceSystem().RegisterResourceLoader(this);
}

TextureImporterD3D12::~TextureImporterD3D12()
{
	HexEngine::g_pEnv->GetResourceSystem().UnregisterResourceLoader(this);
}

namespace
{
	// DirectXTex helper: load whichever container the path's extension matches,
	// returning the decoded image and metadata. Mirrors the dispatch shape used
	// in the D3D11 importer so behaviour stays consistent between backends.
	bool DecodeImage(const std::vector<uint8_t>& data, const fs::path& path, DirectX::TexMetadata& outMeta, DirectX::ScratchImage& outImage)
	{
		std::string ext = path.extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

		if (ext == ".dds")
			return SUCCEEDED(DirectX::LoadFromDDSMemory(data.data(), data.size(), DirectX::DDS_FLAGS_NONE, &outMeta, outImage));
		if (ext == ".tga")
			return SUCCEEDED(DirectX::LoadFromTGAMemory(data.data(), data.size(), &outMeta, outImage));
		// .png, .jpg, .jpeg, .bmp, .tif, .psd all go through WIC.
		return SUCCEEDED(DirectX::LoadFromWICMemory(data.data(), data.size(), DirectX::WIC_FLAGS_NONE, &outMeta, outImage));
	}

	HexEngine::TextureFormat DxgiToTextureFormat(DXGI_FORMAT f)
	{
		// Inverse of ToDXGI12 - small subset covering what WIC / DDS hands back.
		// Anything not listed falls through to R8G8B8A8_UNORM as a safe default;
		// the texture will display but the format won't be exact.
		switch (f)
		{
		case DXGI_FORMAT_R8G8B8A8_UNORM:       return HexEngine::TextureFormat::R8G8B8A8_UNORM;
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:  return HexEngine::TextureFormat::R8G8B8A8_UNORM_SRGB;
		case DXGI_FORMAT_B8G8R8A8_UNORM:       return HexEngine::TextureFormat::B8G8R8A8_UNORM;
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:  return HexEngine::TextureFormat::B8G8R8A8_UNORM_SRGB;
		case DXGI_FORMAT_R16G16B16A16_FLOAT:   return HexEngine::TextureFormat::R16G16B16A16_FLOAT;
		case DXGI_FORMAT_R32G32B32A32_FLOAT:   return HexEngine::TextureFormat::R32G32B32A32_FLOAT;
		case DXGI_FORMAT_R8_UNORM:             return HexEngine::TextureFormat::R8_UNORM;
		case DXGI_FORMAT_R8G8_UNORM:           return HexEngine::TextureFormat::R8G8_UNORM;
		case DXGI_FORMAT_R16_FLOAT:            return HexEngine::TextureFormat::R16_FLOAT;
		case DXGI_FORMAT_R11G11B10_FLOAT:      return HexEngine::TextureFormat::R11G11B10_FLOAT;
		default:                               return HexEngine::TextureFormat::R8G8B8A8_UNORM;
		}
	}
}

std::shared_ptr<HexEngine::IResource> TextureImporterD3D12::LoadResourceFromFile(const fs::path& absolutePath, HexEngine::FileSystem* fileSystem, const HexEngine::ResourceLoadOptions* options)
{
	// Read bytes once and dispatch through the from-memory path. Keeps the
	// container-decode logic in a single place.
	const auto* effectiveFs = fileSystem ? fileSystem : &HexEngine::g_pEnv->GetFileSystem();
	if (!effectiveFs->DoesAbsolutePathExist(absolutePath))
	{
		LOG_WARN("D3D12 TextureImporter: '%s' not found", absolutePath.u8string().c_str());
		return nullptr;
	}

	HexEngine::DiskFile file(absolutePath, std::ios::in | std::ios::binary);
	if (!file.Open())
		return nullptr;

	std::vector<uint8_t> bytes(file.GetSize());
	file.Read(bytes.data(), (uint32_t)bytes.size());
	return LoadResourceFromMemory(bytes, absolutePath, fileSystem, options);
}

std::shared_ptr<HexEngine::IResource> TextureImporterD3D12::LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, HexEngine::FileSystem* fileSystem, const HexEngine::ResourceLoadOptions* options)
{
	if (data.empty())
		return nullptr;

	DirectX::TexMetadata meta = {};
	DirectX::ScratchImage img;
	if (!DecodeImage(data, relativePath, meta, img))
	{
		LOG_WARN("D3D12 TextureImporter: DirectXTex failed to decode '%S'", relativePath.c_str());
		return nullptr;
	}

	const DirectX::Image* topImage = img.GetImage(0, 0, 0);
	if (topImage == nullptr)
		return nullptr;

	auto* device = static_cast<GraphicsDeviceD3D12*>(HexEngine::g_pEnv->_graphicsDevice);
	if (device == nullptr)
		return nullptr;

	HexEngine::TextureDesc desc;
	desc.width      = (int32_t)meta.width;
	desc.height     = (int32_t)meta.height;
	desc.format     = DxgiToTextureFormat(meta.format);
	desc.bindFlags  = HexEngine::BindFlags::ShaderResource;
	desc.arraySize  = 1;
	desc.mipLevels  = 1;
	desc.dimension  = HexEngine::ResourceDimension::Texture2D;
	desc.usage      = HexEngine::ResourceUsage::Default;

	// Pixel upload happens in CreateTexture2D when initialData is non-null;
	// pass the top-mip bytes.
	HexEngine::SubresourceData subres;
	subres.data            = topImage->pixels;
	subres.rowPitchBytes   = (uint32_t)topImage->rowPitch;
	subres.slicePitchBytes = (uint32_t)topImage->slicePitch;

	auto* tex = static_cast<Texture2DD3D12*>(device->CreateTexture2D(desc, &subres));
	if (tex == nullptr)
	{
		LOG_WARN("D3D12 TextureImporter: device->CreateTexture2D returned null for '%S'", relativePath.c_str());
		return nullptr;
	}

#ifdef _DEBUG
	tex->SetDebugName(relativePath.string());
#endif

	// IResource expects shared_ptr with the engine's deleter so UnloadResource
	// gets called for hot-reload / asset-system cleanup.
	return std::shared_ptr<HexEngine::IResource>(tex, HexEngine::ResourceDeleter());
}

void TextureImporterD3D12::UnloadResource(HexEngine::IResource* resource)
{
	SAFE_DELETE(resource);
}

std::vector<std::string> TextureImporterD3D12::GetSupportedResourceExtensions()
{
	return { ".png", ".jpg", ".jpeg", ".bmp", ".dds", ".tga", ".tif", ".psd" };
}

std::wstring TextureImporterD3D12::GetResourceDirectory() const
{
	return L"Textures";
}
