
#pragma once

#include <string>
#include <vector>

namespace HexEngine
{
	// How strictly plugins are gated. Developer = permissive (current behaviour,
	// unlisted DLLs load with a warning). Production = FAIL CLOSED (only manifest-
	// listed, enabled, hash-verified plugins load; anything else is refused and
	// never even mapped via LoadLibrary).
	enum class PluginLoadPolicy
	{
		Developer,
		Production,
	};

	enum class PluginLoadDecision
	{
		Allow,
		Reject_NotInManifest, // production: DLL absent from the allowlist
		Reject_Disabled,      // manifest entry has "enabled": false
		Reject_HashMismatch,  // computed SHA-256 != the manifest's expected hash
		Reject_HashUnverified,// production: entry requires a hash but none was computed
	};

	struct PluginManifestEntry
	{
		std::string name;    // logical name (informational)
		std::string module;  // DLL file name, e.g. "HexEngine.PhysXPlugin.dll"
		bool        enabled = true;
		std::string sha256;  // optional expected lowercase-hex SHA-256 of the DLL
	};

	struct PluginManifest
	{
		std::vector<PluginManifestEntry> entries;
	};

	// Parse a manifest JSON document ({ "plugins": [ { name, module, enabled,
	// sha256 }, ... ] }). Returns false + a human-readable error on malformed
	// input. Pure (no engine dependencies) so it is directly unit-testable.
	bool ParsePluginManifest(const std::string& jsonText, PluginManifest& out, std::string& error);

	// Case-insensitive lookup of a manifest entry by DLL file name. nullptr if
	// not listed.
	const PluginManifestEntry* FindPluginManifestEntry(const PluginManifest& manifest, const std::string& moduleFileName);

	// The core load decision. `actualSha256OrNull` is the DLL's computed hash
	// (lowercase hex) or nullptr when the caller didn't compute one. Pure +
	// deterministic -> the security-critical path is fully unit-tested.
	PluginLoadDecision EvaluatePluginLoad(
		const PluginManifest& manifest,
		PluginLoadPolicy policy,
		const std::string& moduleFileName,
		const std::string* actualSha256OrNull);

	const char* ToString(PluginLoadDecision decision);
}
