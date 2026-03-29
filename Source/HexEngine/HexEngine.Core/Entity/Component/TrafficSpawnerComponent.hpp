#pragma once

#include "BaseComponent.hpp"
#include "../../Scene/IEntityListener.hpp"
#include <string>
#include <vector>

namespace HexEngine
{
	class HEX_API TrafficSpawnerComponent : public BaseComponent, public IEntityListener
	{
	public:
		CREATE_COMPONENT_ID(TrafficSpawnerComponent);
		DEFINE_COMPONENT_CTOR(TrafficSpawnerComponent);
		virtual void Destroy() override;

		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;
		virtual bool CreateWidget(ComponentWidget* widget) override;
		virtual void OnRenderEditorGizmo(bool isSelected, bool& isHovering) override;
		virtual void OnAddEntity(Entity* entity) override {}
		virtual void OnRemoveEntity(Entity* entity) override;
		virtual void OnAddComponent(Entity* entity, BaseComponent* component) override {}
		virtual void OnRemoveComponent(Entity* entity, BaseComponent* component) override {}

		const std::string& GetLaneEntityName() const { return _laneEntityName; }
		const std::vector<std::string>& GetVehiclePrefabPaths() const { return _vehiclePrefabPaths; }
		float GetSpawnIntervalSeconds() const { return _spawnIntervalSeconds; }
		int32_t GetMaxActiveVehicles() const { return _maxActiveVehicles; }
		float GetActivationDistance() const { return _activationDistance; }
		float GetSpawnClearanceDistance() const { return _spawnClearanceDistance; }
		bool IsEnabled() const { return _enabled; }
		bool ShouldDespawnWhenInactive() const { return _despawnWhenInactive; }

		float& SpawnTimerRef() { return _spawnTimer; }
		std::vector<Entity*>& ActiveVehicles() { return _activeVehicles; }

	private:
		void RegisterEntityListener();
		void UnregisterEntityListener();

	private:
		std::string _laneEntityName;
		std::vector<std::string> _vehiclePrefabPaths;
		float _spawnIntervalSeconds = 2.0f;
		int32_t _maxActiveVehicles = 6;
		float _activationDistance = 200.0f;
		float _spawnClearanceDistance = 7.0f;
		bool _enabled = true;
		bool _despawnWhenInactive = true;
		bool _drawDebug = true;
		bool _isListeningForEntityRemovals = false;

		float _spawnTimer = 0.0f;
		std::vector<Entity*> _activeVehicles;
	};
}
