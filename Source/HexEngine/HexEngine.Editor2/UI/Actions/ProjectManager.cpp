
#include "ProjectManager.hpp"
#include "../../Editor.hpp"
#include "../EditorUI.hpp"
#include <shlobj.h>

namespace HexEditor
{
	ProjectManager::ProjectManager(Element* parent, const Point& position, const Point& size) :
		Dialog(parent, position, size, L"Project Manager")
	{
		_newProjectGroup = new GroupBox(this, Point(10, 10), Point(780, 150), L"Create New");
		{
			_projectName = new LineEdit(_newProjectGroup, Point(10, 10), Point(600, 26), L"Project Name");
			_projectPath = new LineEdit(_newProjectGroup, Point(10, 50), Point(600, 26), L"Project Path");
			_namespaceName = new LineEdit(_newProjectGroup, Point(10, 90), Point(600, 26), L"Namespace");
			_browsePathBtn = new Button(_newProjectGroup, Point(630, 50), Point(130, 26), L"Browse...", std::bind(&ProjectManager::OnBrowseFolderPath, this));

			_projectName->SetLabelMinSize(140);
			_projectPath->SetLabelMinSize(140);
			_namespaceName->SetLabelMinSize(140);
			_projectPath->EnableInput(false);

			_createProjectBtn = new Button(_newProjectGroup, Point(630, 90), Point(130, 26), L"Create", std::bind(&ProjectManager::OnCreateProject, this));
		}

		_oldProjectsGroup = new GroupBox(this, Point(10, 180), Point(780, 260), L"Previous projects");
		{
			_oldProjectsList = new ListBox(_oldProjectsGroup, Point(10, 10), Point(600, 230));
			_oldProjectsList->OnClickItem = std::bind(&ProjectManager::OnClickExistingProject, this, std::placeholders::_1, std::placeholders::_2);
		}
	}

	ProjectManager::~ProjectManager()
	{
	}

	bool ProjectManager::OnClickExistingProject(ListBox* box, ListBox::Item* item)
	{
		fs::path projectPath(item->label);

		if (_onCompleted)
		{
			LoadingDialog* loadingDlg = new LoadingDialog(g_pUIManager->GetRootElement(), Point(g_pUIManager->GetWidth() / 2 - 220, g_pUIManager->GetHeight() / 2 - 60), Point(440, 120), L"Loading");

			std::thread thread([](const fs::path p, LoadingDialog* dlg, const std::wstring namesp, OnCompleted fn)
				{
					fn(p.parent_path(), p.filename().string(), true, namesp, dlg);
				},
				projectPath, loadingDlg, _namespaceName->GetValue(), _onCompleted);
			thread.detach();
		}

		DeleteMe();
		return true;
	}

	ProjectManager* ProjectManager::CreateProjectManagerDialog(Element* parent, OnCompleted onCompletedAction)
	{
		uint32_t width, height;
		g_pEnv->GetScreenSize(width, height);

		int32_t centrex = width >> 1;
		int32_t centrey = height >> 1;

		const int32_t sizex = 800;
		const int32_t sizey = 480;

		ProjectManager* pm = new ProjectManager(parent, Point(centrex - sizex / 2, centrey - sizey / 2), Point(sizex, sizey));

		pm->ReadProjectList();
		pm->BringToFront();
		pm->_onCompleted = onCompletedAction;

		return pm;
	}

	static int32_t CALLBACK BrowseCallbackProc(HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData)
	{
		if (uMsg == BFFM_INITIALIZED)
		{
			SendMessage(hwnd, BFFM_SETSELECTION, TRUE, lpData);
		}

		return 0;
	};

	void ProjectManager::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		//renderer->SetDrawList(&_drawList);

