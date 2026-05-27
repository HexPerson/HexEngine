
#pragma once

#include "IResource.hpp"
#include "AssetFile.hpp"
#include "FileSystem.hpp"
#include <fstream>
#include <mutex>

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
		~AssetPackage();

		// V1 ingest path: AssetFile::Unpack walks the inlined AssetHeader+data
		// blocks and calls this per file. Bytes are copied into _assetMap and
		// stay resident for the package's lifetime (this is the eager model).
		void AddAsset(AssetHeader* file);

		// V2 ingest paths: AssetFile::Unpack hands AssetPackage the TOC + the
		// open file handle to keep. GetFileData reads on demand against this
		// handle, so individual assets aren't resident until requested AND
		// reloads after eviction don't pay the full decompress cost.
		void RegisterTocEntry(const AssetTocEntry& entry);
		void AdoptSourceFile(const fs::path& packagePath);

		virtual void Destroy();

		virtual bool DoesAbsolutePathExist(const fs::path& path) const;

		virtual bool DoesRelativePathExist(const fs::path& path) const;

		virtual bool IsAsset() const;

		virtual fs::path GetLocalAbsoluteDataPath(const fs::path& localPath);

		virtual void GetFileData(const fs::path& absolutePath, std::vector<uint8_t>& data) override;

		const std::map<std::wstring, std::vector<uint8_t>>& GetAssetMap() const;

		static std::shared_ptr<AssetPackage> Create(const fs::path& path, const std::wstring& fsName);

	private:
		// V1 storage: full eager copy of every asset's bytes.
		std::map<std::wstring, std::vector<uint8_t>> _assetMap;

		// V2 storage: TOC keyed by path; open file handle + mutex for
		// on-demand seek/read. _sourcePath is the on-disk .pkg location
		// kept for diagnostics + reopen-on-failure scenarios.
		std::map<std::wstring, AssetTocEntry> _toc;
		fs::path _sourcePath;
		mutable std::mutex _readMutex;     // serialises seek+read against the shared file handle
		mutable std::ifstream _sourceFile; // owned ifstream; closed in dtor
	};
}
