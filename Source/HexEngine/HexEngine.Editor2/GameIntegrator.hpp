
#pragma once

#include <HexEngine.Core\HexEngine.hpp>

namespace HexEditor
{
	enum class GameTestState
	{
		None,
		Loaded,
		Stopped,
		Started,
		Max
	};

	class GameIntegrator : public HexEngine::IEntityListener
	{
	public:
		bool BuildGame(const std::wstring& projectFileName);
		bool LoadGame();
		bool RunGame();
		bool StopGame();
		GameTestState GetState() const;

		virtual void OnAddEntity(HexEngine::Entity* entity) override;
		virtual void OnRemoveEntity(HexEngine::Entity* entity) override;
		virtual void OnAddComponent(HexEngine::Entity* entity, HexEngine::BaseComponent* component) override {};
		virtual void OnRemoveComponent(HexEngine::Entity* entity, HexEngine::BaseComponent* component) override {};

	private:
		std::vector<HexEngine::Entity*> _tempEntitiesCreated;
		HexEngine::FileSystem* _runtimeFS;
		HMODULE _gameDll = 0;
		HexEngine::IGameExtension* _gameExtension = 0;
		GameTestState _state = GameTestState::None;
	};
}
