#pragma once

#include "UpdateComponent.hpp"
#include <string>
#include <vector>

namespace HexEngine
{
	class TrafficLaneComponent;

	class HEX_API TrafficVehicleComponent : public UpdateComponent
	{
	public:
		CREATE_COMPONENT_ID(TrafficVehicleComponent);
		DEFINE_COMPONENT_CTOR(TrafficVehicleComponent);

		virtual void Update(float frameTime) override;
		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;
		virtual bool CreateWidget(ComponentWidget* widget) override;
		virtual void OnRenderEditorGizmo(bool isSelected, bool& isHovering) override;

		void SetLaneEntityName(const std::string& laneEntityName);
		const std::string& GetLaneEntityName() const { return _laneEntityName; }
		void RestartPath();

	private:
		TrafficLaneComponent* ResolveLane();
		bool GatherLanePoints(std::vector<math::Vector3>& outPoints);
		void AdvanceTargetIndex(size_t numPoints);

	private:
		std::string _laneEntityName;
		size_t _targetIndex = 0;
		float _speed = 350.0f;
		float _acceleration = 600.0f;
		float _rotationLerp = 9.0f;
		float _arrivalDistance = 35.0f;
		float _currentSpeed = 0.0f;
		bool _useLaneSpeedLimit = true;
		bool _invertDirection = false;
		bool _drawDebug = true;
	};
}
