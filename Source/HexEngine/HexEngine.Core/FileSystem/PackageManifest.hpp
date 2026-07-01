
#pragma once

#include <string>
#include <cstdint>

namespace HexEngine
{
	// Contents of a `.hashmanifest` sidecar written next to an asset package
	// (.pkg) by the AssetPacker and verified at mount by AssetFile. It carries a
	// SHA-256 over the WHOLE package file, so any tampering with the header, the
	// TOC, or any asset blob is detected in one check. Kept as a separate sidecar
	// (rather than embedded) so the package binary format is unchanged and older
	// packages still load - they simply have no manifest to verify against.
	//
	// Pure data + (de)serialisation, no engine deps, so it is unit-testable in
	// isolation (mirrors PluginManifest).
	struct PackageManifest
	{
		uint32_t    version = 1;         // manifest schema version
		std::string packageFileName;     // informational: the .pkg this describes
		std::string packageSha256;       // lowercase hex, 64 chars, over the whole .pkg
		uint64_t    packageSize = 0;     // bytes; a cheap pre-check before hashing
	};

	// Serialise to pretty JSON text.
	std::string SerializePackageManifest(const PackageManifest& manifest);

	// Parse JSON text. Returns false + a human-readable error on malformed input
	// or a missing/blank required field (version, packageSha256).
	bool ParsePackageManifest(const std::string& jsonText, PackageManifest& out, std::string& error);

	// Case-insensitive compare of two SHA-256 hex strings.
	bool HashesEqual(const std::string& a, const std::string& b);
}
