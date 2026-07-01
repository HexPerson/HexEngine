
#include "PluginManifest.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>

namespace HexEngine
{
	namespace
	{
		std::string ToLower(std::string s)
		{
			std::transform(s.begin(), s.end(), s.begin(),
				[](unsigned char c) { return (char)std::tolower(c); });
			return s;
		}
	}

	bool ParsePluginManifest(const std::string& jsonText, PluginManifest& out, std::string& error)
	{
		out.entries.clear();
		error.clear();

		nlohmann::json j;
		try
		{
			j = nlohmann::json::parse(jsonText);
		}
		catch (const std::exception& e)
		{
			error = std::string("JSON parse error: ") + e.what();
			return false;
		}

		if (!j.is_object())
		{
			error = "manifest root must be a JSON object";
			return false;
		}

		const auto it = j.find("plugins");
		if (it == j.end() || !it->is_array())
		{
			error = "manifest must contain a 'plugins' array";
			return false;
		}

		for (const auto& e : *it)
		{
			if (!e.is_object())
			{
				error = "each entry in 'plugins' must be an object";
				return false;
			}

			PluginManifestEntry entry;
			entry.name    = e.value("name", std::string());
			entry.module  = e.value("module", std::string());
			entry.enabled = e.value("enabled", true);
			entry.sha256  = ToLower(e.value("sha256", std::string()));

			if (entry.module.empty())
			{
				error = "a plugin entry is missing its required 'module' field";
				return false;
			}

			out.entries.push_back(std::move(entry));
		}

		return true;
	}

	const PluginManifestEntry* FindPluginManifestEntry(const PluginManifest& manifest, const std::string& moduleFileName)
	{
		const std::string want = ToLower(moduleFileName);
		for (const auto& e : manifest.entries)
		{
			if (ToLower(e.module) == want)
				return &e;
		}
		return nullptr;
	}

	PluginLoadDecision EvaluatePluginLoad(
		const PluginManifest& manifest,
		PluginLoadPolicy policy,
		const std::string& moduleFileName,
		const std::string* actualSha256OrNull)
	{
		const PluginManifestEntry* entry = FindPluginManifestEntry(manifest, moduleFileName);

		if (entry == nullptr)
		{
			// Fail closed in production; permissive in developer.
			return (policy == PluginLoadPolicy::Production)
				? PluginLoadDecision::Reject_NotInManifest
				: PluginLoadDecision::Allow;
		}

		if (!entry->enabled)
			return PluginLoadDecision::Reject_Disabled;

		if (!entry->sha256.empty())
		{
			if (actualSha256OrNull == nullptr)
			{
				// Hash required but not computed: reject in production, skip in dev.
				if (policy == PluginLoadPolicy::Production)
					return PluginLoadDecision::Reject_HashUnverified;
			}
			else if (ToLower(*actualSha256OrNull) != entry->sha256)
			{
				// A definite mismatch is rejected in BOTH modes.
				return PluginLoadDecision::Reject_HashMismatch;
			}
		}

		return PluginLoadDecision::Allow;
	}

	const char* ToString(PluginLoadDecision decision)
	{
		switch (decision)
		{
		case PluginLoadDecision::Allow:                return "Allow";
		case PluginLoadDecision::Reject_NotInManifest: return "Reject_NotInManifest";
		case PluginLoadDecision::Reject_Disabled:      return "Reject_Disabled";
		case PluginLoadDecision::Reject_HashMismatch:  return "Reject_HashMismatch";
		case PluginLoadDecision::Reject_HashUnverified:return "Reject_HashUnverified";
		}
		return "Unknown";
	}
}
