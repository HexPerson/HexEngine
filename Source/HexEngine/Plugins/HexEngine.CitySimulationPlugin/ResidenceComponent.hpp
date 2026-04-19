#pragma once

#include <HexEngine.Core/HexEngine.hpp>

class ResidenceComponent : public HexEngine::BaseComponent
{
public:
	CREATE_COMPONENT_ID(ResidenceComponent);
	DEFINE_COMPONENT_CTOR(ResidenceComponent);

	virtual void Serialize(json& data, HexEngine::JsonFile* file) override;
	virtual void Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask = 0) override;
	virtual bool CreateWidget(HexEngine::ComponentWidget* widget) override;

	int32_t GetHouseholdCapacity() const { return _householdCapacity; }
	const std::string& GetEntryWaypointEntityName() const { return _entryWaypointEntityName; }
	const std::string& GetParkingWaypointEntityName() const { return _parkingWaypointEntityName; }

private:
	int32_t _householdCapacity = 4;
	std::string _entryWaypointEntityName;
	std::string _parkingWaypointEntityName;
};