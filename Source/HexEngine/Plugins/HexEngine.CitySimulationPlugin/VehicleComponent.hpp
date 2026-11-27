#pragma once

#include <HexEngine.Core/HexEngine.hpp>

namespace HexEngine { class Material; }

class VehicleComponent : public HexEngine::BaseComponent
{
public:
	CREATE_COMPONENT_ID(VehicleComponent);
	DEFINE_COMPONENT_CTOR(VehicleComponent);

	virtual void Serialize(json& data, HexEngine::JsonFile* file) override;
	virtual void Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask = 0) override;
	virtual bool CreateWidget(HexEngine::ComponentWidget* widget) override;

	void RebuildGeneratedEntities();
	// Roll the generated wheel parts by `distance` metres of travel (forward = +).
	void SpinWheels(float distance);

private:
	void RemoveGeneratedChildrenByPrefix(const std::string& prefix);
	void EnsureGeneratedMeshChild(const std::string& childName, const fs::path& meshPath, const std::shared_ptr<HexEngine::Material>& paint);
	static std::vector<fs::path> ToPathList(const std::vector<std::string>& paths);
	void CollectWheelParts();

private:
	fs::path _baseMeshPath;
	std::vector<std::string> _doorMeshPaths;
	fs::path _glassMeshPath;
	fs::path _trunkMeshPath;
	std::vector<std::string> _wheelMeshPaths;
	// Every other part of the model (door/trunk glass, number plates, steering
	// wheel, light bars, ...). All parts are authored in vehicle space.
	std::vector<std::string> _extraMeshPaths;

	// Colour/livery variant: generated parts whose material is the kit's paint
	// atlas (_paintSourceMaterial, matched by file stem) get _paintMaterialPath
	// instead. Glass and other materials are left alone. Empty = mesh default.
	fs::path _paintMaterialPath;
	std::string _paintSourceMaterial = "M_SimpligonUniverse_03_A";

	// Runtime: wheel parts are authored in vehicle space, so each spins about its
	// own hub (mesh AABB centre), not the part entity's origin.
	struct WheelPart
	{
		HexEngine::EntityId id;
		math::Vector3 hub;
		float radius = 0.35f;
	};
	std::vector<WheelPart> _wheelParts;
	bool _wheelPartsCollected = false;
	float _wheelAngle = 0.0f;

	float _maxSpeed = 120.0f;
	float _acceleration = 10.0f;
	int32_t _passengerCapacity = 4;
};

