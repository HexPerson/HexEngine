
#include "AssetFile.hpp"
#include "BinaryReader.hpp"
#include "PackageVerification.hpp"
#include "PackageManifest.hpp"
#include "../Utility/Sha256.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../Environment/LogFile.hpp"
#include "../FileSystem/ICompressionProvider.hpp"
#include "AssetPackage.hpp"

#include <fstream>
#include <iterator>

namespace HexEngine
{
	namespace
	{
		// Verify a package against its `.hashmanifest` sidecar (if present).
		// Returns true when it's safe to proceed - either the package hashed
		// clean, or no sidecar exists (unsigned packages still load, with a
		// warning). Returns false only when a manifest IS present but is broken
		// or its hash doesn't match (tampering) - the package must be refused.
		bool VerifyPackageIntegrity(const fs::path& packagePath)
		{
			fs::path sidecar = packagePath;
			sidecar += L".hashmanifest";

			if (!fs::exists(sidecar))
			{
				LOG_WARN("Asset package '%S' has no .hashmanifest sidecar - integrity NOT verified",
					packagePath.filename().wstring().c_str());
				return true;
			}

			std::ifstream in(sidecar, std::ios::in | std::ios::binary);
			if (!in.is_open())
			{
				LOG_CRIT("Failed to open hash manifest '%S'", sidecar.wstring().c_str());
				return false;
			}
			std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			in.close();

			PackageManifest manifest;
			std::string err;
			if (!ParsePackageManifest(text, manifest, err))
			{
				LOG_CRIT("Asset package hash manifest '%S' is invalid: %s", sidecar.wstring().c_str(), err.c_str());
				return false;
			}

			std::string actualHash, hashErr;
			if (!Sha256File(packagePath, actualHash, hashErr))
			{
				LOG_CRIT("Failed to hash asset package '%S': %s", packagePath.wstring().c_str(), hashErr.c_str());
				return false;
			}

			if (!HashesEqual(actualHash, manifest.packageSha256))
			{
				LOG_CRIT("Asset package '%S' FAILED integrity check (hash mismatch) - refusing to load",
					packagePath.filename().wstring().c_str());
				return false;
			}

			LOG_INFO("Asset package '%S' integrity verified (sha256 ok)", packagePath.filename().wstring().c_str());
			return true;
		}
	}

	AssetFile::AssetFile(const fs::path& absolutePath) :
		DiskFile(absolutePath, std::ios::binary | std::ios::in)
	{}

