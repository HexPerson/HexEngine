#include "TrafficLaneComponent.hpp"
#include "../Entity.hpp"
#include "../../GUI/Elements/Button.hpp"
#include "../../GUI/Elements/Checkbox.hpp"
#include "../../GUI/Elements/ComponentWidget.hpp"
#include "../../GUI/Elements/DragFloat.hpp"
#include "../../Scene/Scene.hpp"
#include "../../Graphics/DebugRenderer.hpp"
#include <format>

namespace HexEngine
{
	namespace
	{
		void DrawPointMarker(const math::Vector3& position, float extent, const math::Color& colour)
		{
			dx::BoundingBox marker;
			marker.Center = position;
			marker.Extents = math::Vector3(extent);
			g_pEnv->_debugRenderer->DrawAABB(marker, colour);
		}
	}

	TrafficLaneComponent::TrafficLaneComponent(Entity* entity) :
		BaseComponent(entity)
	{
	}

	TrafficLaneComponent::TrafficLaneComponent(Entity* entity, TrafficLaneComponent* copy) :
		BaseComponent(entity)
	{
		if (copy != nullptr)
		{
			_loop = copy->_loop;
			_speedLimit = copy->_speedLimit;
			_drawDebug = copy->_drawDebug;
		}
	}

	void TrafficLaneComponent::Serialize(json& data, JsonFile* file)
	{
		file->Serialize(data, "_loop", _loop);
		file->Serialize(data, "_speedLimit", _speedLimit);
		file->Serialize(data, "_drawDebug", _drawDebug);
	}

	void TrafficLaneComponent::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		file->Deserialize(data, "_loop", _loop);
		file->Deserialize(data, "_speedLimit", _speedLimit);
		file->Deserialize(data, "_drawDebug", _drawDebug);
	}

	void TrafficLaneComponent::GatherLanePoints(std::vector<math::Vector3>& outPoints) const
	{
		outPoints.clear();

		auto* entity = GetEntity();
		if (entity == nullptr)
			return;

		const auto& children = entity->GetChildren();
		outPoints.reserve(children.size());
		for (auto* child : children)
		{
			if (child == nullptr || child->IsPendingDeletion())
				continue;

			outPoints.push_back(child->GetWorldTM().Translation());
		}
	}

	bool TrafficLaneComponent::CreateWidget(ComponentWidget* widget)
	{
		auto* loop = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Loop Lane", &_loop);
		auto* speedLimit = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Speed Limit", &_speedLimit, 0.0f, 5000.0f, 1.0f, 1);
		auto* drawDebug = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Draw Debug", &_drawDebug);

		loop->SetPrefabOverrideBinding(GetComponentName(), "/_loop");
		speedLimit->SetPrefabOverrideBinding(GetComponentName(), "/_speedLimit");
		drawDebug->SetPrefabOverrideBinding(GetComponentName(), "/_drawDebug");

		new Button(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Add Waypoint Child", [this](Button* button) -> bool
		{
			auto* laneEntity = GetEntity();
			if (laneEntity == nullptr || laneEntity->GetScene() == nullptr)
				return false;

			const uint32_t waypointIndex = static_cast<uint32_t>(laneEntity->GetChildren().size()) + 1;
			auto* waypoint = laneEntity->GetScene()->CreateEntity(std::format("Waypoint{}", waypointIndex), math::Vector3::Zero);
			if (waypoint == nullptr)
				return false;

			waypoint->SetParent(laneEntity);
			waypoint->ForcePosition(math::Vector3(0.0f, 0.0f, waypointIndex * 250.0f));
			waypoint->SetFlag(EntityFlags::ExcludeFromHLOD);
			return true;
		});

		return true;
	}

	void TrafficLaneComponent::OnRenderEditorGizmo(bool isSelected, bool& isHovering)
	{
		if (!g_pEnv->IsEditorMode())
			return;

		if (!_drawDebug && !isSelected)
			return;

		std::vector<math::Vector3> points;
		GatherLanePoints(points);

		if (points.size() < 2)
			return;

		const math::Color lineColour = isSelected ? math::Color(HEX_RGB_TO_FLOAT3(255, 201, 14), 1.0f) : math::Color(HEX_RGB_TO_FLOAT3(255, 170, 0), 0.8f);
		const math::Color pointColour = math::Color(HEX_RGB_TO_FLOAT3(46, 204, 113), 1.0f);

		for (size_t i = 0; i + 1 < points.size(); ++i)
		{
			g_pEnv->_debugRenderer->DrawLine(points[i], points[i + 1], lineColour);
			DrawPointMarker(points[i], 6.0f, pointColour);
		}

		DrawPointMarker(points.back(), 6.0f, pointColour);

		if (_loop && points.size() > 2)
		{
			g_pEnv->_debugRenderer->DrawLine(points.back(), points.front(), lineColour);
		}
	}
}
