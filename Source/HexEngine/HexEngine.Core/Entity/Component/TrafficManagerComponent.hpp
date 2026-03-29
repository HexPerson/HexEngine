#pragma once

#include "UpdateComponent.hpp"

namespace HexEngine
{
	class Camera;
	class TrafficSpawnerComponent;

	class HEX_API TrafficManagerComponent : public UpdateComponent
	{
	public:
		CREATE_COMPONENT_ID(TrafficManagerComponent);
		DEFINE_COMPONENT_CTOR(TrafficManagerComponent);

		virtual void Update(float frameTime) override;
		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;
		virtual bool CreateWidget(ComponentWidget* widget) override;

	private:
		void UpdateSpawner(TrafficSpawnerComponent* spawner, Camera* camera, float frameTime, int32_t& activeGlobalCount);
		int32_t SpawnVehicleFromSpawner(TrafficSpawnerComponent* spawner);
		int32_t DespawnSpawnerVehicles(TrafficSpawnerComponent* spawner);

	private:
		bool _enabled = true;
		int32_t _globalMaxActiveVehicles = 200;
		float _globalActivationDistance = 500.0f;
		bool _drawDebug = true;
	};
}
