#include "TransitStopComponent.hpp"
#include <HexEngine.Core/Entity/Entity.hpp>
#include <HexEngine.Core/Graphics/DebugRenderer.hpp>
#include <HexEngine.Core/GUI/Elements/ComponentWidget.hpp>
#include <HexEngine.Core/GUI/Elements/DragFloat.hpp>
#include <HexEngine.Core/GUI/Elements/LineEdit.hpp>
#include <HexEngine.Core/GUI/Elements/EntitySearch.hpp>
#include <HexEngine.Core/Scene/Scene.hpp>
#include <algorithm>
#include <cmath>

TransitStopComponent::TransitStopComponent(HexEngine::Entity* entity) :
	BaseComponent(entity)
{
}

TransitStopComponent::TransitStopComponent(HexEngine::Entity* entity, TransitStopComponent* copy) :
	BaseComponent(entity)
{
	if (copy != nullptr)
	{
		_displayName = copy->_displayName;
		_laneWaypointName = copy->_laneWaypointName;
		_waitOffset = copy->_waitOffset;
		_waitRadius = copy->_waitRadius;
	}
}

const std::string& TransitStopComponent::GetDisplayName() const
{
	return _displayName.empty() ? GetEntity()->GetName() : _displayName;
}

math::Vector3 TransitStopComponent::GetWaitCentre() const
{
	return math::Vector3::Transform(_waitOffset, GetEntity()->GetWorldTM());
}

math::Vector3 TransitStopComponent::GetWaitSpot(uint32_t seed) const
{
	// Golden-angle spiral: spots spread evenly through the disc, no two agents
	// in the same place however many wait.
	const float n = (float)(seed % 24u);
	const float r = _waitRadius * std::sqrt((n + 0.5f) / 24.0f);
	const float a = n * 2.39996323f;
	math::Vector3 spot = GetWaitCentre();
	spot.x += r * std::cos(a);
	spot.z += r * std::sin(a);
	return spot;
}

std::vector<std::string> TransitStopComponent::GetLaneWaypointCandidates() const
{
	std::vector<std::string> out;
	size_t start = 0;
	while (start <= _laneWaypointName.size())
	{
		const size_t comma = _laneWaypointName.find(',', start);
		std::string token = _laneWaypointName.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
		const size_t b = token.find_first_not_of(" \t");
		if (b != std::string::npos)
			out.push_back(token.substr(b, token.find_last_not_of(" \t") - b + 1));
		if (comma == std::string::npos)
			break;
		start = comma + 1;
	}
	return out;
}

void TransitStopComponent::AddWaiting(const Waiting& waiting)
{
	RemoveWaiting(waiting.agentName);
	_waiting.push_back(waiting);
}

void TransitStopComponent::RemoveWaiting(const std::string& agentName)
{
	_waiting.erase(std::remove_if(_waiting.begin(), _waiting.end(),
		[&](const Waiting& w) { return w.agentName == agentName; }), _waiting.end());
}

std::vector<TransitStopComponent::Waiting> TransitStopComponent::TakeBoarders(const std::string& lineEntityName, int32_t maxCount, const std::function<bool(const std::string&)>& accept)
{
	std::vector<Waiting> out;
	for (auto it = _waiting.begin(); it != _waiting.end() && (int32_t)out.size() < maxCount;)
	{
		if (it->lineEntityName == lineEntityName && (!accept || accept(it->alightStopName)))
		{
			out.push_back(*it);
			it = _waiting.erase(it);
		}
		else
		{
			++it;
		}
	}
	return out;
}

void TransitStopComponent::OnRenderEditorGizmo(bool isSelected, bool& isHovering)
{
	(void)isHovering;
	if (!isSelected || HexEngine::g_pEnv == nullptr || HexEngine::g_pEnv->_debugRenderer == nullptr)
		return;
	const math::Vector3 c = GetWaitCentre();
	const math::Color colour(1.0f, 0.75f, 0.2f, 0.9f);
	constexpr int32_t kSegments = 24;
	for (int32_t i = 0; i < kSegments; ++i)
	{
		const float a0 = (float)i / kSegments * 6.2831853f, a1 = (float)(i + 1) / kSegments * 6.2831853f;
		HexEngine::g_pEnv->_debugRenderer->DrawLine(
			c + math::Vector3(std::cos(a0) * _waitRadius, 0.05f, std::sin(a0) * _waitRadius),
			c + math::Vector3(std::cos(a1) * _waitRadius, 0.05f, std::sin(a1) * _waitRadius), colour);
	}
	for (const auto& name : GetLaneWaypointCandidates())
	{
		if (auto* wp = GetEntity()->GetScene()->GetEntityByName(name); wp != nullptr)
			HexEngine::g_pEnv->_debugRenderer->DrawLine(c, wp->GetWorldTM().Translation(), math::Color(0.3f, 1.0f, 0.4f, 0.9f));
	}
}

void TransitStopComponent::Serialize(json& data, HexEngine::JsonFile* file)
{
	file->Serialize(data, "_displayName", _displayName);
	file->Serialize(data, "_laneWaypointName", _laneWaypointName);
	file->Serialize(data, "_waitOffset", _waitOffset);
	file->Serialize(data, "_waitRadius", _waitRadius);
}

void TransitStopComponent::Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask)
{
	(void)mask;
	file->Deserialize(data, "_displayName", _displayName);
	file->Deserialize(data, "_laneWaypointName", _laneWaypointName);
	file->Deserialize(data, "_waitOffset", _waitOffset);
	file->Deserialize(data, "_waitRadius", _waitRadius);
}

bool TransitStopComponent::CreateWidget(HexEngine::ComponentWidget* widget)
{
	using namespace HexEngine;
	const int32_t w = widget->GetSize().x - 20;
	auto* name = new LineEdit(widget, widget->GetNextPos(), Point(w, 18), L"Display Name");
	name->SetValue(std::wstring(_displayName.begin(), _displayName.end()));
	name->SetDoesCallbackWaitForReturn(false);
	name->SetOnInputFn([this](LineEdit*, const std::wstring& value) { _displayName = ws2s(value); });
	auto* wp = new EntitySearch(widget, widget->GetNextPos(), Point(w, 18), L"Bus Lane Waypoint");
	wp->SetValue(std::wstring(_laneWaypointName.begin(), _laneWaypointName.end()));
	wp->SetOnInputFn([this](EntitySearch*, const std::wstring& value) { _laneWaypointName = ws2s(value); });
	wp->SetOnSelectFn([this](EntitySearch*, const EntitySearchResult& result) { _laneWaypointName = result.entityName; });
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Wait Radius (m)", &_waitRadius, 0.5f, 20.0f, 0.1f, 1);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Wait Offset X", &_waitOffset.x, -20.0f, 20.0f, 0.05f, 2);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Wait Offset Z", &_waitOffset.z, -20.0f, 20.0f, 0.05f, 2);
	return true;
}
