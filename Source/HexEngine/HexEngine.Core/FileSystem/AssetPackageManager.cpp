
#include "AssetPackageManager.hpp"
#include "../Environment/IEnvironment.hpp"

namespace HexEngine
{
	AssetPackageManager::AssetPackageManager()
	{
		g_pEnv->_resourceSystem->RegisterResourceLoader(this);
	}

	AssetPackageManager::~AssetPackageManager()
	{
		g_pEnv->_resourceSystem->UnregisterResourceLoader(this);
	}

	IResource* AssetPackageManager::LoadResourceFromFile(const fs::path& absolutePath, FileSystem* fileSystem, const ResourceLoadOptions* options)
	{
		AssetFile assetFile(absolutePath);

		AssetPackage* package = new AssetPackage;		

		assetFile.Unpack(package);

		_loadedAssetPackages.push_back(package);

		return package;
	}

	IResource* AssetPackageManager::LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options)
	{
		return nullptr;
	}

	void AssetPackageManager::UnloadResource(IResource* resource)
	{
		_loadedAssetPackages.erase(std::remove(_loadedAssetPackages.begin(), _loadedAssetPackages.end(), resource), _loadedAssetPackages.end());

		SAFE_DELETE(resource);
	}

	std::vector<std::string> AssetPackageManager::GetSupportedResourceExtensions()
	{
		return { ".pkg" };
	}

	std::wstring AssetPackageManager::GetResourceDirectory() const
	{
		return L"AssetPackages";
	}

	const std::vector<AssetPackage*>& AssetPackageManager::GetLoadedAssetPackages() const
	{
		return _loadedAssetPackages;
	}

	AssetPackage* AssetPackageManager::FindLoadAssetPackageByName(const std::wstring& name) const
	{
		for (auto package : _loadedAssetPackages)
		{
			if (package->GetAbsolutePath().filename() == name)
				return package;
		}
		return nullptr;
	}
}
