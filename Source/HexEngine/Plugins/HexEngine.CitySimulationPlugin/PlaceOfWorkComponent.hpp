#pragma once

#include <HexEngine.Core/HexEngine.hpp>

class PlaceOfWorkComponent : public HexEngine::BaseComponent
{
public:
	CREATE_COMPONENT_ID(PlaceOfWorkComponent);
	DEFINE_COMPONENT_CTOR(PlaceOfWorkComponent);

	virtual void Serialize(json& data, HexEngine::JsonFile* file) override;
	virtual void Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask = 0) override;
	virtual bool CreateWidget(HexEngine::ComponentWidget* widget) override;

	const std::vector<std::string>& GetRoleTags() const { return _roleTags; }
	int32_t GetWorkerCapacity() const { return _workerCapacity; }
	int32_t GetServicePriority() const { return _servicePriority; }
	const std::string& GetEntryWaypointEntityName() const { return _entryWaypointEntityName; }
	const std::string& GetParkingWaypointEntityName() const { return _parkingWaypointEntityName; }

	bool IsHourInAnyShift(float hour) const;

private:
	void RebuildRoleTags();
	void RebuildShiftsFromCsv();

private:
	std::string _roleTagsCsv = "Worker";
	std::vector<std::string> _roleTags;
	int32_t _workerCapacity = 8;
	int32_t _servicePriority = 0;
	std::string _shiftWindowsCsv = "9-17";
	std::vector<math::Vector2> _shiftWindows;
	std::string _entryWaypointEntityName;
	std::string _parkingWaypointEntityName;
};


