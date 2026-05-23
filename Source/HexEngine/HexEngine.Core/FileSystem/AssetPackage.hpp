
#pragma once

#include "IResource.hpp"
#include "AssetFile.hpp"
#include "FileSystem.hpp"

namespace HexEngine
{
	struct AssetPackageLoadOptions : ResourceLoadOptions
	{
		std::wstring fsName;
	};

	class HEX_API AssetPackage : public IResource, public FileSystem
	{
	public:
		AssetPackage(const std::wstring& fsName);

		void AddAsset(AssetHeader* file);

		virtual void Destroy();

		virtual bool DoesAbsolutePathExist(const fs::path& path) const;

		virtual bool DoesRelativePathExist(const fs::path& path) const;

		virtual bool IsAsset() const;

		virtual fs::path GetLocalAbsoluteDataPath(const fs::path& localPath);

		virtual void GetFileData(const fs::path& absolutePath, std::vector<uint8_t>& data) override;

		const std::map<std::wstring, std::vector<uint8_t>>& GetAssetMap() const;

		static std::shared_ptr<AssetPackage> Create(const fs::path& path, const std::wstring& fsName);

	private:
		std::map<std::wstring, std::vector<uint8_t>> _assetMap;
	};
}
