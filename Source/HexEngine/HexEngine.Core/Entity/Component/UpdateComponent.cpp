

#include "UpdateComponent.hpp"
#include "../../HexEngine.hpp"

namespace HexEngine
{
	void UpdateComponent::Update(float frameTime)
	{
		UpdatedMessage message;
		message._time = g_pEnv->_timeManager->GetTime();
		GetEntity()->OnMessage(&message, this);

		_lastUpdateTick = (int32_t)g_pEnv->_timeManager->_frameCount;
		// Game time (see TimeManager::_gameTime): Scene::Update hands tick-rated
		// components the game time elapsed since this.
		_lastUpdateTime = (float)g_pEnv->_timeManager->_gameTime;
	}

	void UpdateComponent::SetTickRate(int32_t tickRate)
	{
		_tickRate = tickRate;
	}

	int32_t UpdateComponent::GetTickRate() const
	{
		return _tickRate;
	}

	bool UpdateComponent::CanUpdate() const
	{
		return g_pEnv->_timeManager->_frameCount - _lastUpdateTick >= _tickRate;
	}
}