#include "TrafficLaneComponent.hpp"
#include <HexEngine.Core/Entity/Entity.hpp>
#include <HexEngine.Core/GUI/Elements/ArrayElement.hpp>
#include <HexEngine.Core/Graphics/DebugRenderer.hpp>
#include <HexEngine.Core/GUI/Elements/Button.hpp>
#include <HexEngine.Core/GUI/Elements/Checkbox.hpp>
#include <HexEngine.Core/GUI/Elements/ComponentWidget.hpp>
#include <HexEngine.Core/GUI/Elements/DragFloat.hpp>
#include <HexEngine.Core/GUI/Elements/EntitySearch.hpp>
#include <HexEngine.Core/Math/FloatMath.hpp>
#include <HexEngine.Core/Scene/Scene.hpp>
#include <algorithm>
#include <format>
#include <sstream>

namespace
{
	void DrawPointMarker(const math::Vector3& position, float extent, const math::Color& colour)
	{
		dx::BoundingBox marker;
		marker.Center = position;
		marker.Extents = math::Vector3(extent);
		HexEngine::g_pEnv->_debugRenderer->DrawAABB(marker, colour);
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

TrafficLaneComponent::TrafficLaneComponent(HexEngine::Entity* entity) :
	BaseComponent(entity)
{
}

TrafficLaneComponent::TrafficLaneComponent(HexEngine::Entity* entity, TrafficLaneComponent* copy) :
	BaseComponent(entity)
{
	if (copy != nullptr)
	{
		_loop = copy->_loop;
		_speedLimit = copy->_speedLimit;
		_drawDebug = copy->_drawDebug;
		_nextLaneEntityNames = copy->_nextLaneEntityNames;
		_sequentialNextLaneCursor = 0;
		_branchOffset = copy->_branchOffset;
	}
}

void TrafficLaneComponent::Serialize(json& data, HexEngine::JsonFile* file)
{
	file->Serialize(data, "_loop", _loop);
	file->Serialize(data, "_speedLimit", _speedLimit);
	file->Serialize(data, "_drawDebug", _drawDebug);
	file->Serialize(data, "_nextLaneEntityNames", _nextLaneEntityNames);
	file->Serialize(data, "_branchOffset", _branchOffset);
}

void TrafficLaneComponent::Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask)
{
	file->Deserialize(data, "_loop", _loop);
	file->Deserialize(data, "_speedLimit", _speedLimit);
	file->Deserialize(data, "_drawDebug", _drawDebug);
	file->Deserialize(data, "_nextLaneEntityNames", _nextLaneEntityNames);
	file->Deserialize(data, "_branchOffset", _branchOffset);
	_sequentialNextLaneCursor = 0;
}

void TrafficLaneComponent::GatherLaneWaypointEntities(std::vector<HexEngine::Entity*>& outWaypoints) const
{
	outWaypoints.clear();

	auto* entity = GetEntity();
	if (entity == nullptr || entity->IsPendingDeletion())
		return;

	// Lane graph is explicitly authored via "Next Lanes". A lane contributes exactly one node.
	outWaypoints.push_back(entity);
}

void TrafficLaneComponent::GatherLanePoints(std::vector<math::Vector3>& outPoints) const
{
	outPoints.clear();

	std::vector<HexEngine::Entity*> waypoints;
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

	const size_t index = _sequentialNextLaneCursor % _nextLaneEntityNames.size();
	++_sequentialNextLaneCursor;
	return _nextLaneEntityNames[index];
}

bool TrafficLaneComponent::AddNextLaneEntityName(const std::string& laneEntityName)
{
	const std::string trimmed = Trim(laneEntityName);
	if (trimmed.empty())
		return false;

	if (std::find(_nextLaneEntityNames.begin(), _nextLaneEntityNames.end(), trimmed) != _nextLaneEntityNames.end())
		return false;

	_nextLaneEntityNames.push_back(trimmed);
	return true;
}

bool TrafficLaneComponent::CreateWidget(HexEngine::ComponentWidget* widget)
{
	auto* loop = new HexEngine::Checkbox(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Loop Lane", &_loop);
	auto* speedLimit = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Speed Limit", &_speedLimit, 0.0f, 5000.0f, 0.1f, 2);
	auto* drawDebug = new HexEngine::Checkbox(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Draw Debug", &_drawDebug);

	new HexEngine::ArrayElement<std::string>(
		widget,
		widget->GetNextPos(),
		HexEngine::Point(widget->GetSize().x - 20, 132),
		L"Next Lanes",
		_nextLaneEntityNames,
		[](HexEngine::Element* parent, std::string& item, int32_t index)
		{
			auto* laneSearch = new HexEngine::EntitySearch(parent, HexEngine::Point(0, 0), HexEngine::Point(parent->GetSize().x, 22), L"Lane");
			laneSearch->SetValue(std::wstring(item.begin(), item.end()));
			laneSearch->SetOnInputFn([&item](HexEngine::EntitySearch* search, const std::wstring& value)
				{
					item = std::string(value.begin(), value.end());
				});
			laneSearch->SetOnSelectFn([&item](HexEngine::EntitySearch* search, const HexEngine::EntitySearchResult& result)
				{
					item = result.entityName;
				});
		},
		[]() -> std::string
		{
			return std::string();
		},
		[](const std::string& item, int32_t index) -> int32_t
		{
			return 34;
		},
		[](const std::string& item, int32_t index) -> std::wstring
		{
			return std::format(L"Lane {}", index + 1);
		});

	loop->SetPrefabOverrideBinding(GetComponentName(), "/_loop");
	speedLimit->SetPrefabOverrideBinding(GetComponentName(), "/_speedLimit");
	drawDebug->SetPrefabOverrideBinding(GetComponentName(), "/_drawDebug");

	auto* branchOffsetX = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Branch Offset X", &_branchOffset.x, -10000.0f, 10000.0f, 0.1f, 2);
	auto* branchOffsetY = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Branch Offset Y", &_branchOffset.y, -10000.0f, 10000.0f, 0.1f, 2);
	auto* branchOffsetZ = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Branch Offset Z", &_branchOffset.z, -10000.0f, 10000.0f, 0.1f, 2);

	branchOffsetX->SetPrefabOverrideBinding(GetComponentName(), "/_branchOffset/x");
	branchOffsetY->SetPrefabOverrideBinding(GetComponentName(), "/_branchOffset/y");
	branchOffsetZ->SetPrefabOverrideBinding(GetComponentName(), "/_branchOffset/z");

	new HexEngine::Button(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Add Child Lane", [this](HexEngine::Button* button) -> bool
		{
			auto* laneEntity = GetEntity();
			if (laneEntity == nullptr || laneEntity->GetScene() == nullptr)
				return false;

			HexEngine::Entity* previousWaypoint = nullptr;
			for (auto* child : laneEntity->GetChildren())
			{
				if (child == nullptr || child->IsPendingDeletion())
					continue;
				previousWaypoint = child;
			}

			const uint32_t waypointIndex = static_cast<uint32_t>(laneEntity->GetChildren().size()) + 1;
			auto* waypoint = laneEntity->GetScene()->CreateEntity(std::format("Lane{}", waypointIndex), math::Vector3::Zero);
			if (waypoint == nullptr)
				return false;

			waypoint->SetParent(laneEntity);
			math::Vector3 waypointPosition = laneEntity->GetWorldTM().Translation() + math::Vector3(0.0f, 0.0f, waypointIndex * 5.0f);
			if (previousWaypoint != nullptr && !previousWaypoint->IsPendingDeletion())
			{
				waypointPosition = previousWaypoint->GetWorldTM().Translation() + math::Vector3(0.0f, 0.0f, 5.0f);
			}
			waypoint->ForcePosition(waypointPosition);
			waypoint->SetFlag(HexEngine::EntityFlags::ExcludeFromHLOD);
			waypoint->AddComponent<TrafficLaneComponent>();

			AddNextLaneEntityName(waypoint->GetName());
			return true;
		});

	new HexEngine::Button(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Create Branch Lane", [this](HexEngine::Button* button) -> bool
		{
			auto* laneEntity = GetEntity();
			auto* scene = laneEntity != nullptr ? laneEntity->GetScene() : nullptr;
			if (laneEntity == nullptr || scene == nullptr)
				return false;

			HexEngine::Entity* sourceWaypoint = GetEntity();

			const math::Vector3 startPos = sourceWaypoint->GetWorldTM().Translation();
			const math::Vector3 endPos = startPos + _branchOffset;

			auto* branchLane = scene->CreateEntity("BranchLane", startPos);
			if (branchLane == nullptr)
				return false;

			branchLane->SetParent(sourceWaypoint);
			branchLane->AddComponent<TrafficLaneComponent>();
			branchLane->SetFlag(HexEngine::EntityFlags::ExcludeFromHLOD);


			AddNextLaneEntityName(branchLane->GetName());

			return true;
		});

	return true;
}

void TrafficLaneComponent::OnRenderEditorGizmo(bool isSelected, bool& isHovering)
{
	if (!HexEngine::g_pEnv->IsEditorMode())
		return;

	if (!_drawDebug && !isSelected)
		return;

	std::vector<math::Vector3> points;
	GatherLanePoints(points);

	if (points.empty())
		return;

	const math::Color lineColour = isSelected ? math::Color(HEX_RGB_TO_FLOAT3(255, 201, 14), 1.0f) : math::Color(HEX_RGB_TO_FLOAT3(255, 170, 0), 0.8f);
	const math::Color pointColour = math::Color(HEX_RGB_TO_FLOAT3(46, 204, 113), 1.0f);
	const math::Color nextLaneColour = math::Color(HEX_RGBA_TO_FLOAT4(52, 152, 219, 200));

	for (size_t i = 0; i + 1 < points.size(); ++i)
	{
		HexEngine::g_pEnv->_debugRenderer->DrawLine(points[i], points[i + 1], lineColour);
		DrawPointMarker(points[i], 0.3f, pointColour);
	}

	DrawPointMarker(points.back(), 0.3f, pointColour);

	if (_loop && points.size() > 2)
	{
		HexEngine::g_pEnv->_debugRenderer->DrawLine(points.back(), points.front(), lineColour);
	}

	auto* entity = GetEntity();
	auto* scene = entity != nullptr ? entity->GetScene() : nullptr;
	if (scene == nullptr)
		return;

	const math::Vector3 from = entity->GetWorldTM().Translation();
	for (const auto& nextName : _nextLaneEntityNames)
	{
		if (nextName.empty())
			continue;

		auto* next = scene->GetEntityByName(nextName);
		if (next == nullptr || next->IsPendingDeletion())
			continue;

		HexEngine::g_pEnv->_debugRenderer->DrawLine(from, next->GetWorldTM().Translation(), nextLaneColour);
	}
}

void TrafficLaneComponent::OnDebugRender()
{
	if (!HexEngine::g_pEnv->IsEditorMode() || !_drawDebug)
		return;
	if (GetEntity() != nullptr && GetEntity()->HasFlag(HexEngine::EntityFlags::SelectedInEditor))
		return;

	bool hovering = false;
	OnRenderEditorGizmo(false, hovering);
}