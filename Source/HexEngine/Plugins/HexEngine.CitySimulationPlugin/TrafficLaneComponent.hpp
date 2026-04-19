#pragma once

#include <HexEngine.Core/HexEngine.hpp>

class TrafficLaneComponent : public HexEngine::BaseComponent
{
public:
	CREATE_COMPONENT_ID(TrafficLaneComponent);
	DEFINE_COMPONENT_CTOR(TrafficLaneComponent);

	virtual void Serialize(json& data, HexEngine::JsonFile* file) override;
	virtual void Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask = 0) override;
	virtual bool CreateWidget(HexEngine::ComponentWidget* widget) override;
	virtual void OnDebugRender() override;
	virtual void OnRenderEditorGizmo(bool isSelected, bool& isHovering) override;

	void GatherLaneWaypointEntities(std::vector<HexEngine::Entity*>& outWaypoints) const;
	void GatherLanePoints(std::vector<math::Vector3>& outPoints) const;
	const std::vector<std::string>& GetNextLaneEntityNames() const { return _nextLaneEntityNames; }
	std::string GetNextLaneEntityName();
	bool AddNextLaneEntityName(const std::string& laneEntityName);
	void SetNextLaneNamesFromCsv(const std::string& csv);
	std::string GetNextLaneNamesCsv() const;
	bool IsLooping() const { return _loop; }
	float GetSpeedLimit() const { return _speedLimit; }
	bool GetDrawDebug() const { return _drawDebug; }

private:
	bool _loop = true;
	float _speedLimit = 30.0f;
	bool _drawDebug = true;
	std::vector<std::string> _nextLaneEntityNames;
	size_t _sequentialNextLaneCursor = 0;
	math::Vector3 _branchOffset = math::Vector3(5.0f, 0.0f, 0.0f);
};



