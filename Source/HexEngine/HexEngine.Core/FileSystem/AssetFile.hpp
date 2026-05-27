
#pragma once

#include "DiskFile.hpp"

namespace HexEngine
{
	// V1 layout (legacy): the entire package is optionally Brotli-compressed
	// as a single stream and exploded into memory at mount. AssetHeader
	// describes one file inline with its data.
	struct AssetHeader
	{
		wchar_t relativePath[128];
		uint32_t size;
		//uint8_t data[1];
	};

	struct AssetFileHeader
	{
		// V1: whole-package compression. Everything decompressed at boot,
		//     all asset bytes resident in memory for the package's lifetime.
		// V2: per-file compression + offset table. AssetPackage keeps only
		//     the TOC + an open file handle; individual asset bytes are
		//     read on demand so reload after eviction doesn't re-decompress
		//     the entire package. Compression is per-file (no whole-package
		//     `compressed` bool any more) so individual files are
		//     independently decodable.
		static const uint32_t AssetVersion = 2;
		static const uint32_t MinSupportedVersion = 1;

		uint16_t version;
		uint16_t numFiles;
		bool compressed;        // V1 only: whole-package Brotli flag. Ignored under V2.
		uint8_t reserved[3];    // padding so V2 fields below sit on a 4-byte boundary
		uint32_t tocSize;       // V2 only: total bytes of the TOC immediately after this header
		                        //          (0 for V1, since V1 has no TOC)
	};

	// V2 only: one entry per packed asset, written contiguously right
	// after AssetFileHeader. The asset's actual bytes live at `offset`
	// from the start of the .pkg file.
	struct AssetTocEntry
	{
		wchar_t  relativePath[128];
		uint64_t offset;            // absolute byte offset from start of .pkg
		uint32_t compressedSize;    // bytes on disk (== uncompressedSize if !isCompressed)
		uint32_t uncompressedSize;  // bytes after Brotli decode
		uint8_t  isCompressed;      // 1 = Brotli, 0 = raw bytes verbatim
		uint8_t  reserved[7];       // 8-byte alignment of next entry
	};

	class AssetPackage;

	class HEX_API AssetFile : public DiskFile
	{
	public:
		AssetFile(const fs::path& absolutePath);

		bool Unpack(std::shared_ptr<AssetPackage> package);

	private:
		// V1-only buffer (whole-package decompressed bytes that the V1
		// reader walks). Unused under V2 since assets stream on demand.
		std::vector<uint8_t> _decompressed;
	};
}
