#pragma once

#include "UpdateComponent.hpp"
#include "../../Scene/IEntityListener.hpp"
#include <string>
#include <vector>

namespace HexEngine
{
	class RoutineAgentComponent;
	class CityRoutineSystemComponent;
	class ServiceStationComponent;

	class HEX_API CityEmergencyDispatcherSystemComponent : public UpdateComponent, public IEntityListener
	{
	public:
		CREATE_COMPONENT_ID(CityEmergencyDispatcherSystemComponent);
		DEFINE_COMPONENT_CTOR(CityEmergencyDispatcherSystemComponent);
		virtual ~CityEmergencyDispatcherSystemComponent();

		virtual void Destroy() override;
		virtual void Update(float frameTime) override;
		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;
		virtual bool CreateWidget(ComponentWidget* widget) override;

		virtual void OnAddEntity(Entity* entity) override {}
		virtual void OnRemoveEntity(Entity* entity) override;
		virtual void OnAddComponent(Entity* entity, BaseComponent* component) override {}
		virtual void OnRemoveComponent(Entity* entity, BaseComponent* component) override {}

		uint32_t ReportEmergency(const math::Vector3& worldPosition, const std::string& requiredServiceTag = "Medical");
		bool ResolveEmergency(uint32_t incidentId);

	private:
		struct EmergencyIncident
		{
			uint32_t id = 0;
			math::Vector3 worldPosition = math::Vector3::Zero;
			std::string requiredServiceTag = "Medical";
			std::string destinationEntryWaypointName;
			std::string destinationParkingWaypointName;
			std::string claimedAgentEntityName;
			bool isResolved = false;
			float createdAtTime = 0.0f;
		};

		void RegisterEntityListener();
		void UnregisterEntityListener();
		void UpdateDispatcher(float frameTime);
		CityRoutineSystemComponent* GetRoutineSystem() const;
		std::string FindNearestWaypointName(const math::Vector3& worldPosition) const;
		ServiceStationComponent* FindNearestServiceStationForTag(const math::Vector3& worldPosition, const std::string& tag) const;
		RoutineAgentComponent* FindBestEmergencyResponder(const EmergencyIncident& incident, const ServiceStationComponent* station) const;
		bool AssignEmergencyTask(CityRoutineSystemComponent* routineSystem, RoutineAgentComponent* agent, const ServiceStationComponent* station, EmergencyIncident& incident);

	private:
		bool _enabled = true;
		float _dispatchTickSeconds = 0.5f;
		bool _isListeningForEntityEvents = false;

		float _dispatcherAccumulator = 0.0f;
		uint32_t _nextIncidentId = 1;
		std::vector<EmergencyIncident> _incidents;
	};
}
