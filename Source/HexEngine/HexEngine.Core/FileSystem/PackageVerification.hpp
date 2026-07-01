
#pragma once

#include <cstdint>

namespace HexEngine
{
	// Bounds-check one V2 asset-package TOC entry's data blob against the
	// package layout, BEFORE the streaming reader seeks to `offset` and reads
	// `compressedSize` bytes. The offset/size come straight off disk and are
	// attacker-controlled, so without this a corrupt TOC could seek out of
	// bounds or drive a multi-gigabyte allocation.
	//
	// Layout invariants a valid entry must satisfy:
	//   - dataStart <= offset            (blob lives in the data section, not
	//                                      overlapping the header or the TOC)
	//   - offset <= fileSize             (blob starts inside the file)
	//   - offset + compressedSize <= fileSize   (blob ends inside the file;
	//                                      written as a subtraction so it can't
	//                                      overflow)
	//   - uncompressedSize <= maxUncompressedBytes  (decompression-bomb guard on
	//                                      the CLAIMED decoded size)
	//
	// Pure and dependency-free (only <cstdint>) so it can be unit-tested in
	// isolation and shared by AssetFile (load-time validation) and any streaming
	// re-check. All arithmetic is 64-bit.
	inline bool IsPackageBlobInBounds(uint64_t offset,
		uint64_t compressedSize,
		uint64_t uncompressedSize,
		uint64_t dataStart,
		uint64_t fileSize,
		uint64_t maxUncompressedBytes)
	{
		if (offset < dataStart)
			return false;
		if (offset > fileSize)
			return false;
		// offset <= fileSize is guaranteed here, so (fileSize - offset) can't
		// wrap; comparing against it avoids an (offset + compressedSize) overflow.
		if (compressedSize > fileSize - offset)
			return false;
		if (uncompressedSize > maxUncompressedBytes)
			return false;
		return true;
	}

	// Default cap on a single asset's decoded size. Generous enough for any
	// realistic packed asset, small enough to refuse an absurd decompression
	// bomb claim. Callers may pass a tighter bound.
	static constexpr uint64_t kDefaultMaxUncompressedAssetBytes = 1ull * 1024ull * 1024ull * 1024ull; // 1 GiB
}
