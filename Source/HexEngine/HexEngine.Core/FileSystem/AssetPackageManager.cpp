
#include "AssetPackageManager.hpp"
#include "../Environment/IEnvironment.hpp"

namespace HexEngine
{
	AssetPackageManager::AssetPackageManager()
	{
		g_pEnv->GetResourceSystem().RegisterResourceLoader(this);
	}

	AssetPackageManager::~AssetPackageManager()
	{
		g_pEnv->GetResourceSystem().UnregisterResourceLoader(this);
	}

	std::shared_ptr<IResource> AssetPackageManager::LoadResourceFromFile(const fs::path& absolutePath, FileSystem* fileSystem, const ResourceLoadOptions* options)
	{
		AssetFile assetFile(absolutePath);

		std::shared_ptr<AssetPackage> package = std::shared_ptr<AssetPackage>(new AssetPackage, ResourceDeleter());

		assetFile.Unpack(package);

		_loadedAssetPackages.push_back(package);

		return package;
	}

	std::shared_ptr<IResource> AssetPackageManager::LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options)
	{
		return nullptr;
	}

	void AssetPackageManager::UnloadResource(IResource* resource)
	{
		_loadedAssetPackages.erase(std::remove_if(_loadedAssetPackages.begin(), _loadedAssetPackages.end(),
			[resource](std::weak_ptr<IResource> res) {
				return res.lock().get() == resource;
			}), _loadedAssetPackages.end());

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

	const std::vector<std::weak_ptr<AssetPackage>>& AssetPackageManager::GetLoadedAssetPackages() const
	{
		return _loadedAssetPackages;
	}

	std::shared_ptr<AssetPackage> AssetPackageManager::FindLoadAssetPackageByName(const std::wstring& name) const
	{
		for (auto package : _loadedAssetPackages)
		{
			auto sp = package.lock();

			if (sp->GetAbsolutePath().filename() == name)
				return sp;
		}
		return nullptr;
	}
}
