

#include "Game.hpp"
#include "../HexEngine.Core/Environment/IEnvironment.hpp"

#include "../HexEngine.Core/Entity/Camera.hpp"
#include "../HexEngine.Core/Entity/Sphere.hpp"
#include "../HexEngine.Core/Entity/Component/Transform.hpp"
#include "../HexEngine.Core/Entity/Component/FirstPersonCameraController.hpp"
#include "../HexEngine.Core/Entity/Component/RTSCameraController.hpp"
#include "../HexEngine.Core/Entity/Component/MeshRenderer.hpp"
#include "SaveFile.hpp"

namespace CityBuilder
{
	Game* g_pGame = nullptr;

	void Game::OnCreateGame()
	{
		// Create a new scene
		//
		_gameScene = g_pEnv->_sceneManager->CreateEmptyScene();

		_playerController = new PlayerController;
		_gameScene->AddEntity(_playerController);

		// Add the camera controller so we can move around
		//
		auto cameraController = _gameScene->GetMainCamera()->AddComponent<FirstPersonCameraController>();		
		//cameraController->SetWorldConstrainedArea(dx::BoundingBox(math::Vector3(), math::Vector3(1300.0f, 1300.0f, 1300.0f)));

		World::Create(2, 32, 32.0f);

		g_pEnv->_debugGui->AddCallback(this);

		//LoadLevel("MySaveFile");
	}

	void Game::OnGui()
	{
	}

	void Game::OnUpdate(float frameTime)
	{
		
	}

	void Game::OnFixedUpdate(float frameTime)
	{
		//_input->Update(frameTime);

		if (_gameScene->GetLights().size() > 0)
			_dayCycle.Update(frameTime, (DirectionalLight*)_gameScene->GetLights().at(0));
	}

	void Game::OnDebugGUI()
	{
		if (ImGui::BeginMainMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				if (ImGui::MenuItem("Save", "CTRL+S"))
				{
					SaveLevel("Save");
				}
				if (ImGui::MenuItem("Load", "CTRL+L"))
				{
					LoadLevel("Save");
				}
				if (ImGui::MenuItem("Close", "ESC"))
				{
					g_pEnv->OnRecieveQuitMessage();
				}
				ImGui::Separator();
				ImGui::EndMenu();
			}

			ImGui::EndMainMenuBar();
		}
	}

	

	void Game::OnDebugRender()
	{
	}

	void Game::OnShutdown()
	{
		// remove the world first
		g_pEnv->_sceneManager->GetCurrentScene()->RemoveEntity(_world);
		SAFE_DELETE(_world);

		if(g_pEnv->_sceneManager && _gameScene)
		g_pEnv->_sceneManager->UnloadScene(_gameScene);

		SAFE_DELETE(_input);
	}

	Scene* Game::GetGameScene()
	{
		return _gameScene;
	}

	void Game::SaveLevel(const fs::path& filename)
	{
		fs::path localPath = "SaveData/";
		localPath += filename;
		localPath += ".hsf";

		auto fullPath = g_pEnv->_fileSystem->GetLocalAbsolutePath(localPath);

		SaveFile save(fullPath, std::ios::out | std::ios::trunc | std::ios::binary);

		if (!save.Save())
		{
			LOG_CRIT("Saving failed!");
			return;
		}
	}

	void Game::LoadLevel(const fs::path& filename)
	{
		fs::path localPath = "SaveData/";
		localPath += filename;
		localPath += ".hsf";

		auto fullPath = g_pEnv->_fileSystem->GetLocalAbsolutePath(localPath);

		g_pEnv->_sceneManager->GetCurrentScene()->Clear();

		SaveFile load(fullPath, std::ios::in | std::ios::binary);

		if (!load.Load())
		{
			LOG_CRIT("Saving failed!");
			return;
		}
	}
}