
#pragma once

#include <HexEngine.Core\HexEngine.hpp>

namespace HexEditor
{
	class ProjectManager : public HexEngine::Dialog
	{
	public:
		using OnCompleted = std::function<void(const fs::path&, const std::string&, bool, const std::wstring&, HexEngine::LoadingDialog* loadingDlg)>;

		ProjectManager(Element* parent, const HexEngine::Point& position, const HexEngine::Point& size);
		~ProjectManager();

		static ProjectManager* CreateProjectManagerDialog(Element* parent, OnCompleted onCompletedAction);

		void ReadProjectList();
		void AddNewProjectPath(const fs::path& path);

		bool OnClickExistingProject(HexEngine::ListBox* box, HexEngine::ListBox::Item* item);

		virtual void Render(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h) override;
		virtual void PostRenderChildren(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h) override;

		//const fs::path& GetCurrentProjectPath() const { return _currentProjectPath; }
		//const fs::path& GetCurrentProjectFilePath() const { return _currentProjectFilePath; }

	private:
		bool OnBrowseFolderPath();
		bool OnCreateProject();

	private:
		// new
		HexEngine::GroupBox* _newProjectGroup;
		HexEngine::LineEdit* _projectName;
		HexEngine::LineEdit* _projectPath;
		HexEngine::LineEdit* _namespaceName;
		HexEngine::Button* _browsePathBtn;
		HexEngine::Button* _createProjectBtn;

		// existig
		HexEngine::GroupBox* _oldProjectsGroup;
		HexEngine::ListBox* _oldProjectsList;

		json _projectListData;

		//fs::path _currentProjectPath;
		//fs::path _currentProjectFilePath;

		OnCompleted _onCompleted;

		HexEngine::DrawList _drawList;

		//std::wstring _projectPath;
		//std::wstring _projectName;
	};
}
