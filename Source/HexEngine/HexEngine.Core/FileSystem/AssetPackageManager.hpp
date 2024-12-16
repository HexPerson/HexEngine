
#pragma once

#include "ResourceSystem.hpp"
#include "AssetPackage.hpp"

namespace HexEngine
{
	class AssetPackageManager : public IResourceLoader
	{
	public:
		AssetPackageManager();
		~AssetPackageManager();

		virtual IResource*					LoadResourceFromFile(const fs::path& absolutePath, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) override;
		virtual IResource*					LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) override;
		virtual void						UnloadResource(IResource* resource) override;
		virtual std::vector<std::string>	GetSupportedResourceExtensions() override;
		virtual std::wstring				GetResourceDirectory() const override;
		virtual void						SaveResource(IResource* resource, const fs::path& path) override {}
		const std::vector<AssetPackage*>&	GetLoadedAssetPackages() const;
		AssetPackage*						FindLoadAssetPackageByName(const std::wstring& name) const;

	private:
		std::vector<AssetPackage*> _loadedAssetPackages;
	};
}
