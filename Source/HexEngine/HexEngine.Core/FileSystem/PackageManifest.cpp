
#include "PackageManifest.hpp"

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

	bool HashesEqual(const std::string& a, const std::string& b)
	{
		if (a.size() != b.size() || a.empty())
			return false;
		return ToLower(a) == ToLower(b);
	}

	std::string SerializePackageManifest(const PackageManifest& manifest)
	{
		nlohmann::json j;
		j["version"] = manifest.version;
		j["package"] = {
			{ "file",   manifest.packageFileName },
			{ "sha256", manifest.packageSha256 },
			{ "size",   manifest.packageSize },
		};
		return j.dump(2);
	}

	bool ParsePackageManifest(const std::string& jsonText, PackageManifest& out, std::string& error)
	{
		out = PackageManifest{};
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

		out.version = j.value("version", 0u);
		if (out.version == 0)
		{
			error = "manifest must contain a non-zero 'version'";
			return false;
		}

		const auto pkg = j.find("package");
		if (pkg == j.end() || !pkg->is_object())
		{
			error = "manifest must contain a 'package' object";
			return false;
		}

		out.packageFileName = pkg->value("file", std::string());
		out.packageSha256   = pkg->value("sha256", std::string());
		out.packageSize     = pkg->value("size", (uint64_t)0);

		if (out.packageSha256.size() != 64)
		{
			error = "package 'sha256' must be a 64-character hex string";
			return false;
		}

		return true;
	}
}
