
#include "StaticTools.hpp"

#include "../../HexEngine.Core/Utility/Sha256.hpp"
#include "../../HexEngine.Core/FileSystem/PackageManifest.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace HexEngine
{
namespace Mcp
{
	namespace
	{
		// Resolve `rel` under `root`, rejecting anything that escapes root (path
		// traversal guard). On success `outAbs` is the resolved absolute path.
		bool ResolveInside(const std::string& root, const std::string& rel, fs::path& outAbs, std::string& err)
		{
			std::error_code ec;
			fs::path rootAbs = fs::weakly_canonical(fs::path(root), ec);
			if (ec) rootAbs = fs::absolute(fs::path(root));

			fs::path candidate = fs::path(rel);
			if (candidate.is_relative())
				candidate = rootAbs / candidate;
			candidate = fs::weakly_canonical(candidate, ec);
			if (ec) candidate = fs::absolute(fs::path(rel));

			// Reject escapes: candidate must be rootAbs or under it.
			const std::string r = rootAbs.lexically_normal().string();
			const std::string c = candidate.lexically_normal().string();
			if (c.rfind(r, 0) != 0)
			{
				err = "path resolves outside the allowed root";
				return false;
			}
			outAbs = candidate;
			return true;
		}

		std::string ToLower(std::string s)
		{
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
			return s;
		}

		std::vector<std::string> SplitCsv(const std::string& csv)
		{
			std::vector<std::string> out;
			std::string cur;
			std::stringstream ss(csv);
			while (std::getline(ss, cur, ','))
			{
				// trim
				size_t a = cur.find_first_not_of(" \t");
				size_t b = cur.find_last_not_of(" \t");
				if (a != std::string::npos)
					out.push_back(ToLower(cur.substr(a, b - a + 1)));
			}
			return out;
		}
	}

	json ValidateNlohmannStaging(const std::string& includeDir)
	{
		const char* required[] = { "json.hpp", "adl_serializer.hpp", "json_fwd.hpp" };
		json present = json::array();
		json missing = json::array();
		fs::path base = fs::path(includeDir) / "nlohmann";
		for (const char* h : required)
		{
			if (fs::is_regular_file(base / h))
				present.push_back(std::string("nlohmann/") + h);
			else
				missing.push_back(std::string("nlohmann/") + h);
		}
		const bool staged = missing.empty();
		return json{
			{"staged", staged},
			{"includeDir", includeDir},
			{"present", present},
			{"missing", missing},
			{"message", staged
				? "full nlohmann/json include tree is staged"
				: "nlohmann/json staging incomplete - run the dependency bootstrap"},
		};
	}

	json ParseMsbuildLog(const std::string& logText, size_t maxSamples)
	{
		size_t errors = 0, warnings = 0;
		json errorSamples = json::array();
		json warningSamples = json::array();

		std::stringstream ss(logText);
		std::string line;
		while (std::getline(ss, line))
		{
			const std::string low = ToLower(line);
			// Errors: "error C1234", "error MSB####", "error LNK####", "fatal error".
			const bool isError =
				low.find(": error ") != std::string::npos ||
				low.find("fatal error") != std::string::npos ||
				low.find(") : error") != std::string::npos;
			const bool isWarning = !isError && low.find(": warning ") != std::string::npos;

			if (isError)
			{
				++errors;
				if (errorSamples.size() < maxSamples)
					errorSamples.push_back(line);
			}
			else if (isWarning)
			{
				++warnings;
				if (warningSamples.size() < maxSamples)
					warningSamples.push_back(line);
			}
		}

		return json{
			{"errorCount", errors},
			{"warningCount", warnings},
			{"errors", errorSamples},
			{"warnings", warningSamples},
			{"ok", errors == 0},
		};
	}

	json ReadBuildLog(const std::string& root, const std::string& path)
	{
		fs::path abs;
		std::string err;
		if (!ResolveInside(root, path, abs, err))
			return json{ {"error", err} };
		if (!fs::is_regular_file(abs))
			return json{ {"error", "file not found: " + abs.string()} };

		std::ifstream in(abs, std::ios::binary);
		if (!in.is_open())
			return json{ {"error", "failed to open: " + abs.string()} };

		std::string text;
		text.resize(kMaxReadBytes);
		in.read(&text[0], (std::streamsize)kMaxReadBytes);
		const size_t got = (size_t)in.gcount();
		text.resize(got);
		const bool truncated = (fs::file_size(abs) > kMaxReadBytes);

		return json{
			{"path", abs.string()},
			{"bytes", got},
			{"truncated", truncated},
			{"text", text},
		};
	}

	json ParseMsbuildLogFile(const std::string& root, const std::string& path)
	{
		json read = ReadBuildLog(root, path);
		if (read.contains("error"))
			return read;
		json parsed = ParseMsbuildLog(read.value("text", std::string()));
		parsed["path"] = read.value("path", std::string());
		parsed["truncated"] = read.value("truncated", false);
		return parsed;
	}

	json ValidateDependencyLayout(const std::string& repoRoot)
	{
		fs::path manifestPath = fs::path(repoRoot) / "build" / "dependencies.lock.json";
		if (!fs::is_regular_file(manifestPath))
			return json{ {"error", "dependency manifest not found: " + manifestPath.string()} };

		std::ifstream in(manifestPath, std::ios::binary);
		std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

		json manifest;
		try { manifest = json::parse(text); }
		catch (const std::exception& e) { return json{ {"error", std::string("manifest JSON parse error: ") + e.what()} }; }

		json deps = json::array();
		size_t missingRefs = 0, missingDirs = 0;
		for (const auto& d : manifest.value("dependencies", json::array()))
		{
			const std::string name = d.value("name", std::string());
			const std::string url  = d.value("git_url", std::string());
			const std::string ref  = d.value("ref", std::string());
			const std::string relPath = d.value("path", std::string());
			const bool dirExists = !relPath.empty() && fs::is_directory(fs::path(repoRoot) / relPath);
			if (ref.empty()) ++missingRefs;
			if (!dirExists)  ++missingDirs;
			deps.push_back(json{
				{"name", name},
				{"hasGitUrl", !url.empty()},
				{"hasRef", !ref.empty()},
				{"path", relPath},
				{"cloned", dirExists},
			});
		}

		return json{
			{"manifest", manifestPath.string()},
			{"dependencyCount", deps.size()},
			{"missingRefs", missingRefs},
			{"notCloned", missingDirs},
			{"dependencies", deps},
			{"ok", missingRefs == 0},
		};
	}

	json VerifyPackageManifest(const std::string& root, const std::string& pkgPath)
	{
		fs::path pkg;
		std::string err;
		if (!ResolveInside(root, pkgPath, pkg, err))
			return json{ {"error", err} };
		if (!fs::is_regular_file(pkg))
			return json{ {"error", "package not found: " + pkg.string()} };

		fs::path sidecar = pkg;
		sidecar += ".hashmanifest";
		if (!fs::is_regular_file(sidecar))
			return json{ {"verified", false}, {"reason", "no .hashmanifest sidecar present"}, {"package", pkg.string()} };

		std::ifstream in(sidecar, std::ios::binary);
		std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

		PackageManifest manifest;
		std::string parseErr;
		if (!ParsePackageManifest(text, manifest, parseErr))
			return json{ {"verified", false}, {"reason", "invalid manifest: " + parseErr} };

		std::string actualHash, hashErr;
		if (!Sha256File(pkg, actualHash, hashErr))
			return json{ {"verified", false}, {"reason", "failed to hash package: " + hashErr} };

		const bool match = HashesEqual(actualHash, manifest.packageSha256);
		return json{
			{"verified", match},
			{"package", pkg.string()},
			{"expectedSha256", manifest.packageSha256},
			{"actualSha256", actualHash},
			{"manifestSize", manifest.packageSize},
			{"actualSize", (uint64_t)fs::file_size(pkg)},
		};
	}

	json ListProjectFiles(const std::string& root, const std::string& subdir, const std::string& extCsv)
	{
		fs::path base;
		std::string err;
		if (!ResolveInside(root, subdir.empty() ? std::string(".") : subdir, base, err))
			return json{ {"error", err} };
		if (!fs::is_directory(base))
			return json{ {"error", "not a directory: " + base.string()} };

		const std::vector<std::string> exts = SplitCsv(extCsv);
		json files = json::array();
		bool truncated = false;
		std::error_code ec;
		for (fs::recursive_directory_iterator it(base, fs::directory_options::skip_permission_denied, ec), end;
			it != end; it.increment(ec))
		{
			if (ec) break;
			if (!it->is_regular_file(ec)) continue;
			const fs::path& p = it->path();
			if (!exts.empty())
			{
				const std::string e = ToLower(p.extension().string());
				if (std::find(exts.begin(), exts.end(), e) == exts.end())
					continue;
			}
			if (files.size() >= kMaxFileList) { truncated = true; break; }
			files.push_back(fs::relative(p, base, ec).string());
		}

		return json{
			{"root", base.string()},
			{"count", files.size()},
			{"truncated", truncated},
			{"files", files},
		};
	}

	json ListAssetFiles(const std::string& root, const std::string& subdir)
	{
		// Common HexEngine asset extensions.
		return ListProjectFiles(root, subdir,
			".hmesh,.hmat,.hscene,.hprefab,.png,.jpg,.jpeg,.dds,.hcs,.pkg,.wav,.ogg,.ttf,.otf");
	}
}
}
