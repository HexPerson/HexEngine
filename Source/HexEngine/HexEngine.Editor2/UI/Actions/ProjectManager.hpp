
#pragma once

#include <HexEngine.Core\HexEngine.hpp>

namespace HexEditor
{
	class ProjectManager : public Dialog
	{
	public:
		using OnCompleted = std::function<void(const fs::path&, const std::string&, bool, const std::wstring&, LoadingDialog* loadingDlg)>;

		ProjectManager(Element* parent, const Point& position, const Point& size);
		~ProjectManager();

		static ProjectManager* CreateProjectManagerDialog(Element* parent, OnCompleted onCompletedAction);

		void ReadProjectList();
		void AddNewProjectPath(const fs::path& path);

		bool OnClickExistingProject(ListBox* box, ListBox::Item* item);

		virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override;
		virtual void PostRenderChildren(GuiRenderer* renderer, uint32_t w, uint32_t h) override;

		//const fs::path& GetCurrentProjectPath() const { return _currentProjectPath; }
		//const fs::path& GetCurrentProjectFilePath() const { return _currentProjectFilePath; }

	private:
		bool OnBrowseFolderPath();
		bool OnCreateProject();

	private:
		// new
		GroupBox* _newProjectGroup;
		LineEdit* _projectName;
		LineEdit* _projectPath;
		LineEdit* _namespaceName;
		Button* _browsePathBtn;
		Button* _createProjectBtn;

		// existig
		GroupBox* _oldProjectsGroup;
		ListBox* _oldProjectsList;

		json _projectListData;

		//fs::path _currentProjectPath;
		//fs::path _currentProjectFilePath;

		OnCompleted _onCompleted;

		DrawList _drawList;

		//std::wstring _projectPath;
		//std::wstring _projectName;
	};
}
