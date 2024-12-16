
#pragma once

#include "DiskFile.hpp"
#include "SceneSaveFile.hpp"

namespace HexEngine
{
	class ProjectFile : public DiskFile
	{
	public:
		ProjectFile(const fs::path& absolutePath, std::ios_base::openmode openMode);

		~ProjectFile();

		const int Version = 1;

		bool Load();

		bool Save();

		bool IsValidVersion();

	public:
		std::vector<SceneSaveFile*> _scenes;
		std::string _projectName;
		int32_t _engineVersion = 0;
		int32_t _editorVersion = 0;
	};
}
