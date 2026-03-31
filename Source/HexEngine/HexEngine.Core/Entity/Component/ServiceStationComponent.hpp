#pragma once

#include "BaseComponent.hpp"
#include <string>
#include <vector>

namespace HexEngine
{
	class HEX_API ServiceStationComponent : public BaseComponent
	{
	public:
		CREATE_COMPONENT_ID(ServiceStationComponent);
		DEFINE_COMPONENT_CTOR(ServiceStationComponent);

		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;
		virtual bool CreateWidget(ComponentWidget* widget) override;

		const std::vector<std::string>& GetServiceTags() const { return _serviceTags; }
		float GetDispatchRadius() const { return _dispatchRadius; }
		int32_t GetPriority() const { return _priority; }
		const std::string& GetVehiclePrefabPath() const { return _vehiclePrefabPath; }
		const std::string& GetEntryWaypointEntityName() const { return _entryWaypointEntityName; }
		const std::string& GetParkingWaypointEntityName() const { return _parkingWaypointEntityName; }

	private:
		void RebuildServiceTags();

	private:
		std::string _serviceTagsCsv = "Medical";
		std::vector<std::string> _serviceTags;
		float _dispatchRadius = 5000.0f;
		int32_t _priority = 0;
		std::string _vehiclePrefabPath;
		std::string _entryWaypointEntityName;
		std::string _parkingWaypointEntityName;
	};
}
