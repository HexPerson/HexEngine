
#include "AssetPackage.hpp"
#include "../Environment/LogFile.hpp"
#include "../Environment/IEnvironment.hpp"
#include "ResourceSystem.hpp"

namespace HexEngine
{
	AssetPackage::AssetPackage() :
		FileSystem(GetAbsolutePath().filename().wstring())
	{
		g_pEnv->GetResourceSystem().AddFileSystem(this);
	}

	void AssetPackage::Destroy()
	{
		g_pEnv->GetResourceSystem().RemoveFileSystem(this);
	}

	void AssetPackage::AddAsset(AssetHeader* file)
	{
		auto it = _assetMap.find(file->relativePath);

		if (it != _assetMap.end())
		{
			LOG_WARN("AssetPackage already has an asset named '%S', skipping", file->relativePath);
			return;
		}

		uint8_t* data = (uint8_t*)file + sizeof(AssetHeader);

		auto& asset = _assetMap[file->relativePath];
		asset.insert(asset.end(), data, data + file->size);
	}

	bool AssetPackage::DoesAbsolutePathExist(const fs::path& path) const
	{
		return _assetMap.find(path) != _assetMap.end();
	}

	bool AssetPackage::DoesRelativePathExist(const fs::path& path) const
	{
		return _assetMap.find(path) != _assetMap.end();
	}

	bool AssetPackage::IsAsset() const
	{
		return true;
	}

	fs::path AssetPackage::GetLocalAbsoluteDataPath(const fs::path& localPath)
	{
		std::wstring wpath = localPath.wstring();
		std::replace(wpath.begin(), wpath.end(), '\\', '/');
		return wpath;
	}

	void AssetPackage::GetFileData(const fs::path& absolutePath, std::vector<uint8_t>& data)
	{
		auto it = _assetMap.find(absolutePath);

		if (it == _assetMap.end())
			return;

		data = it->second;
	}

	const std::map<std::wstring, std::vector<uint8_t>>& AssetPackage::GetAssetMap() const
	{
		return _assetMap;
	}
}