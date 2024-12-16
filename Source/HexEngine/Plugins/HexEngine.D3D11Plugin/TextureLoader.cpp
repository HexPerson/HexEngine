

#include "TextureLoader.hpp"
#include <HexEngine.Core/HexEngine.hpp>
#include "GraphicsDeviceD3D11.hpp"
#include <DDSTextureLoader.h>
#include <WICTextureLoader.h>
#include <DirectXTex\DirectXTex.h>

namespace HexEngine
{
	TextureLoader::TextureLoader()
	{
		g_pEnv->_resourceSystem->RegisterResourceLoader(this);
	}

	TextureLoader::~TextureLoader()
	{
		g_pEnv->_resourceSystem->UnregisterResourceLoader(this);
	}

	IResource* TextureLoader::LoadResourceFromFile(const fs::path& absolutePath, FileSystem* fileSystem, const ResourceLoadOptions* options /*= nullptr*/)
	{
		if (g_pEnv->_fileSystem->DoesAbsolutePathExist(absolutePath) == false)
		{
			LOG_WARN("The texture at path '%s' does not exist, cannot load!", absolutePath.u8string().c_str());
			return nullptr;
		}
		auto extension = absolutePath.extension();

		std::string lowerExtension = extension.string();
		std::transform(lowerExtension.begin(), lowerExtension.end(), lowerExtension.begin(), ::tolower);

		auto gfxDevice = (ID3D11Device*)g_pEnv->_graphicsDevice->GetNativeDevice();
		auto gfxContexte = (ID3D11DeviceContext*)g_pEnv->_graphicsDevice->GetNativeDeviceContext();

		Texture2D* tex = new Texture2D;

		ID3D11Texture2D* d3dTexture = nullptr;
		ID3D11ShaderResourceView* d3dSRV = nullptr;

		g_pGraphics->Lock();

		LOG_INFO("Loading texture '%S'", absolutePath.c_str());
		
		if (lowerExtension == ".dds")
		{
			CHECK_HR(DirectX::CreateDDSTextureFromFile(
				gfxDevice,
				absolutePath.c_str(),
				(ID3D11Resource**)&d3dTexture,
				&d3dSRV));
		}
		else if (lowerExtension == ".tga")
		{
			DirectX::TexMetadata metaData;
			DirectX::ScratchImage scratchImage;

			if (DirectX::LoadFromTGAFile(absolutePath.c_str(), &metaData, scratchImage) == S_OK)
			{
				DirectX::CreateTexture(
					gfxDevice,
					scratchImage.GetImages(),
					scratchImage.GetImageCount(),
					metaData,
					(ID3D11Resource**)&d3dTexture);

				DirectX::CreateShaderResourceView(
					gfxDevice,
					scratchImage.GetImages(),
					scratchImage.GetImageCount(),
					metaData,
					&d3dSRV);
			}
		}
		else
		{
			CHECK_HR(DirectX::CreateWICTextureFromFileEx(
				gfxDevice,
				gfxContexte,
				absolutePath.c_str(),
				0,
				D3D11_USAGE_DEFAULT,
				D3D11_BIND_SHADER_RESOURCE,
				0,0,
				DirectX::WIC_LOADER_FORCE_RGBA32,
				(ID3D11Resource**)&d3dTexture,
				&d3dSRV));
		}

		g_pGraphics->Unlock();

		if (d3dTexture == nullptr)
		{
			delete tex;
			return nullptr;
		}

		D3D11_TEXTURE2D_DESC desc;
		d3dTexture->GetDesc(&desc);

		tex->_width = desc.Width;
		tex->_height = desc.Height;
		tex->_texture = d3dTexture;
		tex->_shaderResourceView = d3dSRV;

#ifdef _DEBUG
		std::string path = absolutePath.string().c_str();
		((ID3D11Texture2D*)tex->GetNativePtr())->SetPrivateData(WKPDID_D3DDebugObjectName, path.length(), path.data());
#endif

		return tex;
	}

	IResource* TextureLoader::LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options)
	{
		auto extension = relativePath.extension();

		auto gfxDevice = (ID3D11Device*)g_pEnv->_graphicsDevice->GetNativeDevice();
		auto gfxContexte = (ID3D11DeviceContext*)g_pEnv->_graphicsDevice->GetNativeDeviceContext();

		Texture2D* tex = new Texture2D;

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

			if (DirectX::LoadFromTGAMemory(
				data.data(),
				data.size(),
				&metaData,
				scratchImage) == S_OK)
			{
				DirectX::CreateTexture(
					gfxDevice,
					scratchImage.GetImages(),
					scratchImage.GetImageCount(),
					metaData,
					(ID3D11Resource**)&d3dTexture);

				DirectX::CreateShaderResourceView(
					gfxDevice,
					scratchImage.GetImages(),
					scratchImage.GetImageCount(),
					metaData,
					&d3dSRV);
			}
		}
		else
		{
			CHECK_HR(DirectX::CreateWICTextureFromMemory(
				gfxDevice,
				gfxContexte,
				data.data(),
				data.size(),
				(ID3D11Resource**)&d3dTexture,
				&d3dSRV));
		}

		g_pGraphics->Unlock();

		if (d3dTexture == nullptr)
		{
			delete tex;
			return nullptr;
		}

		D3D11_TEXTURE2D_DESC desc;
		d3dTexture->GetDesc(&desc);

		tex->_width = desc.Width;
		tex->_height = desc.Height;
		tex->_texture = d3dTexture;
		tex->_shaderResourceView = d3dSRV;

#ifdef _DEBUG
		std::string path = relativePath.string().c_str();
		((ID3D11Texture2D*)tex->GetNativePtr())->SetPrivateData(WKPDID_D3DDebugObjectName, path.length(), path.data());
#endif

		return tex;
	}

	void TextureLoader::UnloadResource(IResource* resource)
	{
		SAFE_DELETE(resource);
	}

	std::vector<std::string> TextureLoader::GetSupportedResourceExtensions()
	{
		return { ".png", ".jpg", ".jpeg", ".bmp", ".dds", ".tga", ".tif", ".psd"};
	}

	std::wstring TextureLoader::GetResourceDirectory() const
	{
		return L"Textures";
	}
}