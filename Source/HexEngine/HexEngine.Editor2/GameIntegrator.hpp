
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

	class GameIntegrator : public IEntityListener
	{
	public:
		bool BuildGame(const std::wstring& projectFileName);
		bool LoadGame();
		bool RunGame();
		bool StopGame();
		GameTestState GetState() const;

		virtual void OnAddEntity(Entity* entity) override;
		virtual void OnRemoveEntity(Entity* entity) override;
		virtual void OnAddComponent(Entity* entity, BaseComponent* component) override {};
		virtual void OnRemoveComponent(Entity* entity, BaseComponent* component) override {};

	private:
		std::vector<Entity*> _tempEntitiesCreated;
		FileSystem* _runtimeFS;
		HMODULE _gameDll = 0;
		IGameExtension* _gameExtension = 0;
		GameTestState _state = GameTestState::None;
	};
}
