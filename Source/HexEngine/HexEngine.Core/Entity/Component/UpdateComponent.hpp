

#pragma once

#include "BaseComponent.hpp"

namespace HexEngine
{
	class HEX_API UpdateComponent : public BaseComponent
	{
	public:
		CREATE_COMPONENT_ID(UpdateComponent);

		UpdateComponent(Entity* entity) :
			BaseComponent(entity)
		{}

		UpdateComponent(Entity* entity, UpdateComponent* copy) : BaseComponent(entity) {}

		virtual void Update(float frameTime);

		virtual void FixedUpdate(float frameTime) {}

		virtual void LateUpdate(float frameTime) {}

		// Opt-in second pass, run by Scene::Update after EVERY component's Update
		// and before rendering: for work that must see this frame's final poses
		// of other entities (a player riding a moving bus follows it here).
		virtual bool WantsPostUpdate() const { return false; }
		virtual void PostUpdate(float frameTime) { (void)frameTime; }

		void SetTickRate(int32_t tickRate);
		int32_t GetTickRate() const;
		bool CanUpdate() const;
		float GetLastUpdateTime() const { return _lastUpdateTime; }

	private:
		int32_t _tickRate = 1;
		int32_t _lastUpdateTick = 0;
		float _lastUpdateTime = 0.0f;
	};
}
