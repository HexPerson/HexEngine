
#include "AssetPackage.hpp"
#include "../Environment/LogFile.hpp"
#include "../Environment/IEnvironment.hpp"
#include "ResourceSystem.hpp"
#include "ICompressionProvider.hpp"

namespace HexEngine
{
	AssetPackage::AssetPackage(const std::wstring& fsName) :
		FileSystem(fsName)
	{

	}

	AssetPackage::~AssetPackage()
	{
		// Close the V2 file handle. _sourceFile.close() is safe when not open.
		if (_sourceFile.is_open())
			_sourceFile.close();
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

	void AssetPackage::RegisterTocEntry(const AssetTocEntry& entry)
	{
		// V2 ingest: AssetFile::Unpack hands us each TOC entry. We store
		// the metadata only - no asset bytes are read yet. The actual
		// bytes are streamed lazily in GetFileData via _sourceFile.
		if (_toc.find(entry.relativePath) != _toc.end())
		{
			LOG_WARN("AssetPackage already has a TOC entry for '%S', skipping", entry.relativePath);
			return;
		}
		_toc[entry.relativePath] = entry;
	}

	void AssetPackage::AdoptSourceFile(const fs::path& packagePath)
	{
		// V2 only: open the .pkg and keep the handle so subsequent
		// GetFileData calls can seek+read individual assets on demand.
		// The handle stays open for the lifetime of this AssetPackage.
		_sourcePath = packagePath;
		_sourceFile.open(packagePath, std::ios::in | std::ios::binary);
		if (!_sourceFile.is_open())
		{
			LOG_CRIT("AssetPackage failed to keep source file handle for V2 streaming: '%S'", packagePath.wstring().c_str());
		}
	}

	bool AssetPackage::DoesAbsolutePathExist(const fs::path& path) const
	{
		// Check both the V1 eager map and the V2 TOC. Only one will be
		// populated for a given package (depending on the file version),
		// so this lookup is O(log N) twice in the worst case - cheap.
		if (_assetMap.find(path) != _assetMap.end())
			return true;
		return _toc.find(path) != _toc.end();
	}

	bool AssetPackage::DoesRelativePathExist(const fs::path& path) const
	{
		if (_assetMap.find(path) != _assetMap.end())
			return true;
		return _toc.find(path) != _toc.end();
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
		// V1 (eager): bytes are sitting in _assetMap from mount-time decompress.
		// Just copy them out.
		if (auto it = _assetMap.find(absolutePath); it != _assetMap.end())
		{
			data = it->second;
			return;
		}

		// V2 (lazy): look up the TOC, seek into the open .pkg file handle,
		// read the asset's bytes, decompress if needed. The seek+read is
		// serialised under _readMutex because the shared ifstream isn't
		// thread-safe across seeks (a different thread could move the file
		// position mid-read otherwise).
		auto tocIt = _toc.find(absolutePath);
		if (tocIt == _toc.end())
			return;

		const AssetTocEntry& entry = tocIt->second;

		std::vector<uint8_t> raw(entry.compressedSize);
		{
			std::lock_guard lock(_readMutex);
			if (!_sourceFile.is_open())
			{
				LOG_CRIT("AssetPackage cannot stream '%S' - source file handle is closed", absolutePath.wstring().c_str());
				return;
			}

			_sourceFile.clear();
			_sourceFile.seekg(static_cast<std::streamoff>(entry.offset), std::ios::beg);
			_sourceFile.read(reinterpret_cast<char*>(raw.data()), entry.compressedSize);

			const auto bytesRead = _sourceFile.gcount();
			if (bytesRead != static_cast<std::streamsize>(entry.compressedSize))
			{
				LOG_CRIT("AssetPackage short-read on '%S': wanted %u, got %lld",
					absolutePath.wstring().c_str(),
					entry.compressedSize,
					static_cast<long long>(bytesRead));
				return;
			}
		}

		if (entry.isCompressed)
		{
			if (!g_pEnv->_compressionProvider->DecompressData(raw, data))
			{
				LOG_CRIT("AssetPackage failed to decompress asset '%S'", absolutePath.wstring().c_str());
				return;
			}
			if (data.size() != entry.uncompressedSize)
			{
				LOG_WARN("AssetPackage decompressed size mismatch for '%S': expected %u, got %zu",
					absolutePath.wstring().c_str(),
					entry.uncompressedSize,
					data.size());
			}
		}
		else
		{
			data = std::move(raw);
		}
	}

	const std::map<std::wstring, std::vector<uint8_t>>& AssetPackage::GetAssetMap() const
	{
		return _assetMap;
	}

	std::shared_ptr<AssetPackage> AssetPackage::Create(const fs::path& path, const std::wstring& fsName)
	{
		AssetPackageLoadOptions opts;
		opts.fsName = fsName;

		auto ret = dynamic_pointer_cast<AssetPackage>(g_pEnv->GetResourceSystem().LoadResource(path, &opts));

		if(ret)
			g_pEnv->GetResourceSystem().AddFileSystem(ret.get());

		return ret;
	}
}