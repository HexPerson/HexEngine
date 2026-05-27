
#include "AssetFile.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../Environment/LogFile.hpp"
#include "../FileSystem/ICompressionProvider.hpp"
#include "AssetPackage.hpp"

namespace HexEngine
{
	AssetFile::AssetFile(const fs::path& absolutePath) :
		DiskFile(absolutePath, std::ios::binary | std::ios::in)
	{}

	bool AssetFile::Unpack(std::shared_ptr<AssetPackage> package)
	{
		if (!Open())
			return false;

		// Peek the header so we can tell V1 from V2 before deciding how to
		// load. Both versions start with the same {version, numFiles}
		// prefix; the rest of the header differs and downstream parsing
		// branches accordingly.
		AssetFileHeader headerProbe = {};
		Read(&headerProbe, sizeof(AssetFileHeader));

		if (headerProbe.version < AssetFileHeader::MinSupportedVersion ||
			headerProbe.version > AssetFileHeader::AssetVersion)
		{
			LOG_CRIT("Asset package version unsupported. Min: %u, max: %u, given: %u",
				AssetFileHeader::MinSupportedVersion,
				AssetFileHeader::AssetVersion,
				headerProbe.version);
			Close();
			return false;
		}

		// -----------------------------------------------------------------
		// V1: legacy eager-load path. Whole package is (optionally) Brotli-
		// compressed as a single blob; we slurp the file, decompress it,
		// then walk the inlined AssetHeader+data blocks and copy each
		// asset's bytes into AssetPackage::_assetMap. Asset bytes stay
		// resident in memory for the package's entire lifetime.
		// -----------------------------------------------------------------
		if (headerProbe.version == 1)
		{
			// Re-read from start: ReadAll wants a stream cursor at offset 0.
			Close();
			if (!Open())
				return false;

			std::vector<uint8_t> data;
			ReadAll(data);
			Close();

			uint8_t* p = data.data();
			AssetFileHeader* header = reinterpret_cast<AssetFileHeader*>(p);

			if (header->compressed)
			{
				if (!g_pEnv->_compressionProvider->DecompressData(data, _decompressed))
					return false;
				p = _decompressed.data();
				header = reinterpret_cast<AssetFileHeader*>(p);
			}

			p += sizeof(AssetFileHeader);

			for (uint32_t i = 0; i < header->numFiles; ++i)
			{
				AssetHeader* fileHeader = reinterpret_cast<AssetHeader*>(p);
				LOG_INFO("Asset (v1): %S, size %u", fileHeader->relativePath, fileHeader->size);
				package->AddAsset(fileHeader);
				p += (sizeof(AssetHeader) + fileHeader->size);
			}

			return true;
		}

		// -----------------------------------------------------------------
		// V2: lazy streaming path. Parse only the TOC into the package; do
		// NOT read any asset bytes. The package keeps the file handle open
		// (via AdoptSourceFile) and reads individual assets on demand from
		// the offsets stored in the TOC. Per-file Brotli compression means
		// each asset is independently decodable so reloads don't trigger
		// re-decompressing the entire package.
		// -----------------------------------------------------------------
		LOG_INFO("Loading v2 asset package with %u files (lazy streaming)", headerProbe.numFiles);

		// Header is already consumed; TOC immediately follows.
		std::vector<AssetTocEntry> entries(headerProbe.numFiles);
		if (headerProbe.numFiles > 0)
		{
			const uint32_t expectedTocBytes = headerProbe.numFiles * sizeof(AssetTocEntry);
			if (headerProbe.tocSize != expectedTocBytes)
			{
				LOG_CRIT("V2 asset package TOC size mismatch: expected %u bytes, file says %u",
					expectedTocBytes, headerProbe.tocSize);
				Close();
				return false;
			}

			Read(entries.data(), expectedTocBytes);

			for (const auto& entry : entries)
			{
				LOG_INFO("Asset (v2): %S, %u bytes on disk%s",
					entry.relativePath,
					entry.compressedSize,
					entry.isCompressed ? " [brotli]" : "");
				package->RegisterTocEntry(entry);
			}
		}

		// Hand off the file handle to the package - AssetPackage owns its
		// own ifstream rather than reusing this one because AssetFile is
		// stack-scoped to Unpack(); the package needs a stream that lives
		// as long as it does. Close ours now (it was only used for the TOC
		// read) and let the package open its own keep-alive handle.
		Close();
		package->AdoptSourceFile(GetAbsolutePath());

		return true;
	}
}