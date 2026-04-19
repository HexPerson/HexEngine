#pragma once

#include <HexEngine.Core/HexEngine.hpp>

class VehicleComponent : public HexEngine::BaseComponent
{
public:
	CREATE_COMPONENT_ID(VehicleComponent);
	DEFINE_COMPONENT_CTOR(VehicleComponent);

	virtual void Serialize(json& data, HexEngine::JsonFile* file) override;
	virtual void Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask = 0) override;
	virtual bool CreateWidget(HexEngine::ComponentWidget* widget) override;

	void RebuildGeneratedEntities();

private:
	void RemoveGeneratedChildrenByPrefix(const std::string& prefix);
	void EnsureGeneratedMeshChild(const std::string& childName, const fs::path& meshPath);
	static std::vector<fs::path> ToPathList(const std::vector<std::string>& paths);

private:
	fs::path _baseMeshPath;
	std::vector<std::string> _doorMeshPaths;
	fs::path _glassMeshPath;
	fs::path _trunkMeshPath;
	std::vector<std::string> _wheelMeshPaths;

	float _maxSpeed = 120.0f;
	float _acceleration = 10.0f;
	int32_t _passengerCapacity = 4;
};

