
#include "ProjectManager.hpp"
#include "../../Editor.hpp"
#include "../EditorUI.hpp"
#include <shlobj.h>

namespace HexEditor
{
	ProjectManager::ProjectManager(Element* parent, const HexEngine::Point& position, const HexEngine::Point& size) :
		Dialog(parent, position, size, L"Project Manager")
	{
		_newProjectGroup = new HexEngine::GroupBox(this, HexEngine::Point(10, 10), HexEngine::Point(780, 150), L"Create New");
		{
			_projectName = new HexEngine::LineEdit(_newProjectGroup, HexEngine::Point(10, 10), HexEngine::Point(600, 26), L"Project Name");
			_projectPath = new HexEngine::LineEdit(_newProjectGroup, HexEngine::Point(10, 50), HexEngine::Point(600, 26), L"Project Path");
			_namespaceName = new HexEngine::LineEdit(_newProjectGroup, HexEngine::Point(10, 90), HexEngine::Point(600, 26), L"Namespace");
			_browsePathBtn = new HexEngine::Button(_newProjectGroup, HexEngine::Point(630, 50), HexEngine::Point(130, 26), L"Browse...", std::bind(&ProjectManager::OnBrowseFolderPath, this));

			_projectName->SetLabelMinSize(140);
			_projectPath->SetLabelMinSize(140);
			_namespaceName->SetLabelMinSize(140);
			_projectPath->EnableInput(false);

			_createProjectBtn = new HexEngine::Button(_newProjectGroup, HexEngine::Point(630, 90), HexEngine::Point(130, 26), L"Create", std::bind(&ProjectManager::OnCreateProject, this));
		}

		_oldProjectsGroup = new HexEngine::GroupBox(this, HexEngine::Point(10, 180), HexEngine::Point(780, 260), L"Previous projects");
		{
			_oldProjectsList = new HexEngine::ListBox(_oldProjectsGroup, HexEngine::Point(10, 10), HexEngine::Point(600, 230));
			_oldProjectsList->OnClickItem = std::bind(&ProjectManager::OnClickExistingProject, this, std::placeholders::_1, std::placeholders::_2);
		}
	}

	ProjectManager::~ProjectManager()
	{
	}

	bool ProjectManager::OnClickExistingProject(HexEngine::ListBox* box, HexEngine::ListBox::Item* item)
	{
		fs::path projectPath(item->label);

		if (_onCompleted)
		{
			HexEngine::LoadingDialog* loadingDlg = new HexEngine::LoadingDialog(g_pUIManager->GetRootElement(), HexEngine::Point(g_pUIManager->GetWidth() / 2 - 220, g_pUIManager->GetHeight() / 2 - 60), HexEngine::Point(440, 120), L"Loading");

			std::thread thread([](const fs::path p, HexEngine::LoadingDialog* dlg, const std::wstring namesp, OnCompleted fn)
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
		HexEngine::g_pEnv->GetScreenSize(width, height);

		int32_t centrex = width >> 1;
		int32_t centrey = height >> 1;

		const int32_t sizex = 800;
		const int32_t sizey = 480;

		ProjectManager* pm = new ProjectManager(parent, HexEngine::Point(centrex - sizex / 2, centrey - sizey / 2), HexEngine::Point(sizex, sizey));

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

	void ProjectManager::Render(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		//renderer->SetDrawList(&_drawList);6

		Dialog::Render(renderer, w, h);
	}

	void ProjectManager::PostRenderChildren(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		renderer->ListDraw(&_drawList);
	}

	bool ProjectManager::OnBrowseFolderPath()
	{
		wchar_t baseDirectory[MAX_PATH];
		wcscpy_s(baseDirectory, HexEngine::g_pEnv->GetFileSystem().GetBaseDirectory().wstring().c_str());

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
		HexEngine::DiskFile file(HexEngine::g_pEnv->GetFileSystem().GetLocalAbsolutePath(L"Projects.json"), std::ios::in);

		if (file.Open())
		{
			std::string contents;
			file.ReadAll(contents);

			try
			{
				_projectListData = json::parse(contents);

				for (auto& project : _projectListData["projects"].items())
					//for(json::iterator it = _projectListData["projects"].begin(); it != _projectListData["projects"].end(); it++)
				{
					auto val = project.value();

					auto v = val.get<std::string>();

					_oldProjectsList->AddItem(std::wstring(v.begin(), v.end()), HexEngine::g_pEnv->GetUIManager().GetRenderer()->_style.img_folder_closed.get());
				}
			}
			catch (json::exception& e)
			{
				LOG_CRIT("Failed to read json file: %s", e.what());
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
		HexEngine::DiskFile file(HexEngine::g_pEnv->GetFileSystem().GetLocalAbsolutePath(L"Projects.json"), std::ios::out | std::ios::trunc);

		if (file.Open())
		{
			auto str = _projectListData.dump(2);

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