		Dialog::Render(renderer, w, h);
	}

	void ProjectManager::PostRenderChildren(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		renderer->ListDraw(&_drawList);
	}

	bool ProjectManager::OnBrowseFolderPath()
	{
		wchar_t baseDirectory[MAX_PATH];
		wcscpy_s(baseDirectory, g_pEnv->_fileSystem->GetBaseDirectory().wstring().c_str());

		BROWSEINFO bi = { 0 };
		bi.lpszTitle = L"Browse for folder...";
		bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
		bi.lpfn = BrowseCallbackProc;
		bi.lParam = (LPARAM)baseDirectory;

		LPITEMIDLIST pidl = SHBrowseForFolder(&bi);

		std::wstring path;

		if (pidl != 0)
		{
			wchar_t tempPath[MAX_PATH];
			//get the name of the folder and put it in path
			SHGetPathFromIDList(pidl, tempPath);

			//free memory used
			IMalloc* imalloc = 0;
			if (SUCCEEDED(SHGetMalloc(&imalloc)))
			{
				imalloc->Free(pidl);
				imalloc->Release();
			}

			path = tempPath;
		}
		else
			return true;

		if (path.length() == 0)
		{
			return true;
		}

		_projectPath->SetValue(path);		
		return true;
	}

	void ProjectManager::ReadProjectList()
	{
		DiskFile file(g_pEnv->_fileSystem->GetLocalAbsolutePath(L"Projects.json"), std::ios::in);

		if (file.Open())
		{
			std::string contents;
			file.ReadAll(contents);

			_projectListData = json::parse(contents);

			for (auto& project : _projectListData["projects"].items())
			//for(json::iterator it = _projectListData["projects"].begin(); it != _projectListData["projects"].end(); it++)
			{
				auto val = project.value();

				auto v = val.get<std::string>();

				_oldProjectsList->AddItem(std::wstring(v.begin(), v.end()), g_pEnv->_uiManager->GetRenderer()->_style.img_folder_closed.get());
			}

			file.Close();
		}
	}

	void ProjectManager::AddNewProjectPath(const fs::path& path)
	{
		for (auto& project : _projectListData["projects"].items())
		{
			auto val = project.value();

			auto v = val.get<std::string>();

			if (path == v)
				return;
		}

		_projectListData["projects"].push_back(path);

		// update the project file on disk
		DiskFile file(g_pEnv->_fileSystem->GetLocalAbsolutePath(L"Projects.json"), std::ios::out | std::ios::trunc);

		if (file.Open())
		{
			auto str = _projectListData.dump();

			file.Write(str.data(), str.length());
			file.Close();
		}
	}

	bool ProjectManager::OnCreateProject()
	{
		if (_projectPath->GetValue().length() == 0)
		{
			LOG_CRIT("Please select a valid project path before creating a project");
			return true;
		}

		if (_projectName->GetValue().length() == 0)
		{
			LOG_CRIT("Please select a valid project name before creating a project");
			return true;
		}

		fs::path projectFilePath = _projectPath->GetValue();
		fs::path projectFolder = projectFilePath;
		projectFilePath /= (_projectName->GetValue() + L".json");

#ifndef _DEBUG
		if (fs::exists(projectFilePath))
		{
			MessageBox(0, L"A Hex Engine project already exists at this location, please select another folder", L"Project Creation Error", MB_TOPMOST | MB_ICONASTERISK);
			return true;
		}
#endif

		g_pEditor->CreateFileSystem(_projectPath->GetValue());

		//auto* newScene = g_pEnv->_sceneManager->CreateEmptyScene(g_pUIManager);


		// Create the new scene file
		//
		//SceneSaveFile sceneFile(projectFolder / L"Data/Scenes/New Scene.scene", std::ios::out | std::ios::binary);
		//_currentProjectPath = projectFolder;

		//sceneFile.Save();
		//sceneFile.Close();

		//const auto absPath = sceneFile.GetAbsolutePath();

		//std::string sceneFilePath = std::string(absPath.begin(), absPath.end());

		//// Create the json project file
		//json projectData = {
		//	{"projectName", _projectName->GetValue().c_str()},
		//	{"editorVersion", HexEditorVersion},
		//	{"engineVersion", HexEngineVersion},
		//	{"scenes", {
		//		sceneFilePath
		//	}}
		//};


		//
		//DiskFile projectFile(projectFilePath, std::ios::out | std::ios::trunc);

		//if (projectFile.Open() == false)
		//{
		//	LOG_CRIT("Failed to create project file!");
		//	return true;
		//}

		//auto dumpData = projectData.dump();
		//projectFile.Write(dumpData.data(), dumpData.length());
		//projectFile.Close();

		//LOG_INFO("Project file successfully written to %S", projectFilePath.wstring().c_str());

		// add the project
		AddNewProjectPath(projectFilePath);		

		std::string projectName(_projectName->GetValue().begin(), _projectName->GetValue().end());

		if (_onCompleted)
		{
			_onCompleted(projectFolder, projectName + ".json", false, _namespaceName->GetValue(), nullptr);
		}

		DeleteMe();
		return true;
	}
}