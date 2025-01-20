
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

		virtual std::shared_ptr<IResource>				LoadResourceFromFile(const fs::path& absolutePath, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) override;
		virtual std::shared_ptr<IResource>				LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) override;
		virtual void									OnResourceChanged(std::shared_ptr<IResource> resource) override {}
		virtual void									UnloadResource(IResource* resource) override;
		virtual std::vector<std::string>				GetSupportedResourceExtensions() override;
		virtual std::wstring							GetResourceDirectory() const override;
		virtual void									SaveResource(IResource* resource, const fs::path& path) override {}
		const std::vector<std::weak_ptr<AssetPackage>>& GetLoadedAssetPackages() const;
		std::shared_ptr<AssetPackage>					FindLoadAssetPackageByName(const std::wstring& name) const;

	private:
		std::vector<std::weak_ptr<AssetPackage>> _loadedAssetPackages;
	};
}
