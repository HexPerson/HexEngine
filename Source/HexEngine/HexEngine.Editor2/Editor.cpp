
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
		_overlayIcons[Overlay_Light] = HexEngine::ITexture2D::Create("EngineData.Textures/UI/light_bulb.png");

		
	}

	void EditorExtension::OnGUI()
	{
		auto scene = HexEngine::g_pEnv->GetSceneManager().GetCurrentScene();

		if (scene)
		{
			std::vector<HexEngine::BaseComponent*> lights;
			//if (scene->GetComponents(1 << PointLight::_GetComponentId() | 1 << SpotLight::_GetComponentId(), lights))
			{
				for (auto& light : lights)
				{
					int32_t scrx, scry;
					if (HexEngine::g_pEnv->_inputSystem->GetWorldToScreenPosition(
						scene->GetMainCamera(),
						light->GetEntity()->GetPosition(),
						scrx, scry) == false)
						continue;

					//scrx += g_pUIManager->GetSceneView()->GetPosition().x;
					//scry += g_pUIManager->GetSceneView()->GetPosition().y;

					const int32_t IconSize = 24;

					HexEngine::g_pEnv->GetUIManager().GetRenderer()->FillTexturedQuad(_overlayIcons[Overlay_Light].get(), scrx- IconSize/2, scry- IconSize/2, IconSize, IconSize, math::Color(1, 1, 1, 1));

					HexEngine::g_pEnv->GetUIManager().GetRenderer()->PrintText(g_pUIManager->GetRenderer()->_style.font.get(), (uint8_t)HexEngine::Style::FontSize::Tiny, scrx, scry + IconSize / 2 + 2, math::Color(1, 1, 1, 1), HexEngine::FontAlign::CentreLR, std::wstring(light->GetEntity()->GetName().begin(), light->GetEntity()->GetName().end()));
				}
			}
		}
	}

	void EditorExtension::OnResize(int32_t width, int32_t height)
	{
		const auto sceneView = g_pUIManager->GetSceneView();

		HexEngine::g_pEnv->_inputSystem->SetInputViewport(
			sceneView->GetPosition().x,
			sceneView->GetPosition().y,
			sceneView->GetSize().x,
			sceneView->GetSize().y);
	}

	void EditorExtension::OnFileChangeEvent(const HexEngine::DirectoryWatchInfo& info, const HexEngine::FileChangeActionMap& actionMap)
	{
		std::map<HexEngine::IResourceLoader*, std::vector<fs::path>> addedFiles;

		for (auto& action : actionMap)
		{
			if (action.first == FILE_ACTION_ADDED)
			{
				for (auto& fileInfo : action.second)
				{
					HexEngine::IResourceLoader* resourceLoader = HexEngine::g_pEnv->GetResourceSystem().FindResourceLoaderForExtension(fileInfo.path.extension().string());

					if (resourceLoader)
					{
						if (resourceLoader->DoesSupportHotLoading() == false)
							continue;

						auto fileSystem = HexEngine::g_pEnv->GetResourceSystem().FindFileSystemByPath(fileInfo.path);

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
					HexEngine::g_pEnv->GetResourceSystem().LoadResource(path);
				}
			}
		}
	}

	void EditorExtension::CreateFileSystem(const fs::path& path)
	{
		// Create the filesystem for the new project
		if (_projectFS != nullptr)
		{
			HexEngine::g_pEnv->GetResourceSystem().RemoveFileSystem(_projectFS);
			delete g_pEditor->_projectFS;
		}

		g_pEditor->_projectFS = new HexEngine::FileSystem(L"GameData");
		g_pEditor->_projectFS->SetBaseDirectory(path);
		g_pEditor->_projectFS->CreateChangeNotifier(g_pEditor->_projectFS->GetDataDirectory(), std::bind(&EditorExtension::OnFileChangeEvent, this, std::placeholders::_1, std::placeholders::_2));

		HexEngine::g_pEnv->GetResourceSystem().AddFileSystem(g_pEditor->_projectFS);

		g_pUIManager->GetExplorer()->SetProjectPath(path);
	}

	
}
