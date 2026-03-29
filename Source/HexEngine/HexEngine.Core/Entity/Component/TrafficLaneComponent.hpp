#pragma once

#include "BaseComponent.hpp"
#include <string>
#include <vector>

namespace HexEngine
{
	class HEX_API TrafficLaneComponent : public BaseComponent
	{
	public:
		CREATE_COMPONENT_ID(TrafficLaneComponent);
		DEFINE_COMPONENT_CTOR(TrafficLaneComponent);

		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;
		virtual bool CreateWidget(ComponentWidget* widget) override;
		virtual void OnRenderEditorGizmo(bool isSelected, bool& isHovering) override;

		void GatherLaneWaypointEntities(std::vector<Entity*>& outWaypoints) const;
		void GatherLanePoints(std::vector<math::Vector3>& outPoints) const;
		const std::vector<std::string>& GetNextLaneEntityNames() const { return _nextLaneEntityNames; }
		std::string GetNextLaneEntityName();
		void SetNextLaneNamesFromCsv(const std::string& csv);
		std::string GetNextLaneNamesCsv() const;
		bool IsLooping() const { return _loop; }
		float GetSpeedLimit() const { return _speedLimit; }
		bool GetDrawDebug() const { return _drawDebug; }
		bool GetRandomNextLane() const { return _randomNextLane; }

	private:
		bool _loop = true;
		float _speedLimit = 30.0f;
		bool _drawDebug = true;
		bool _randomNextLane = true;
		std::vector<std::string> _nextLaneEntityNames;
		size_t _sequentialNextLaneCursor = 0;
	};
}
