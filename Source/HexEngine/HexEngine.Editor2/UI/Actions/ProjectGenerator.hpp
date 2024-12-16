
#pragma once

#include <HexEngine.Core\HexEngine.hpp>

namespace HexEditor
{
	struct ProjectGenerationParams
	{
		fs::path path;		
		fs::path sdkPath;
		fs::path primaryScenePath;
		std::string projectName;
		std::string guid;
		std::string nameSpace;		
	};

	class ProjectGenerator
	{
	public:
		bool Create(ProjectGenerationParams& params);

	private:
		bool CreateSolutionFile(ProjectGenerationParams& params);
		bool CreateProjectFile(ProjectGenerationParams& params);
		bool CreateMainHeaderFile(ProjectGenerationParams& params);
		bool CreateMainCppFile(ProjectGenerationParams& params);
		bool CreateMainFile(ProjectGenerationParams& params);
		bool CreateFiltersFile(ProjectGenerationParams& params);
		bool CreateShaderTargetsTemplate(ProjectGenerationParams& params);
		bool CreateShaderTemplate(ProjectGenerationParams& params);
		bool CreateUserTemplate(ProjectGenerationParams& params);
		void ReplaceKeyValues(const std::vector<std::pair<std::string, std::string>>& keyValues, std::string& text);
	};
}
