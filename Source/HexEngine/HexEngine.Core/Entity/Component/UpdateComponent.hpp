

#pragma once

#include "BaseComponent.hpp"

namespace HexEngine
{
	class UpdateComponent : public BaseComponent
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

		void SetTickRate(int32_t tickRate);
		int32_t GetTickRate() const;
		bool CanUpdate() const;

	private:
		int32_t _tickRate = 1;
		int32_t _lastUpdateTick = 0;
	};
}
