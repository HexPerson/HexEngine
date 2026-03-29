#include "TrafficLaneComponent.hpp"
#include "../Entity.hpp"
#include "../../Graphics/DebugRenderer.hpp"
#include "../../GUI/Elements/Button.hpp"
#include "../../GUI/Elements/Checkbox.hpp"
#include "../../GUI/Elements/ComponentWidget.hpp"
#include "../../GUI/Elements/DragFloat.hpp"
#include "../../GUI/Elements/LineEdit.hpp"
#include "../../Math/FloatMath.hpp"
#include "../../Scene/Scene.hpp"
#include <algorithm>
#include <format>
#include <sstream>

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

		std::string Trim(const std::string& text)
		{
			const auto begin = text.find_first_not_of(" \t\r\n");
			if (begin == std::string::npos)
				return {};

			const auto end = text.find_last_not_of(" \t\r\n");
			return text.substr(begin, end - begin + 1);
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
			_randomNextLane = copy->_randomNextLane;
			_nextLaneEntityNames = copy->_nextLaneEntityNames;
			_sequentialNextLaneCursor = 0;
		}
	}

	void TrafficLaneComponent::Serialize(json& data, JsonFile* file)
	{
		file->Serialize(data, "_loop", _loop);
		file->Serialize(data, "_speedLimit", _speedLimit);
		file->Serialize(data, "_drawDebug", _drawDebug);
		file->Serialize(data, "_randomNextLane", _randomNextLane);
		file->Serialize(data, "_nextLaneEntityNames", _nextLaneEntityNames);
	}

	void TrafficLaneComponent::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		file->Deserialize(data, "_loop", _loop);
		file->Deserialize(data, "_speedLimit", _speedLimit);
		file->Deserialize(data, "_drawDebug", _drawDebug);
		file->Deserialize(data, "_randomNextLane", _randomNextLane);
		file->Deserialize(data, "_nextLaneEntityNames", _nextLaneEntityNames);
		_sequentialNextLaneCursor = 0;
	}

	void TrafficLaneComponent::GatherLaneWaypointEntities(std::vector<Entity*>& outWaypoints) const
	{
		outWaypoints.clear();

		auto* entity = GetEntity();
		if (entity == nullptr)
			return;

		const auto& children = entity->GetChildren();
		outWaypoints.reserve(children.size());
		for (auto* child : children)
		{
			if (child == nullptr || child->IsPendingDeletion())
				continue;

			outWaypoints.push_back(child);
		}
	}

	void TrafficLaneComponent::GatherLanePoints(std::vector<math::Vector3>& outPoints) const
	{
		outPoints.clear();

		std::vector<Entity*> waypoints;
		GatherLaneWaypointEntities(waypoints);
		outPoints.reserve(waypoints.size());
		for (auto* child : waypoints)
		{
			outPoints.push_back(child->GetWorldTM().Translation());
		}
	}

	void TrafficLaneComponent::SetNextLaneNamesFromCsv(const std::string& csv)
	{
		_nextLaneEntityNames.clear();

		std::stringstream stream(csv);
		std::string token;
		while (std::getline(stream, token, ','))
		{
			token = Trim(token);
			if (!token.empty())
				_nextLaneEntityNames.push_back(token);
		}

		_sequentialNextLaneCursor = 0;
	}

	std::string TrafficLaneComponent::GetNextLaneNamesCsv() const
	{
		std::string csv;
		for (size_t i = 0; i < _nextLaneEntityNames.size(); ++i)
		{
			csv += _nextLaneEntityNames[i];
			if (i + 1 < _nextLaneEntityNames.size())
				csv += ", ";
		}
		return csv;
	}

	std::string TrafficLaneComponent::GetNextLaneEntityName()
	{
		if (_nextLaneEntityNames.empty())
			return {};

		if (_randomNextLane)
		{
			const int32_t index = GetRandomInt(0, static_cast<int32_t>(_nextLaneEntityNames.size()) - 1);
			return _nextLaneEntityNames[static_cast<size_t>(index)];
		}

		const size_t index = _sequentialNextLaneCursor % _nextLaneEntityNames.size();
		++_sequentialNextLaneCursor;
		return _nextLaneEntityNames[index];
	}

	bool TrafficLaneComponent::CreateWidget(ComponentWidget* widget)
	{
		auto* loop = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Loop Lane", &_loop);
		auto* speedLimit = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Speed Limit", &_speedLimit, 0.0f, 5000.0f, 0.1f, 2);
		auto* drawDebug = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Draw Debug", &_drawDebug);
		auto* randomNext = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Random Next Lane", &_randomNextLane);

		auto* nextLaneNames = new LineEdit(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Next Lanes (CSV)");
		const std::string nextLanesCsv = GetNextLaneNamesCsv();
		nextLaneNames->SetValue(std::wstring(nextLanesCsv.begin(), nextLanesCsv.end()));
		nextLaneNames->SetDoesCallbackWaitForReturn(false);
		nextLaneNames->SetOnInputFn([this](LineEdit* edit, const std::wstring& value)
		{
			SetNextLaneNamesFromCsv(std::string(value.begin(), value.end()));
		});

		loop->SetPrefabOverrideBinding(GetComponentName(), "/_loop");
		speedLimit->SetPrefabOverrideBinding(GetComponentName(), "/_speedLimit");
		drawDebug->SetPrefabOverrideBinding(GetComponentName(), "/_drawDebug");
		randomNext->SetPrefabOverrideBinding(GetComponentName(), "/_randomNextLane");
		nextLaneNames->SetPrefabOverrideBinding(GetComponentName(), "/_nextLaneEntityNames");

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
			waypoint->ForcePosition(math::Vector3(0.0f, 0.0f, waypointIndex * 5.0f));
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
			DrawPointMarker(points[i], 0.3f, pointColour);
		}

		DrawPointMarker(points.back(), 0.3f, pointColour);

		if (_loop && points.size() > 2)
		{
			g_pEnv->_debugRenderer->DrawLine(points.back(), points.front(), lineColour);
		}
	}
}
