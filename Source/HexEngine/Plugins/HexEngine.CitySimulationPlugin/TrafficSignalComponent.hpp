#pragma once

#include <HexEngine.Core/HexEngine.hpp>

class TrafficSignalControllerComponent;

// A traffic-light head (the visible prop). Shows the state of one arm of a
// TrafficSignalControllerComponent junction by swapping the material on its
// own StaticMeshComponent:
//  - Vehicle heads: red / amber / green material (the kit's atlas with the
//    matching emissive map lit).
//  - Pedestrian heads: walk / don't-walk mesh AND material (the kit's
//    Crosswalk_Go / Crosswalk_Stop meshes).
// An empty controller name inherits the controller (and arm) of the nearest
// parent TrafficSignalComponent, so a pedestrian head parented to a signal
// post needs no wiring of its own.
class TrafficSignalComponent : public HexEngine::UpdateComponent
{
public:
	CREATE_COMPONENT_ID(TrafficSignalComponent);
	DEFINE_COMPONENT_CTOR(TrafficSignalComponent);

	enum class Role : int32_t
	{
		Vehicle = 0,
		Pedestrian = 1,
	};

	virtual void Update(float frameTime) override;
	virtual void Serialize(json& data, HexEngine::JsonFile* file) override;
	virtual void Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask = 0) override;
	virtual bool CreateWidget(HexEngine::ComponentWidget* widget) override;

	void SetController(const std::string& controllerEntityName, const std::string& arm);
	const std::string& GetControllerEntityName() const { return _controllerEntityName; }
	const std::string& GetArm() const { return _arm; }

private:
	TrafficSignalControllerComponent* ResolveController(std::string& outArm) const;
	void ApplyVisual(int32_t visualState);

private:
	// config (serialized)
	std::string _controllerEntityName;
	std::string _arm = "N";
	int32_t _role = (int32_t)Role::Vehicle;
	std::string _redMaterial;
	std::string _amberMaterial;
	std::string _greenMaterial;
	std::string _walkMesh;
	std::string _dontWalkMesh;
	std::string _walkMaterial;
	std::string _dontWalkMaterial;

	// runtime
	int32_t _appliedState = -1;
	bool _giExcluded = false;
};
