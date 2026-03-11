
#include "ProjectGenerator.hpp"

namespace HexEditor
{
	bool ProjectGenerator::Create(ProjectGenerationParams& params)
	{
		LOG_DEBUG("Generating project in folder '%S' named '%s'", params.path.wstring().c_str(), params.projectName.c_str());

		// check if the solution exists, don't generate it if it does
		if (fs::exists(params.path / (params.projectName + ".sln")))
		{
			LOG_INFO("Skipping project file generation because it already exists on disk");
			return true;
		}

		if (fs::exists(params.path) == false)
		{
			fs::create_directory(params.path);
		}

		if (!CreateSolutionFile(params))
		{
			LOG_CRIT("Solution file generation filed");
			return false;
		}

		if (!CreateProjectFile(params))
		{
			LOG_CRIT("Project file generation failed");
			return false;
		}

		CreateMainHeaderFile(params);
		CreateMainCppFile(params);
		CreateMainFile(params);
		CreateFiltersFile(params);
		CreateShaderTargetsTemplate(params);
		CreateShaderTemplate(params);
		CreateUserTemplate(params);

		// copy the assets
		auto assetFolder = params.path.parent_path() / "Build/Data/AssetPackages";
		if (!fs::exists(assetFolder))
		{
			fs::create_directories(assetFolder);
		}
		fs::copy(HexEngine::g_pEnv->GetFileSystem().GetLocalAbsoluteDataPath("AssetPackages/StandardAssets.pkg"), assetFolder / "StandardAssets.pkg", fs::copy_options::recursive | fs::copy_options::overwrite_existing);

		return true;
	}

	bool ProjectGenerator::CreateSolutionFile(ProjectGenerationParams& params)
	{
		// Open up the solution template file
		HexEngine::DiskFile slnTemplate(HexEngine::g_pEnv->GetFileSystem().GetLocalAbsoluteDataPath("Templates/SolutionTemplate.txt"), std::ios::in | std::ios::binary);

		if (slnTemplate.Open() == false)
		{
			LOG_CRIT("Unable to open solution template file");
			return false;
		}

		std::string text;
		slnTemplate.ReadAll(text);

		if (params.guid.length() == 0)
		{
			GUID gidReference;
			HRESULT hCreateGuid = CoCreateGuid(&gidReference);

			OLECHAR guidString[64];
			StringFromGUID2(gidReference, guidString, _countof(guidString));

			std::wstring guidStringWide(guidString);
			std::string guidStringAscii(guidStringWide.begin(), guidStringWide.end());

			params.guid = guidStringAscii;
		}

		const std::vector<std::pair<std::string, std::string>> keys = {
			{"<$NAME>", params.projectName},
			{"<$GUID>", params.guid}
		};

		ReplaceKeyValues(keys, text);

		HexEngine::DiskFile slnFile(params.path / (params.projectName + ".sln"), std::ios::out | std::ios::binary);

		if (!slnFile.Open())
		{
			LOG_CRIT("Failed to open new solution file for writing");
			return false;
		}

		slnFile.Write(text.data(), text.length());
		slnFile.Close();

		return true;
	}

	bool ProjectGenerator::CreateProjectFile(ProjectGenerationParams& params)
	{
		fs::path projectFolder = params.path / params.projectName;

		if (!fs::exists(projectFolder))
		{
			fs::create_directory(projectFolder);
		}

		// Open up the solution template file
		HexEngine::DiskFile slnTemplate(HexEngine::g_pEnv->GetFileSystem().GetLocalAbsoluteDataPath("Templates/ProjectTemplate.txt"), std::ios::in | std::ios::binary);

		if (slnTemplate.Open() == false)
		{
			LOG_CRIT("Unable to open solution template file");
			return false;
		}

		std::string text;
		slnTemplate.ReadAll(text);

		const std::vector<std::pair<std::string, std::string>> keys = {
			{"<$NAME>", params.projectName},
			{"<$GUID>", params.guid},
			{"<$SDKPATH>", params.sdkPath.string()}
		};

		ReplaceKeyValues(keys, text);

		HexEngine::DiskFile projFile(projectFolder / (params.projectName + ".vcxproj"), std::ios::out | std::ios::binary);

		if (!projFile.Open())
		{
			LOG_CRIT("Failed to open new solution file for writing");
			return false;
		}

		projFile.Write(text.data(), text.length());
		projFile.Close();

		return true;
	}

