#pragma once

#include "BaseComponent.hpp"
#include <string>

namespace HexEngine
{
	class HEX_API ResidenceComponent : public BaseComponent
	{
	public:
		CREATE_COMPONENT_ID(ResidenceComponent);
		DEFINE_COMPONENT_CTOR(ResidenceComponent);

		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;
		virtual bool CreateWidget(ComponentWidget* widget) override;

		int32_t GetHouseholdCapacity() const { return _householdCapacity; }
		const std::string& GetEntryWaypointEntityName() const { return _entryWaypointEntityName; }
		const std::string& GetParkingWaypointEntityName() const { return _parkingWaypointEntityName; }

	private:
		int32_t _householdCapacity = 4;
		std::string _entryWaypointEntityName;
		std::string _parkingWaypointEntityName;
	};
}
