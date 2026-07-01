
#pragma once

#include <string>
#include <cstddef>
#include <filesystem>

namespace HexEngine
{
	// SHA-256 helpers backed by the Windows CNG (BCrypt) API - no extra
	// dependency (bcrypt.lib is auto-linked via a #pragma in the .cpp). Used for
	// plugin manifest hash verification (PR1) and reused for package/asset hashing
	// (PR7). Output is lowercase hex (64 chars).

	// Hash a contiguous buffer. Returns "" on failure.
	std::string Sha256Hex(const void* data, size_t size);

	// Hash a file's contents, streamed in bounded chunks (never loads the whole
	// file into memory). Returns false + a human-readable error on failure.
	bool Sha256File(const std::filesystem::path& path, std::string& outHex, std::string& error);
}
