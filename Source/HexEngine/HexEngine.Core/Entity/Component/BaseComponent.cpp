

#include "BaseComponent.hpp"

#include "../../Environment/IEnvironment.hpp"
#include "../../Environment/LogFile.hpp"
#include "../../Scene/Scene.hpp"
#include "../../Scene/SceneManager.hpp"
#include "../Entity.hpp"

namespace HexEngine
{
	BaseComponent::BaseComponent(Entity* entity) :
		_entity(entity)
	{
		if (_entity != nullptr)
			_ownerId = _entity->GetId();
	}

	Entity* BaseComponent::GetEntity() const
	{
		return _entity;
	}

	void BaseComponent::BroadcastMessage(Message* message)
	{
		auto scene = g_pEnv->_sceneManager->GetCurrentScene();
		if (scene == nullptr)
			return;

		std::vector<EntityId> liveEntityIds;
		if (!scene->GetLiveEntityIds(liveEntityIds))
			return;

		for (const EntityId id : liveEntityIds)
		{
			if (id == _ownerId)
				continue;

			Entity* entity = scene->TryGetEntity(id);
			if (entity == nullptr)
				continue;

			entity->OnMessage(message, this);
		}
	}

	void BaseComponent::WaitForDeserialize()
	{
		while (_serializationState == SerializationState::Deserializing)
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
}
