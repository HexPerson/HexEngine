

#include "BaseComponent.hpp"

#include "../../Environment/IEnvironment.hpp"
#include "../../Environment/LogFile.hpp"
#include "../../Scene/Scene.hpp"
#include "../../Scene/SceneManager.hpp"

namespace HexEngine
{
	BaseComponent::BaseComponent(Entity* entity) :
		_entity(entity)
	{}

	Entity* BaseComponent::GetEntity() const
	{
		return _entity;
	}

	void BaseComponent::BroadcastMessage(Message* message)
	{
		for (auto& entSet : g_pEnv->_sceneManager->GetCurrentScene()->GetEntities())
		{
			for (auto& ent : entSet.second)
			{
				if (ent == GetEntity())
					continue;

				ent->OnMessage(message, this);
			}
		}
	}

	void BaseComponent::WaitForDeserialize()
	{
		while (_serializationState == SerializationState::Deserializing)
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
}