	bool AssetFile::Unpack(std::shared_ptr<AssetPackage> package)
	{
		if (!Open())
			return false;

		// A well-formed package is at least a full header. Anything smaller is
		// truncated/garbage - reject before we read a partial (uninitialised)
		// header off the stream.
		if (GetSize() < sizeof(AssetFileHeader))
		{
			LOG_CRIT("Asset package is smaller than its header (%u bytes) - truncated or not a package", GetSize());
			Close();
			return false;
		}

		// Integrity gate: if a hash manifest is shipped alongside the package,
		// the whole file must hash to the recorded SHA-256 before we parse a
		// single byte of it. Tampered packages are refused here.
		if (!VerifyPackageIntegrity(GetAbsolutePath()))
		{
			Close();
			return false;
		}

		// Peek the header so we can tell V1 from V2 before deciding how to
		// load. Both versions start with the same {version, numFiles}
		// prefix; the rest of the header differs and downstream parsing
		// branches accordingly.
		AssetFileHeader headerProbe = {};
		if (Read(&headerProbe, sizeof(AssetFileHeader)) != sizeof(AssetFileHeader))
		{
			LOG_CRIT("Failed to read asset package header");
			Close();
			return false;
		}

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
		//
		// Every block is validated against the bytes actually present: a
		// corrupt AssetHeader::size can no longer walk the cursor off the end
		// of the buffer (an out-of-bounds heap read in the original code).
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

			if (data.size() < sizeof(AssetFileHeader))
			{
				LOG_CRIT("V1 asset package truncated before header");
				return false;
			}

			// The header is read raw at the front; if the package was
			// whole-package compressed, the walked bytes live in the
			// decompressed buffer instead.
			const uint8_t* base = data.data();
			size_t         baseSize = data.size();
			AssetFileHeader* header = reinterpret_cast<AssetFileHeader*>(data.data());

			if (header->compressed)
			{
				if (!g_pEnv->_compressionProvider->DecompressData(data, _decompressed))
					return false;

				if (_decompressed.size() < sizeof(AssetFileHeader))
				{
					LOG_CRIT("V1 asset package decompressed payload is smaller than its header");
					return false;
				}

				base = _decompressed.data();
				baseSize = _decompressed.size();
				header = reinterpret_cast<AssetFileHeader*>(_decompressed.data());
			}

			const uint32_t numFiles = header->numFiles;

			BinaryReader r(base, baseSize);
			if (!r.Skip(sizeof(AssetFileHeader)))
			{
				LOG_CRIT("V1 asset package header overruns its payload");
				return false;
			}

			for (uint32_t i = 0; i < numFiles; ++i)
			{
				// Validate the AssetHeader is fully present...
				const uint8_t* headerPtr = r.ReadInPlace(sizeof(AssetHeader));
				if (!headerPtr)
				{
					LOG_CRIT("V1 asset package truncated: entry %u/%u header runs past end of payload", i, numFiles);
					return false;
				}

				AssetHeader* fileHeader = reinterpret_cast<AssetHeader*>(const_cast<uint8_t*>(headerPtr));

				// ...then that its inline data bytes are all present before
				// AddAsset copies them (it reads `size` bytes past the header).
				if (!r.Skip(fileHeader->size))
				{
					LOG_CRIT("V1 asset package corrupt: entry %u/%u claims %u data bytes past end of payload", i, numFiles, fileHeader->size);
					return false;
				}

				LOG_INFO("Asset (v1): %S, size %u", fileHeader->relativePath, fileHeader->size);
				package->AddAsset(fileHeader);
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
		if (headerProbe.numFiles > 0)
		{
			// 64-bit product so a large count can't wrap when sized against the
			// entry stride.
			const uint64_t expectedTocBytes = (uint64_t)headerProbe.numFiles * sizeof(AssetTocEntry);

			if (headerProbe.tocSize != expectedTocBytes)
			{
				LOG_CRIT("V2 asset package TOC size mismatch: expected %llu bytes, file says %u",
					(unsigned long long)expectedTocBytes, headerProbe.tocSize);
				Close();
				return false;
			}

			// The TOC must physically fit in the file after the header, else a
			// bogus numFiles would drive an over-read (and an over-allocation
			// of `entries`).
			const uint64_t bytesAfterHeader = (uint64_t)GetSize() - sizeof(AssetFileHeader);
			if (expectedTocBytes > bytesAfterHeader)
			{
				LOG_CRIT("V2 asset package TOC (%llu bytes) does not fit in the file (%llu bytes after header)",
					(unsigned long long)expectedTocBytes, (unsigned long long)bytesAfterHeader);
				Close();
				return false;
			}

			std::vector<AssetTocEntry> entries(headerProbe.numFiles);
			const uint32_t got = Read(entries.data(), (uint32_t)expectedTocBytes);
			if (got != expectedTocBytes)
			{
				LOG_CRIT("V2 asset package TOC truncated: read %u of %llu bytes", got, (unsigned long long)expectedTocBytes);
				Close();
				return false;
			}

			// Per-entry bounds check: each blob must lie wholly inside the data
			// section of the file. offset/compressedSize come straight off disk;
			// without this a corrupt TOC would drive GetFileData to seek out of
			// bounds or allocate a bogus (huge) buffer. Reject the WHOLE package
			// if any entry is inconsistent - a partially-valid TOC isn't safe to
			// stream from.
			const uint64_t dataStart = (uint64_t)sizeof(AssetFileHeader) + expectedTocBytes;
			const uint64_t fileSize  = (uint64_t)GetSize();
			for (const auto& entry : entries)
			{
				if (!IsPackageBlobInBounds(entry.offset, entry.compressedSize, entry.uncompressedSize,
					dataStart, fileSize, kDefaultMaxUncompressedAssetBytes))
				{
					LOG_CRIT("V2 asset package entry '%S' is out of bounds (offset %llu, size %u, uncompressed %u, file %llu) - refusing to load",
						entry.relativePath,
						(unsigned long long)entry.offset,
						entry.compressedSize,
						entry.uncompressedSize,
						(unsigned long long)fileSize);
					Close();
					return false;
				}
			}

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
