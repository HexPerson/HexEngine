#pragma once

#include <HexEngine.Core/HexEngine.hpp>
#include <HexEngine.Core/Entity/Component/IDistrictGate.hpp>
#include <string>
#include <unordered_map>
#include <vector>

// A district that is closed until the game unlocks it (Whereabouts: the high-rise
// district over the bridge opens at Pro rank). While locked:
//   - the listed traffic lanes are closed (TrafficLaneComponent::SetClosed), so no car
//     plans or turns into the district;
//   - routine NPCs never pick a venue / workplace on the other side of the gate
//     (CanTravel), so city residents don't try to go in and district residents stay in;
//   - the barrier pieces stay up: "arms" rotate open by _armOpenAngle about their local
//     Z when unlocked, "blocks" are hidden and their colliders become triggers.
// The game decides the state (SetLocked) - nothing here is saved per playthrough, the
// authored scene keeps _locked = true. Applied only while the game runs, so editor
// saves never bake an opened gate.
class DistrictGateComponent : public HexEngine::UpdateComponent, public HexEngine::IDistrictGate
{
public:
	CREATE_COMPONENT_ID(DistrictGateComponent);
	DEFINE_COMPONENT_CTOR(DistrictGateComponent);

	virtual void Update(float frameTime) override;
	virtual void Serialize(json& data, HexEngine::JsonFile* file) override;
	virtual void Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask = 0) override;
	virtual bool CreateWidget(HexEngine::ComponentWidget* widget) override;
	virtual void OnRenderEditorGizmo(bool isSelected, bool& isHovering) override;

	// IDistrictGate
	bool IsLocked() const override { return _locked; }
	void SetLocked(bool locked) override { _locked = locked; }
	const std::string& GetDistrictName() const override { return _districtName; }
	bool ContainsPoint(float x, float y, float z) const override { return Contains(math::Vector3(x, y, z)); }

	bool Contains(const math::Vector3& worldPos) const;

	// True when worldPos lies inside any LOCKED district.
	static bool IsPointLocked(HexEngine::Scene* scene, const math::Vector3& worldPos);
	// False when a locked gate separates a and b (exactly one of them inside it).
	static bool CanTravel(HexEngine::Scene* scene, const math::Vector3& a, const math::Vector3& b);
	// Every gate in the scene (game code: unlock by name / all).
	static void GetGates(HexEngine::Scene* scene, std::vector<DistrictGateComponent*>& out);

private:
	void Apply(bool locked);

	bool _locked = true;
	std::string _districtName = "District";
	std::string _closedLanesCsv;
	std::string _armsCsv;
	std::string _blocksCsv;
	math::Vector3 _boundsMin = math::Vector3(-50.0f, -1000.0f, -50.0f);
	math::Vector3 _boundsMax = math::Vector3(50.0f, 1000.0f, 50.0f);
	float _armOpenAngle = 80.0f;

	// runtime
	int32_t _appliedState = -1;		// -1 never applied, 0 open, 1 locked
	std::unordered_map<std::string, math::Quaternion> _armClosedRotation;
	std::vector<std::string> _hiddenByGate;
};