	bool ProjectGenerator::CreateMainHeaderFile(ProjectGenerationParams& params)
	{
		fs::path projectFolder = params.path / params.projectName;

		// Open up the solution template file
		HexEngine::DiskFile slnTemplate(HexEngine::g_pEnv->GetFileSystem().GetLocalAbsoluteDataPath("Templates/HeaderTemplate.txt"), std::ios::in | std::ios::binary);

		if (slnTemplate.Open() == false)
		{
			LOG_CRIT("Unable to open solution template file");
			return false;
		}

		std::string text;
		slnTemplate.ReadAll(text);

		const std::vector<std::pair<std::string, std::string>> keys = {
			{"<$NAME>", params.projectName},
			{"<$NAMESPACE>", params.nameSpace}
		};

		ReplaceKeyValues(keys, text);

		HexEngine::DiskFile headerFile(projectFolder / (params.projectName + ".hpp"), std::ios::out | std::ios::binary);

		if (!headerFile.Open())
		{
			LOG_CRIT("Failed to open new solution file for writing");
			return false;
		}

		headerFile.Write(text.data(), text.length());
		headerFile.Close();

		return true;
	}

	bool ProjectGenerator::CreateMainCppFile(ProjectGenerationParams& params)
	{
		fs::path projectFolder = params.path / params.projectName;

		// Open up the solution template file
		HexEngine::DiskFile slnTemplate(HexEngine::g_pEnv->GetFileSystem().GetLocalAbsoluteDataPath("Templates/CppTemplate.txt"), std::ios::in | std::ios::binary);

		if (slnTemplate.Open() == false)
		{
			LOG_CRIT("Unable to open solution template file");
			return false;
		}

		std::string text;
		slnTemplate.ReadAll(text);

		const std::vector<std::pair<std::string, std::string>> keys = {
			{"<$NAME>", params.projectName},
			{"<$NAMESPACE>", params.nameSpace},
			{"<$SCENEPATH>", params.primaryScenePath.string()}
		};

		ReplaceKeyValues(keys, text);

		HexEngine::DiskFile headerFile(projectFolder / (params.projectName + ".cpp"), std::ios::out | std::ios::binary);

		if (!headerFile.Open())
		{
			LOG_CRIT("Failed to open new solution file for writing");
			return false;
		}

		headerFile.Write(text.data(), text.length());
		headerFile.Close();

		return true;
	}

	bool ProjectGenerator::CreateMainFile(ProjectGenerationParams& params)
	{
		fs::path projectFolder = params.path / params.projectName;

		// Open up the solution template file
		HexEngine::DiskFile slnTemplate(HexEngine::g_pEnv->GetFileSystem().GetLocalAbsoluteDataPath("Templates/MainCppTemplate.txt"), std::ios::in | std::ios::binary);

		if (slnTemplate.Open() == false)
		{
			LOG_CRIT("Unable to open solution template file");
			return false;
		}

		std::string text;
		slnTemplate.ReadAll(text);

		const std::vector<std::pair<std::string, std::string>> keys = {
			{"<$NAME>", params.projectName},
			{"<$NAMESPACE>", params.nameSpace},
		};

		ReplaceKeyValues(keys, text);

		HexEngine::DiskFile headerFile(projectFolder / ("Main.cpp"), std::ios::out | std::ios::binary);

		if (!headerFile.Open())
		{
			LOG_CRIT("Failed to open new solution file for writing");
			return false;
		}

		headerFile.Write(text.data(), text.length());
		headerFile.Close();

		return true;
	}

