#pragma once

#include <HexEngine.Core/HexEngine.hpp>

// Somewhere cars park OFF the carriageway: a public car park or a house's driveway.
// Routine NPCs drive to it, pull into a reserved bay and leave the car there (visible,
// parked) until they need it again; then they back out and rejoin the road.
//
// Everything is in the entity's local frame (rotation applies, scale doesn't):
//   bays      explicit list (_bays: position + yaw of the car's nose), or - when the
//             list is empty - a generated row along +X: _bayCount bays _baySpacing apart
//             from the origin, noses pointing -Z; _doubleRow adds a facing row across
//             the aisle (noses +Z).
//   aisle     a car pulls in from (and backs out to) the point _aisleDistance behind
//             its bay's nose (bay - nose * _aisleDistance).
//   entrance  the point on the road where cars turn in / pull out (in the lane, just
//             past the dropped kerb). The road side is picked automatically: the nearest
//             quiet lane nodes (not junctions or bus stops), or _accessWaypointsCsv.
//   gate      optional (_useGate): the lot-side end of the entrance driveway, so cars go
//             road -> dropped kerb -> gate -> aisle instead of cutting across verges.
//
// _privateTo names a residence: only its household parks there (a driveway). Public
// lots serve destinations within _maxWalkDistance.
class ParkingLotComponent : public HexEngine::BaseComponent
{
public:
	CREATE_COMPONENT_ID(ParkingLotComponent);
	DEFINE_COMPONENT_CTOR(ParkingLotComponent);

	virtual void Serialize(json& data, HexEngine::JsonFile* file) override;
	virtual void Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask = 0) override;
	virtual bool CreateWidget(HexEngine::ComponentWidget* widget) override;
	virtual void OnRenderEditorGizmo(bool isSelected, bool& isHovering) override;

	struct Bay
	{
		math::Vector3 position = math::Vector3::Zero;	// local
		float yawDegrees = 0.0f;						// local, nose direction (0 = +Z)
	};

	bool IsEnabled() const { return _enabled; }
	bool IsPrivate() const { return !_privateTo.empty(); }
	const std::string& GetPrivateTo() const { return _privateTo; }
	float GetMaxWalkDistance() const { return _maxWalkDistance; }
	const std::string& GetAccessWaypointsCsv() const { return _accessWaypointsCsv; }

	int32_t GetBayCount() const;
	// World-space parked position and (horizontal, unit) nose direction of a bay.
	bool GetBay(int32_t bay, math::Vector3& outPosition, math::Vector3& outNose) const;
	math::Vector3 GetAislePoint(int32_t bay) const;
	math::Vector3 GetEntrancePoint() const;
	bool HasGate() const { return _useGate; }
	math::Vector3 GetGatePoint() const;

	// Off-lane legs for TrafficVehicleComponent::SetOffLaneLegs.
	void BuildArrivalLeg(int32_t bay, std::vector<math::Vector3>& out) const;		// entrance -> aisle -> bay
	void BuildDepartureLeg(int32_t bay, std::vector<math::Vector3>& out, int32_t& outReversePoints) const;	// bay -> aisle (reversing) -> entrance

	// Occupancy (runtime): a bay is reserved from the moment a driver heads for it.
	int32_t FindFreeBay(const math::Vector3& nearPoint) const;
	bool IsBayFree(int32_t bay) const;
	int32_t CountFreeBays() const;
	const std::string& GetBayOccupant(int32_t bay) const;
	bool Reserve(int32_t bay, const std::string& carName);
	void Release(int32_t bay, const std::string& carName);
	void ReleaseAll();

private:
	void EnsureOccupancy() const;
	math::Quaternion WorldRotation() const;
	void GetLocalBay(int32_t bay, math::Vector3& outLocal, float& outYawRadians) const;

	bool _enabled = true;
	std::string _privateTo;
	std::vector<Bay> _bays;
	int32_t _bayCount = 6;
	float _baySpacing = 2.8f;
	bool _doubleRow = false;
	float _aisleDistance = 6.0f;
	math::Vector3 _entrance = math::Vector3(-4.0f, 0.0f, 6.0f);
	bool _useGate = false;
	math::Vector3 _gate = math::Vector3::Zero;
	std::string _accessWaypointsCsv;
	float _maxWalkDistance = 180.0f;

	mutable std::vector<std::string> _occupants;	// runtime, per bay (car entity name)
};
