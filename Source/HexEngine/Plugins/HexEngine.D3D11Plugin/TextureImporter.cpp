

#include "TextureImporter.hpp"
#include <HexEngine.Core/HexEngine.hpp>
#include "GraphicsDeviceD3D11.hpp"
#include <DDSTextureLoader.h>
#include <WICTextureLoader.h>
#include <DirectXTex\DirectXTex.h>

namespace
{
	const DirectX::Image* GetImagesWithMipmaps(
		const DirectX::ScratchImage& sourceImage,
		DirectX::TexMetadata& metaData,
		DirectX::ScratchImage& generatedMipChain)
	{
		if (metaData.dimension == DirectX::TEX_DIMENSION_TEXTURE2D && metaData.mipLevels <= 1 && metaData.width > 1 && metaData.height > 1)
		{
			if (SUCCEEDED(DirectX::GenerateMipMaps(
				sourceImage.GetImages(),
				sourceImage.GetImageCount(),
				metaData,
				DirectX::TEX_FILTER_DEFAULT,
				0,
				generatedMipChain)))
			{
				metaData = generatedMipChain.GetMetadata();
				return generatedMipChain.GetImages();
			}
		}

		return sourceImage.GetImages();
	}
}

TextureImporter::TextureImporter()
{
	HexEngine::g_pEnv->GetResourceSystem().RegisterResourceLoader(this);
}

TextureImporter::~TextureImporter()
{
	HexEngine::g_pEnv->GetResourceSystem().UnregisterResourceLoader(this);
}

std::shared_ptr<HexEngine::IResource> TextureImporter::LoadResourceFromFile(const fs::path& absolutePath, HexEngine::FileSystem* fileSystem, const HexEngine::ResourceLoadOptions* options /*= nullptr*/)
{
	if (HexEngine::g_pEnv->GetFileSystem().DoesAbsolutePathExist(absolutePath) == false)
	{
		LOG_WARN("The texture at path '%s' does not exist, cannot load!", absolutePath.u8string().c_str());
		return nullptr;
	}
	auto extension = absolutePath.extension();

	std::string lowerExtension = extension.string();
	std::transform(lowerExtension.begin(), lowerExtension.end(), lowerExtension.begin(), ::tolower);

	auto gfxDevice = (ID3D11Device*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDevice();
	auto gfxContexte = (ID3D11DeviceContext*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDeviceContext();



	ID3D11Texture2D* d3dTexture = nullptr;
	ID3D11ShaderResourceView* d3dSRV = nullptr;

	g_pGraphics->Lock();

	LOG_INFO("Loading texture '%S'", absolutePath.c_str());

	DirectX::TexMetadata metaData;
	DirectX::ScratchImage scratchImage;
	DirectX::ScratchImage mipChain;

	if (lowerExtension == ".dds")
	{
		/*CHECK_HR(DirectX::CreateDDSTextureFromFile(
			gfxDevice,
			absolutePath.c_str(),
			(ID3D11Resource**)&d3dTexture,
			&d3dSRV));*/

		CHECK_HR(DirectX::LoadFromDDSFile(absolutePath.c_str(), DirectX::DDS_FLAGS_NONE, &metaData, scratchImage));
	}
	else if (lowerExtension == ".tga")
	{

		auto hr = DirectX::LoadFromTGAFile(absolutePath.c_str(), &metaData, scratchImage);
		if (FAILED(hr))
		{

		}
	}
	else
	{
		CHECK_HR(DirectX::LoadFromWICFile(absolutePath.c_str(), DirectX::WIC_FLAGS_NONE, &metaData, scratchImage));

		/*CHECK_HR(DirectX::CreateWICTextureFromFileEx(
			gfxDevice,
			gfxContexte,
			absolutePath.c_str(),
			0,
			D3D11_USAGE_DEFAULT,
			D3D11_BIND_SHADER_RESOURCE,
			0,0,
			DirectX::WIC_LOADER_FORCE_RGBA32,
			(ID3D11Resource**)&d3dTexture,
			&d3dSRV));*/
	}

	const DirectX::Image* imageData = GetImagesWithMipmaps(scratchImage, metaData, mipChain);
	const size_t imageCount = mipChain.GetImageCount() > 0 ? mipChain.GetImageCount() : scratchImage.GetImageCount();

	CHECK_HR(DirectX::CreateTexture(
		gfxDevice,
		imageData,
		imageCount,
		metaData,
		(ID3D11Resource**)&d3dTexture));

	CHECK_HR(DirectX::CreateShaderResourceView(
		gfxDevice,
		imageData,
		imageCount,
		metaData,
		&d3dSRV));

	g_pGraphics->Unlock();

	if (d3dTexture == nullptr)
	{
		return nullptr;
	}

	std::shared_ptr<Texture2D> tex = std::shared_ptr<Texture2D>(new Texture2D, HexEngine::ResourceDeleter());

	D3D11_TEXTURE2D_DESC desc;
	d3dTexture->GetDesc(&desc);

	tex->_width = desc.Width;
	tex->_height = desc.Height;
	tex->_format = desc.Format;
	tex->_texture = d3dTexture;
	tex->_shaderResourceView = d3dSRV;

#ifdef _DEBUG
	std::string path = absolutePath.string().c_str();
	((ID3D11Texture2D*)tex->GetNativePtr())->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)path.length(), path.data());
#endif

	return tex;
}

