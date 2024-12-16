
#include <HexEngine.Core\HexEngine.hpp>
#include <HexEngine.Core\Environment\IEnvironment.hpp>
#include <HexEngine.Core\Environment\Game3DEnvironment.hpp>
#include <HexEngine.Core\Entity\Component\PointLight.hpp>
#include "Editor.hpp"
#include "UI\EditorUI.hpp"

IEnvironment* HexEngine::g_pEnv = nullptr;
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
		SAFE_UNLOAD_ARRAY(_overlayIcons, _countof(_overlayIcons));
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
			if (scene->GetComponents(1 << PointLight::_GetComponentId() | 1 << SpotLight::_GetComponentId(), lights))
			{
				for (auto& light : lights)
				{
					int32_t scrx, scry;
					if (g_pEnv->_inputSystem->GetWorldToScreenPosition(
						scene->GetMainCamera(),
						light->GetEntity()->GetPosition(),
						scrx, scry, 
						g_pUIManager->_centralDock->GetSize().x, g_pUIManager->_centralDock->GetSize().y) == false)
						continue;

					scrx += g_pUIManager->_centralDock->GetPosition().x;
					scry += g_pUIManager->_centralDock->GetPosition().y;

					const int32_t IconSize = 24;

					g_pEnv->_uiManager->GetRenderer()->FillTexturedQuad(_overlayIcons[Overlay_Light], scrx- IconSize/2, scry- IconSize/2, IconSize, IconSize, math::Color(1, 1, 1, 1));

					g_pEnv->_uiManager->GetRenderer()->PrintText(g_pUIManager->GetRenderer()->_style.font, (uint8_t)Style::FontSize::Tiny, scrx, scry + IconSize / 2 + 2, math::Color(1, 1, 1, 1), FontAlign::CentreLR, std::wstring(light->GetEntity()->GetName().begin(), light->GetEntity()->GetName().end()));
				}
			}
		}
	}

	void EditorExtension::OnResize(int32_t width, int32_t height)
	{
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

		g_pEnv->_resourceSystem->AddFileSystem(g_pEditor->_projectFS);

		g_pUIManager->GetExplorer()->SetProjectPath(path);
	}

	
}
