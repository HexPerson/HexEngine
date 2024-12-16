
#include "ProjectFile.hpp"
#include "../Scene/Scene.hpp"
#include "../Environment/LogFile.hpp"

namespace HexEngine
{
	ProjectFile::ProjectFile(const fs::path& absolutePath, std::ios_base::openmode openMode) :
		DiskFile(absolutePath, openMode)
	{}

	ProjectFile::~ProjectFile()
	{
		for (auto& s : _scenes)
		{
			SAFE_DELETE(s);
		}
	}

	bool ProjectFile::Load()
	{
		if (!Open())
			return false;

		std::string data;
		ReadAll(data);

		json j = json::parse(data);

		_editorVersion = j["editorVersion"];
		_engineVersion = j["engineVersion"];		

		for (auto& s : j["scenes"].items())
		{
			auto sceneFile = GetAbsolutePath().parent_path() / s.value().get<std::string>();

			_scenes.push_back(new SceneSaveFile(sceneFile, std::ios::in, nullptr));
		}

		return IsValidVersion();
	}

	bool ProjectFile::Save()
	{
		if (!Open())
			return false;

		json projectData = {
			{"projectName", _projectName.c_str()},
			{"editorVersion", HexEditorVersion},
			{"engineVersion", HexEngineVersion},
			{"scenes", {}
			}
		};

		std::vector<std::string> sceneList;
		for (auto s : _scenes)
		{
			auto wpath = s->GetAbsolutePath();
			auto relativePath = fs::relative(wpath, this->_fsPathObj.parent_path()).string();

			projectData["scenes"].push_back(relativePath);
		}

		auto data = projectData.dump(2);

		Write(data.data(), (uint32_t)data.length());

		Close();

		return true;
	}

	bool ProjectFile::IsValidVersion()
	{
		if (_editorVersion != HexEditorVersion)
		{
			LOG_CRIT("Project file is using an unsupported version. Expected version = %d.%d, project version = %d.%d",
				GET_MAJOR_VERSION(HexEditorVersion), GET_MINOR_VERSION(HexEditorVersion),
				GET_MAJOR_VERSION(_editorVersion), GET_MINOR_VERSION(_editorVersion));
			return false;
		}

		if (_engineVersion != HexEngineVersion)
		{
			LOG_CRIT("Project was built using a different engine version. Expected version = %d.%d, project version = %d.%d",
				GET_MAJOR_VERSION(HexEngineVersion), GET_MINOR_VERSION(HexEngineVersion),
				GET_MAJOR_VERSION(_engineVersion), GET_MINOR_VERSION(_engineVersion));
			return false;
		}
		return true;
	}
}