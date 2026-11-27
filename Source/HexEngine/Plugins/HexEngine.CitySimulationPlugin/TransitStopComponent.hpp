#pragma once

#include <HexEngine.Core/HexEngine.hpp>
#include <functional>

// A place where people get on and off public transport: a bus-stop shelter or a
// train platform. The entity's position is the waiting area - waiting NPCs stand
// within _waitRadius of it (offset by _waitOffset, in the entity's local frame).
//
// Buses pull up at _laneWaypointName: a TrafficLane node on the kerb side next
// to the shelter (empty = the nearest lane node, resolved at play start).
// Trains stop where their line's track passes closest to the stop entity.
//
// At runtime the stop keeps the queue of routine agents waiting here, so an
// arriving vehicle can board the ones heading somewhere it goes.
class TransitStopComponent : public HexEngine::BaseComponent
{
public:
	CREATE_COMPONENT_ID(TransitStopComponent);
	DEFINE_COMPONENT_CTOR(TransitStopComponent);

	virtual void Serialize(json& data, HexEngine::JsonFile* file) override;
	virtual void Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask = 0) override;
	virtual bool CreateWidget(HexEngine::ComponentWidget* widget) override;
	virtual void OnRenderEditorGizmo(bool isSelected, bool& isHovering) override;

	// What signs, announcements and the HUD call it (falls back to the entity name).
	const std::string& GetDisplayName() const;
	// CSV: a stop can have a node on each lane chain passing it; the bus takes
	// whichever its route arrives on.
	const std::string& GetLaneWaypointName() const { return _laneWaypointName; }
	std::vector<std::string> GetLaneWaypointCandidates() const;
	void SetLaneWaypointName(const std::string& name) { _laneWaypointName = name; }
	math::Vector3 GetWaitCentre() const;
	float GetWaitRadius() const { return _waitRadius; }
	// A standing spot in the waiting area, spread deterministically by seed.
	math::Vector3 GetWaitSpot(uint32_t seed) const;

	struct Waiting
	{
		std::string agentName;
		std::string lineEntityName;
		std::string alightStopName;
		float since = 0.0f;
	};
	void AddWaiting(const Waiting& waiting);
	void RemoveWaiting(const std::string& agentName);
	const std::vector<Waiting>& GetWaiting() const { return _waiting; }
	// Removes and returns up to maxCount waiting agents for this line whose
	// destination stop the vehicle will reach (accept(alightStop) == true).
	std::vector<Waiting> TakeBoarders(const std::string& lineEntityName, int32_t maxCount, const std::function<bool(const std::string&)>& accept);
	void ClearWaiting() { _waiting.clear(); }

private:
	std::string _displayName;
	std::string _laneWaypointName;
	math::Vector3 _waitOffset = math::Vector3::Zero;
	float _waitRadius = 2.0f;

	std::vector<Waiting> _waiting;	// runtime
};
