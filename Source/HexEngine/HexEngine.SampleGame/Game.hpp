

#pragma once

#include "../HexEngine.Core/Environment/IGameExtension.hpp"
#include "../HexEngine.Core/Scene/Scene.hpp"
#include "../HexEngine.Core/Entity/DirectionalLight.hpp"
#include "../HexEngine.Core/Entity/Terrain.hpp"
#include "Input.hpp"
#include "Game\DayNightCycle.hpp"
#include "Game\World\World.hpp"
#include "../HexEngine.Core/GUI/DebugGUI.hpp"
#include "PlayerController.hpp"

using namespace HexEngine;

namespace CityBuilder
{
	class Game : public IGameExtension, public HexEngine::IDebugGUICallback
	{
	public:
		virtual void OnCreateGame() override;

		virtual void OnUpdate(float frameTime) override;

		virtual void OnFixedUpdate(float frameTime) override;

		virtual void OnShutdown() override;

		virtual void OnGui() override;

		virtual void OnDebugRender() override;

		virtual void OnDebugGUI() override;

		void SaveLevel(const fs::path& filename);
		void LoadLevel(const fs::path& filename);

		Terrain* GetWorldTerrain();

		Scene* GetGameScene();

	public:
		Scene* _gameScene = nullptr;
		Input* _input = nullptr;
		World* _world = nullptr;

		DayNightCycle _dayCycle;
		PlayerController* _playerController;
	};

	extern Game* g_pGame;
}
