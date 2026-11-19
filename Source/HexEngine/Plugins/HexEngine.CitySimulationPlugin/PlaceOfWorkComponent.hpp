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

	// Services this place offers to VISITORS (routine schedule activities other
	// than Home/Work, e.g. "Food", "Shop", "Leisure"). Open while any shift
	// window covers the hour.
	const std::vector<std::string>& GetServiceTags() const { return _serviceTags; }
	bool OffersService(const std::string& tag) const;
	int32_t GetVisitorCapacity() const { return _visitorCapacity; }

	// Waypoint entities resolved against this building's own hierarchy first
	// (prefab instances get de-duplicated child names), then the scene.
	std::string ResolveEntryWaypointName() const;
	std::string ResolveParkingWaypointName() const;

	void SetServiceTagsCsv(const std::string& v) { _serviceTagsCsv = v; RebuildServiceTags(); }
	void SetRoleTagsCsv(const std::string& v) { _roleTagsCsv = v; RebuildRoleTags(); }
	void SetShiftWindowsCsv(const std::string& v) { _shiftWindowsCsv = v; RebuildShiftsFromCsv(); }
	void SetWorkerCapacity(int32_t v) { _workerCapacity = v; }
	void SetEntryWaypointEntityName(const std::string& v) { _entryWaypointEntityName = v; }
	void SetParkingWaypointEntityName(const std::string& v) { _parkingWaypointEntityName = v; }

private:
	void RebuildRoleTags();
	void RebuildShiftsFromCsv();
	void RebuildServiceTags();

private:
	std::string _roleTagsCsv = "Worker";
	std::vector<std::string> _roleTags;
	int32_t _workerCapacity = 8;
	int32_t _servicePriority = 0;
	std::string _shiftWindowsCsv = "9-17";
	std::vector<math::Vector2> _shiftWindows;
	std::string _entryWaypointEntityName;
	std::string _parkingWaypointEntityName;
	std::string _serviceTagsCsv;
	std::vector<std::string> _serviceTags;
	int32_t _visitorCapacity = 12;
};


