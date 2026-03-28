#pragma once

#include "BaseComponent.hpp"
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

		void GatherLanePoints(std::vector<math::Vector3>& outPoints) const;
		bool IsLooping() const { return _loop; }
		float GetSpeedLimit() const { return _speedLimit; }
		bool GetDrawDebug() const { return _drawDebug; }

	private:
		bool _loop = true;
		float _speedLimit = 450.0f;
		bool _drawDebug = true;
	};
}