	bool ProjectGenerator::CreateFiltersFile(ProjectGenerationParams& params)
	{
		fs::path projectFolder = params.path / params.projectName;

		// Open up the solution template file
		HexEngine::DiskFile slnTemplate(HexEngine::g_pEnv->GetFileSystem().GetLocalAbsoluteDataPath("Templates/FiltersTemplate.txt"), std::ios::in | std::ios::binary);

		if (slnTemplate.Open() == false)
		{
			LOG_CRIT("Unable to open solution template file");
			return false;
		}

		std::string text;
		slnTemplate.ReadAll(text);

		const std::vector<std::pair<std::string, std::string>> keys = {
			{"<$NAME>", params.projectName}
		};

		ReplaceKeyValues(keys, text);

		HexEngine::DiskFile headerFile(projectFolder / (params.projectName + ".vcxproj.filters"), std::ios::out | std::ios::binary);

		if (!headerFile.Open())
		{
			LOG_CRIT("Failed to open new solution file for writing");
			return false;
		}

		headerFile.Write(text.data(), text.length());
		headerFile.Close();

		return true;
	}

	bool ProjectGenerator::CreateShaderTargetsTemplate(ProjectGenerationParams& params)
	{
		fs::path projectFolder = params.path / params.projectName;

		// Open up the solution template file
		HexEngine::DiskFile slnTemplate(HexEngine::g_pEnv->GetFileSystem().GetLocalAbsoluteDataPath("Templates/ShaderTargetsTemplate.txt"), std::ios::in | std::ios::binary);

		if (slnTemplate.Open() == false)
		{
			LOG_CRIT("Unable to open solution template file");
			return false;
		}

		std::string text;
		slnTemplate.ReadAll(text);

		const std::vector<std::pair<std::string, std::string>> keys = {
			{"<$NAME>", params.projectName},
			{"<$SDKPATH>", params.sdkPath.string()}
		};

		ReplaceKeyValues(keys, text);

		HexEngine::DiskFile headerFile(projectFolder / "HexEngine.Shaders.targets", std::ios::out | std::ios::binary);

		if (!headerFile.Open())
		{
			LOG_CRIT("Failed to open new solution file for writing");
			return false;
		}

		headerFile.Write(text.data(), text.length());
		headerFile.Close();

		return true;
	}

	bool ProjectGenerator::CreateShaderTemplate(ProjectGenerationParams& params)
	{
		fs::path projectFolder = params.path / params.projectName;

		// Open up the solution template file
		HexEngine::DiskFile slnTemplate(HexEngine::g_pEnv->GetFileSystem().GetLocalAbsoluteDataPath("Templates/ShaderTemplate.txt"), std::ios::in | std::ios::binary);

		if (slnTemplate.Open() == false)
		{
			LOG_CRIT("Unable to open solution template file");
			return false;
		}

		std::string text;
		slnTemplate.ReadAll(text);

		HexEngine::DiskFile headerFile(projectFolder / "shader.xml", std::ios::out | std::ios::binary);

		if (!headerFile.Open())
		{
			LOG_CRIT("Failed to open new solution file for writing");
			return false;
		}

		headerFile.Write(text.data(), text.length());
		headerFile.Close();

		return true;
	}

	bool ProjectGenerator::CreateUserTemplate(ProjectGenerationParams& params)
	{
		fs::path projectFolder = params.path / params.projectName;

		// Open up the solution template file
		HexEngine::DiskFile slnTemplate(HexEngine::g_pEnv->GetFileSystem().GetLocalAbsoluteDataPath("Templates/UserTemplate.txt"), std::ios::in | std::ios::binary);

		if (slnTemplate.Open() == false)
		{
			LOG_CRIT("Unable to open solution template file");
			return false;
		}

		std::string text;
		slnTemplate.ReadAll(text);

		const std::vector<std::pair<std::string, std::string>> keys = {
			{"<$NAME>", params.projectName},
		};

		ReplaceKeyValues(keys, text);

		HexEngine::DiskFile headerFile(projectFolder / (params.projectName + ".vcxproj.user"), std::ios::out | std::ios::binary);

		if (!headerFile.Open())
		{
			LOG_CRIT("Failed to open new solution file for writing");
			return false;
		}

		headerFile.Write(text.data(), text.length());
		headerFile.Close();

		return true;
	}

	void ProjectGenerator::ReplaceKeyValues(const std::vector<std::pair<std::string, std::string>>& keyValues, std::string& text)
	{
		for (auto& k : keyValues)
		{
			std::string::size_type p = 0;

			while ((p = text.find(k.first, p)) != std::string::npos)
			{
				text.replace(p, k.first.size(), k.second);
				p += k.second.size();
			}
		}
	}
}