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

	// Resolved against this building's own hierarchy first (prefab instances
	// get de-duplicated child names), then the scene.
	std::string ResolveEntryWaypointName() const;
	std::string ResolveParkingWaypointName() const;

	void SetHouseholdCapacity(int32_t v) { _householdCapacity = v; }
	void SetEntryWaypointEntityName(const std::string& v) { _entryWaypointEntityName = v; }
	void SetParkingWaypointEntityName(const std::string& v) { _parkingWaypointEntityName = v; }

private:
	int32_t _householdCapacity = 4;
	std::string _entryWaypointEntityName;
	std::string _parkingWaypointEntityName;
};