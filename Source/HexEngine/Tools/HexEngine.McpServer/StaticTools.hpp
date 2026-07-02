
#pragma once

// Static (file/project) inspection tools for the HexEngine MCP server. These do
// NOT require a running editor - they read files under a repo/project root. All
// read-only: no writes, no deletes, no process execution. Each returns a plain
// JSON object (boring + explicit) suitable for wrapping in an MCP text result.
//
// The core logic is expressed as pure functions taking explicit inputs so it can
// be unit-tested without the MCP transport (see HexEngine.Tests).

#include <nlohmann/json.hpp>
#include <string>

namespace HexEngine
{
namespace Mcp
{
	using json = nlohmann::json;

	// Cap on bytes read from any single file (build logs etc.).
	inline constexpr size_t kMaxReadBytes = 512u * 1024u; // 512 KiB
	// Cap on number of files listed.
	inline constexpr size_t kMaxFileList  = 2000u;

	// --- Pure logic (unit-tested directly) --------------------------------------

	// Validate that the full nlohmann/json include tree is staged under
	// `includeDir` (which should be <repo>/Include). Checks json.hpp plus the
	// sibling headers json.hpp pulls in. Mirrors the bootstrap staging guarantee.
	json ValidateNlohmannStaging(const std::string& includeDir);

	// Parse MSBuild/cl.exe log text into structured diagnostics: counts + the
	// first N error/warning lines. Recognises `error C####`, `error MSB####`,
	// `error LNK####`, `fatal error`, and `warning C####`/`warning MSB####`.
	json ParseMsbuildLog(const std::string& logText, size_t maxSamples = 50);

	// --- Filesystem-backed tools ------------------------------------------------

	// Read a build/text log file (capped). `path` must resolve inside `root`.
	json ReadBuildLog(const std::string& root, const std::string& path);

	// Convenience: read a log then parse it.
	json ParseMsbuildLogFile(const std::string& root, const std::string& path);

	// Validate the dependency manifest layout: build/dependencies.lock.json parses,
	// every dependency has a name + git_url + ref, and (best effort) reports which
	// ThirdParty/<path> directories exist on disk.
	json ValidateDependencyLayout(const std::string& repoRoot);

	// Verify an asset package against its `.hashmanifest` sidecar (SHA-256 over the
	// whole .pkg). `pkgPath` must resolve inside `root`.
	json VerifyPackageManifest(const std::string& root, const std::string& pkgPath);

	// List files under `root`/`subdir` (capped, read-only). Optionally filter by a
	// comma-separated extension list (e.g. ".hmesh,.hmat").
	json ListProjectFiles(const std::string& root, const std::string& subdir, const std::string& extCsv);

	// List asset files (common HexEngine asset extensions) under `root`/`subdir`.
	json ListAssetFiles(const std::string& root, const std::string& subdir);
}
}