std::shared_ptr<HexEngine::IResource> TextureImporter::LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, HexEngine::FileSystem* fileSystem, const HexEngine::ResourceLoadOptions* options)
{
	auto extension = relativePath.extension();

	auto gfxDevice = (ID3D11Device*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDevice();
	auto gfxContexte = (ID3D11DeviceContext*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDeviceContext();

	ID3D11Texture2D* d3dTexture = nullptr;
	ID3D11ShaderResourceView* d3dSRV = nullptr;

	g_pGraphics->Lock();

	LOG_INFO("Loading texture '%S'", relativePath.c_str());

	if (extension == ".dds")
	{
		CHECK_HR(DirectX::CreateDDSTextureFromMemory(
			gfxDevice,
			data.data(),
			data.size(),
			(ID3D11Resource**)&d3dTexture,
			&d3dSRV));
	}
	else if (extension == ".tga")
	{
		DirectX::TexMetadata metaData;
		DirectX::ScratchImage scratchImage;
		DirectX::ScratchImage mipChain;

		if (DirectX::LoadFromTGAMemory(
			data.data(),
			data.size(),
			&metaData,
			scratchImage) == S_OK)
		{
			const DirectX::Image* imageData = GetImagesWithMipmaps(scratchImage, metaData, mipChain);
			const size_t imageCount = mipChain.GetImageCount() > 0 ? mipChain.GetImageCount() : scratchImage.GetImageCount();

			CHECK_HR(DirectX::CreateTexture(
				gfxDevice,
				imageData,
				imageCount,
				metaData,
				(ID3D11Resource**)&d3dTexture));

			CHECK_HR(DirectX::CreateShaderResourceView(
				gfxDevice,
				imageData,
				imageCount,
				metaData,
				&d3dSRV));
		}
	}
	else
	{
		DirectX::TexMetadata metaData;
		DirectX::ScratchImage scratchImage;
		DirectX::ScratchImage mipChain;

		CHECK_HR(DirectX::LoadFromWICMemory(
			data.data(),
			data.size(),
			DirectX::WIC_FLAGS_NONE,
			&metaData,
			scratchImage));

		const DirectX::Image* imageData = GetImagesWithMipmaps(scratchImage, metaData, mipChain);
		const size_t imageCount = mipChain.GetImageCount() > 0 ? mipChain.GetImageCount() : scratchImage.GetImageCount();

		CHECK_HR(DirectX::CreateTexture(
			gfxDevice,
			imageData,
			imageCount,
			metaData,
			(ID3D11Resource**)&d3dTexture));

		CHECK_HR(DirectX::CreateShaderResourceView(
			gfxDevice,
			imageData,
			imageCount,
			metaData,
			&d3dSRV));
	}

	g_pGraphics->Unlock();

	if (d3dTexture == nullptr)
	{
		return nullptr;
	}

	std::shared_ptr<Texture2D> tex = std::shared_ptr<Texture2D>(new Texture2D, HexEngine::ResourceDeleter());

	D3D11_TEXTURE2D_DESC desc;
	d3dTexture->GetDesc(&desc);

	tex->_width = desc.Width;
	tex->_height = desc.Height;
	tex->_format = desc.Format;
	tex->_texture = d3dTexture;
	tex->_shaderResourceView = d3dSRV;

#ifdef _DEBUG
	std::string path = relativePath.string().c_str();
	((ID3D11Texture2D*)tex->GetNativePtr())->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)path.length(), path.data());
#endif

	return tex;
}

void TextureImporter::UnloadResource(HexEngine::IResource* resource)
{
	SAFE_DELETE(resource);
}

std::vector<std::string> TextureImporter::GetSupportedResourceExtensions()
{
	return { ".png", ".jpg", ".jpeg", ".bmp", ".dds", ".tga", ".tif", ".psd" };
}

std::wstring TextureImporter::GetResourceDirectory() const
{
	return L"Textures";
}
