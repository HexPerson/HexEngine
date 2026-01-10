
#include <HexEngine.Core\HexEngine.hpp>
#include <HexEngine.Core\Environment\IEnvironment.hpp>
#include <HexEngine.Core\Environment\Game3DEnvironment.hpp>
#include <HexEngine.Core\Entity\Component\PointLight.hpp>
#include "Editor.hpp"
#include "UI\EditorUI.hpp"

//HVar* HexEngine::g_hvars = nullptr;
//HCommand* HexEngine::g_commands = nullptr;
//int32_t HexEngine::g_numVars = 0;
//int32_t HexEngine::g_numCommands = 0;

namespace HexEditor
{
	EditorExtension::EditorExtension()
	{}

	EditorExtension::~EditorExtension()
	{
		SAFE_DELETE(_projectFS);
	}

	void EditorExtension::OnCreateGame()
	{
		_overlayIcons[Overlay_Light] = ITexture2D::Create("EngineData.Textures/UI/light_bulb.png");
	}

	void EditorExtension::OnGUI()
	{
		auto scene = g_pEnv->_sceneManager->GetCurrentScene();

		if (scene)
		{
			std::vector<BaseComponent*> lights;
			//if (scene->GetComponents(1 << PointLight::_GetComponentId() | 1 << SpotLight::_GetComponentId(), lights))
			{
				for (auto& light : lights)
				{
					int32_t scrx, scry;
					if (g_pEnv->_inputSystem->GetWorldToScreenPosition(
						scene->GetMainCamera(),
						light->GetEntity()->GetPosition(),
						scrx, scry, 
						g_pUIManager->GetSceneView()->GetSize().x, g_pUIManager->GetSceneView()->GetSize().y) == false)
						continue;

					scrx += g_pUIManager->GetSceneView()->GetPosition().x;
					scry += g_pUIManager->GetSceneView()->GetPosition().y;

					const int32_t IconSize = 24;

					g_pEnv->_uiManager->GetRenderer()->FillTexturedQuad(_overlayIcons[Overlay_Light].get(), scrx- IconSize/2, scry- IconSize/2, IconSize, IconSize, math::Color(1, 1, 1, 1));

					g_pEnv->_uiManager->GetRenderer()->PrintText(g_pUIManager->GetRenderer()->_style.font.get(), (uint8_t)Style::FontSize::Tiny, scrx, scry + IconSize / 2 + 2, math::Color(1, 1, 1, 1), FontAlign::CentreLR, std::wstring(light->GetEntity()->GetName().begin(), light->GetEntity()->GetName().end()));
				}
			}
		}
	}

	void EditorExtension::OnResize(int32_t width, int32_t height)
	{
	}

	void EditorExtension::OnFileChangeEvent(const DirectoryWatchInfo& info, const FileChangeActionMap& actionMap)
	{
		std::map<IResourceLoader*, std::vector<fs::path>> addedFiles;

		for (auto& action : actionMap)
		{
			if (action.first == FILE_ACTION_ADDED)
			{
				for (auto& fileInfo : action.second)
				{
					IResourceLoader* resourceLoader = g_pEnv->_resourceSystem->FindResourceLoaderForExtension(fileInfo.path.extension().string());

					if (resourceLoader)
					{
						if (resourceLoader->DoesSupportHotLoading() == false)
							continue;

						auto fileSystem = g_pEnv->_resourceSystem->FindFileSystemByPath(fileInfo.path);

						if (fileSystem)
						{
							std::wstring relative = (fileSystem->GetName() + L".") + fs::relative(fileInfo.path, fileSystem->GetDataDirectory()).wstring();

							addedFiles[resourceLoader].push_back(relative);
						}
					}
				}
			}
		}

		for (auto& added : addedFiles)
		{
			// if the editor dialog is null, we should just presume that no import options are needed and immediately load the resource
			//if (auto dlg = added.first->CreateEditorDialog(added.second); dlg == nullptr)
			{
				for (auto& path : added.second)
				{
					g_pEnv->_resourceSystem->LoadResource(path);
				}
			}
		}
	}

	void EditorExtension::CreateFileSystem(const fs::path& path)
	{
		// Create the filesystem for the new project
		if (_projectFS != nullptr)
		{
			g_pEnv->_resourceSystem->RemoveFileSystem(_projectFS);
			delete g_pEditor->_projectFS;
		}

		g_pEditor->_projectFS = new FileSystem(L"GameData");
		g_pEditor->_projectFS->SetBaseDirectory(path);
		g_pEditor->_projectFS->CreateChangeNotifier(g_pEditor->_projectFS->GetDataDirectory(), std::bind(&EditorExtension::OnFileChangeEvent, this, std::placeholders::_1, std::placeholders::_2));

		g_pEnv->_resourceSystem->AddFileSystem(g_pEditor->_projectFS);

		g_pUIManager->GetExplorer()->SetProjectPath(path);
	}

	
}
