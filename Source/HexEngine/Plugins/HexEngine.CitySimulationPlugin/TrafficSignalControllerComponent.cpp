#include "TrafficSignalControllerComponent.hpp"
#include <HexEngine.Core/Entity/Entity.hpp>
#include <HexEngine.Core/Entity/Component/NavMeshLinkComponent.hpp>
#include <HexEngine.Core/Graphics/DebugRenderer.hpp>
#include <HexEngine.Core/GUI/Elements/Checkbox.hpp>
#include <HexEngine.Core/GUI/Elements/ComponentWidget.hpp>
#include <HexEngine.Core/GUI/Elements/DragFloat.hpp>
#include <HexEngine.Core/GUI/Elements/LineEdit.hpp>
#include <HexEngine.Core/Scene/Scene.hpp>
#include <algorithm>
#include <cmath>
#include <sstream>

std::vector<TrafficSignalControllerComponent*> TrafficSignalControllerComponent::s_controllers;

namespace
{
	std::string TrimCopy(const std::string& text)
	{
		const auto begin = text.find_first_not_of(" \t\r\n");
		if (begin == std::string::npos)
			return {};
		const auto end = text.find_last_not_of(" \t\r\n");
		return text.substr(begin, end - begin + 1);
	}

	std::vector<std::string> SplitCsv(const std::string& csv)
	{
		std::vector<std::string> out;
		std::stringstream stream(csv);
		std::string token;
		while (std::getline(stream, token, ','))
		{
			token = TrimCopy(token);
			if (!token.empty())
				out.push_back(token);
		}
		return out;
	}

	bool LocalArmVector(char arm, math::Vector3& out)
	{
		switch (arm)
		{
		case 'N': case 'n': out = math::Vector3(0.0f, 0.0f, -1.0f); return true;
		case 'S': case 's': out = math::Vector3(0.0f, 0.0f, 1.0f); return true;
		case 'E': case 'e': out = math::Vector3(1.0f, 0.0f, 0.0f); return true;
		case 'W': case 'w': out = math::Vector3(-1.0f, 0.0f, 0.0f); return true;
		default: return false;
		}
	}

	float HorizontalDistanceSq(const math::Vector3& a, const math::Vector3& b)
	{
		const float dx = a.x - b.x;
		const float dz = a.z - b.z;
		return dx * dx + dz * dz;
	}
}

TrafficSignalControllerComponent::TrafficSignalControllerComponent(HexEngine::Entity* entity) :
	UpdateComponent(entity)
{
	RebuildArms();
	s_controllers.push_back(this);
}

TrafficSignalControllerComponent::TrafficSignalControllerComponent(HexEngine::Entity* entity, TrafficSignalControllerComponent* copy) :
	UpdateComponent(entity)
{
	if (copy != nullptr)
	{
		_armsCsv = copy->_armsCsv;
		_crossingsCsv = copy->_crossingsCsv;
		_stopLineDistance = copy->_stopLineDistance;
		_approachHalfWidth = copy->_approachHalfWidth;
		_greenSeconds = copy->_greenSeconds;
		_amberSeconds = copy->_amberSeconds;
		_allRedSeconds = copy->_allRedSeconds;
		_pedestrianWalkSeconds = copy->_pedestrianWalkSeconds;
		_pedestrianClearSeconds = copy->_pedestrianClearSeconds;
		_cycleOffsetSeconds = copy->_cycleOffsetSeconds;
		_drawDebug = copy->_drawDebug;
	}
	RebuildArms();
	s_controllers.push_back(this);
}

TrafficSignalControllerComponent::~TrafficSignalControllerComponent()
{
	s_controllers.erase(std::remove(s_controllers.begin(), s_controllers.end(), this), s_controllers.end());
}

void TrafficSignalControllerComponent::RebuildArms()
{
	_arms.clear();
	for (const auto& token : SplitCsv(_armsCsv))
	{
		math::Vector3 unused;
		if (LocalArmVector(token[0], unused))
			_arms.push_back((char)std::toupper((unsigned char)token[0]));
	}
	_crossingNames = SplitCsv(_crossingsCsv);
	_crossingsResolved = false;
}

void TrafficSignalControllerComponent::ResolveCrossings()
{
	_crossings.clear();
	auto* entity = GetEntity();
	auto* scene = entity != nullptr ? entity->GetScene() : nullptr;
	if (scene == nullptr)
		return;

	bool allFound = true;
	for (const auto& name : _crossingNames)
	{
		auto* linkEntity = scene->GetEntityByName(name);
		auto* link = linkEntity != nullptr ? linkEntity->GetComponent<HexEngine::NavMeshLinkComponent>() : nullptr;
		if (link == nullptr)
		{
			allFound = false;
			continue;
		}
		HexEngine::NavMeshLink world;
		link->GetWorldLink(world);
		_crossings.push_back({ world.start, world.end });
	}
	// Missing links (scene still streaming in) -> retry next frame.
	_crossingsResolved = allFound;
}

bool TrafficSignalControllerComponent::GetArmOutward(char arm, math::Vector3& outOutward) const
{
	math::Vector3 local;
	if (!LocalArmVector(arm, local))
		return false;
	auto* entity = GetEntity();
	if (entity == nullptr)
		return false;
	math::Vector3 world = math::Vector3::TransformNormal(local, entity->GetWorldTM());
	world.y = 0.0f;
	const float length = world.Length();
	if (length < 0.001f)
		return false;
	outOutward = world / length;
	return true;
}

float TrafficSignalControllerComponent::GetCycleLength() const
{
	const float perArm = std::max(_greenSeconds, 0.0f) + std::max(_amberSeconds, 0.0f) + std::max(_allRedSeconds, 0.0f);
	return std::max(0.1f, perArm * (float)_arms.size() + std::max(_pedestrianWalkSeconds, 0.0f) + std::max(_pedestrianClearSeconds, 0.0f));
}

float TrafficSignalControllerComponent::GetCycleTime() const
{
	const float length = GetCycleLength();
	float t = std::fmod(_clock + _cycleOffsetSeconds, length);
	if (t < 0.0f)
		t += length;
	return t;
}

TrafficSignalControllerComponent::SignalState TrafficSignalControllerComponent::GetArmState(char arm) const
{
	arm = (char)std::toupper((unsigned char)arm);
	const float perArm = std::max(_greenSeconds, 0.0f) + std::max(_amberSeconds, 0.0f) + std::max(_allRedSeconds, 0.0f);
	const float t = GetCycleTime();
	for (size_t i = 0; i < _arms.size(); ++i)
	{
		if (_arms[i] != arm)
			continue;
		const float local = t - perArm * (float)i;
		if (local >= 0.0f && local < _greenSeconds)
			return SignalState::Green;
		if (local >= _greenSeconds && local < _greenSeconds + _amberSeconds)
			return SignalState::Amber;
		return SignalState::Red;
	}
	return SignalState::Red;
}

bool TrafficSignalControllerComponent::IsPedestrianWalk() const
{
	const float perArm = std::max(_greenSeconds, 0.0f) + std::max(_amberSeconds, 0.0f) + std::max(_allRedSeconds, 0.0f);
	const float walkStart = perArm * (float)_arms.size();
	const float t = GetCycleTime();
	return t >= walkStart && t < walkStart + std::max(_pedestrianWalkSeconds, 0.0f);
}

void TrafficSignalControllerComponent::Update(float frameTime)
{
	UpdateComponent::Update(frameTime);

	// The cycle only runs in play; in the editor the junction holds its first
	// phase so heads show a stable, previewable state.
	if (HexEngine::g_pEnv != nullptr && HexEngine::g_pEnv->IsGameRunning() && frameTime > 0.0f)
		_clock += frameTime;
	else
		_clock = 0.0f;

	if (!_crossingsResolved && !_crossingNames.empty())
		ResolveCrossings();
}

float TrafficSignalControllerComponent::ComputeVehicleSpeedCap(const math::Vector3& position, const math::Vector3& heading,
	float currentSpeed, float maxSpeed, float maxDeceleration, bool& outHeldBySignal)
{
	outHeldBySignal = false;
	float cap = maxSpeed;

	math::Vector3 flatHeading(heading.x, 0.0f, heading.z);
	const float headingLength = flatHeading.Length();
	if (headingLength < 0.001f)
		return cap;
	flatHeading /= headingLength;

	// Comfortable braking for stops we plan; the vehicle's own (harder) brake
	// decides whether a red can still be stopped for at all.
	const float hardDecel = std::max(maxDeceleration, 0.5f);
	const float comfortDecel = std::min(hardDecel, 3.5f);
	const float speed = std::max(currentSpeed, 0.0f);
	constexpr float kStopMargin = 0.5f;
	constexpr float kLookAhead = 70.0f;

	for (auto* controller : s_controllers)
	{
		auto* entity = controller != nullptr ? controller->GetEntity() : nullptr;
		if (entity == nullptr || entity->IsPendingDeletion() || controller->_arms.empty())
			continue;

		const math::Vector3 centre = entity->GetWorldTM().Translation();
		if (std::abs(centre.y - position.y) > 4.0f)
			continue;
		const float reach = controller->_stopLineDistance + kLookAhead;
		if (HorizontalDistanceSq(centre, position) > reach * reach)
			continue;

		math::Vector3 rel = position - centre;
		rel.y = 0.0f;

		for (char arm : controller->_arms)
		{
			math::Vector3 outward;
			if (!controller->GetArmOutward(arm, outward))
				continue;

			const float along = rel.Dot(outward);
			if (along <= 0.0f)
				continue;
			const math::Vector3 lateral = rel - outward * along;
			if (lateral.LengthSquared() > controller->_approachHalfWidth * controller->_approachHalfWidth)
				continue;
			// Only traffic heading INTO the junction on this arm.
			if (flatHeading.Dot(-outward) < 0.6f)
				continue;

			const float distanceToLine = along - controller->_stopLineDistance;
			if (distanceToLine < -kStopMargin || distanceToLine > kLookAhead)
				continue;

			const SignalState state = controller->GetArmState(arm);
			if (state == SignalState::Green)
				continue;

			const float stoppingDistance = speed * speed / (2.0f * comfortDecel);
			// Amber: stop only if we comfortably can, otherwise clear the junction.
			if (state == SignalState::Amber && stoppingDistance > distanceToLine)
				continue;
			// Red but physically unable to stop before the line (committed on
			// amber): carry on rather than stopping in the junction mouth.
			if (state == SignalState::Red && speed * speed / (2.0f * hardDecel) > distanceToLine + 1.0f)
				continue;

			const float room = distanceToLine - kStopMargin;
			const float allowed = room <= 0.0f ? 0.0f : std::sqrt(2.0f * comfortDecel * room);
			if (allowed < cap)
			{
				cap = allowed;
				outHeldBySignal = true;
			}
		}
	}

	return cap;
}

bool TrafficSignalControllerComponent::IsPedestrianStepAllowed(const math::Vector3& from, const math::Vector3& to)
{
	// "About to step onto a crossing" = standing at one end of it with the
	// next path node at the other end (the navmesh link puts both ends in the
	// path). Agents already part-way across are never stopped.
	constexpr float kAtEndSq = 2.0f * 2.0f;
	constexpr float kNextEndSq = 2.5f * 2.5f;

	for (auto* controller : s_controllers)
	{
		auto* entity = controller != nullptr ? controller->GetEntity() : nullptr;
		if (entity == nullptr || entity->IsPendingDeletion() || controller->_crossings.empty())
			continue;

		const math::Vector3 centre = entity->GetWorldTM().Translation();
		const float reach = controller->_stopLineDistance + 6.0f;
		if (HorizontalDistanceSq(centre, from) > reach * reach)
			continue;

		for (const auto& crossing : controller->_crossings)
		{
			const bool startsAtA = HorizontalDistanceSq(from, crossing.a) <= kAtEndSq && HorizontalDistanceSq(to, crossing.b) <= kNextEndSq;
			const bool startsAtB = HorizontalDistanceSq(from, crossing.b) <= kAtEndSq && HorizontalDistanceSq(to, crossing.a) <= kNextEndSq;
			if (startsAtA || startsAtB)
				return controller->IsPedestrianWalk();
		}
	}
	return true;
}

void TrafficSignalControllerComponent::OnDebugRender()
{
	if (!_drawDebug || HexEngine::g_pEnv == nullptr || !HexEngine::g_pEnv->IsEditorMode())
		return;
	auto* entity = GetEntity();
	if (entity == nullptr)
		return;

	const math::Vector3 centre = entity->GetWorldTM().Translation() + math::Vector3(0.0f, 0.3f, 0.0f);
	for (char arm : _arms)
	{
		math::Vector3 outward;
		if (!GetArmOutward(arm, outward))
			continue;
		const math::Vector3 across(-outward.z, 0.0f, outward.x);
		const math::Vector3 lineCentre = centre + outward * _stopLineDistance;
		math::Color colour(0.9f, 0.1f, 0.1f, 1.0f);
		switch (GetArmState(arm))
		{
		case SignalState::Green: colour = math::Color(0.1f, 0.9f, 0.2f, 1.0f); break;
		case SignalState::Amber: colour = math::Color(1.0f, 0.6f, 0.0f, 1.0f); break;
		default: break;
		}
		HexEngine::g_pEnv->_debugRenderer->DrawLine(lineCentre - across * _approachHalfWidth, lineCentre + across * _approachHalfWidth, colour);
		HexEngine::g_pEnv->_debugRenderer->DrawLine(centre, lineCentre, colour);
	}

	const math::Color walkColour = IsPedestrianWalk() ? math::Color(1.0f, 1.0f, 1.0f, 1.0f) : math::Color(0.6f, 0.1f, 0.1f, 1.0f);
	for (const auto& crossing : _crossings)
		HexEngine::g_pEnv->_debugRenderer->DrawLine(crossing.a, crossing.b, walkColour);
}

void TrafficSignalControllerComponent::Serialize(json& data, HexEngine::JsonFile* file)
{
	file->Serialize(data, "_armsCsv", _armsCsv);
	file->Serialize(data, "_crossingsCsv", _crossingsCsv);
	file->Serialize(data, "_stopLineDistance", _stopLineDistance);
	file->Serialize(data, "_approachHalfWidth", _approachHalfWidth);
	file->Serialize(data, "_greenSeconds", _greenSeconds);
	file->Serialize(data, "_amberSeconds", _amberSeconds);
	file->Serialize(data, "_allRedSeconds", _allRedSeconds);
	file->Serialize(data, "_pedestrianWalkSeconds", _pedestrianWalkSeconds);
	file->Serialize(data, "_pedestrianClearSeconds", _pedestrianClearSeconds);
	file->Serialize(data, "_cycleOffsetSeconds", _cycleOffsetSeconds);
	file->Serialize(data, "_drawDebug", _drawDebug);
}

void TrafficSignalControllerComponent::Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask)
{
	(void)mask;
	file->Deserialize(data, "_armsCsv", _armsCsv);
	file->Deserialize(data, "_crossingsCsv", _crossingsCsv);
	file->Deserialize(data, "_stopLineDistance", _stopLineDistance);
	file->Deserialize(data, "_approachHalfWidth", _approachHalfWidth);
	file->Deserialize(data, "_greenSeconds", _greenSeconds);
	file->Deserialize(data, "_amberSeconds", _amberSeconds);
	file->Deserialize(data, "_allRedSeconds", _allRedSeconds);
	file->Deserialize(data, "_pedestrianWalkSeconds", _pedestrianWalkSeconds);
	file->Deserialize(data, "_pedestrianClearSeconds", _pedestrianClearSeconds);
	file->Deserialize(data, "_cycleOffsetSeconds", _cycleOffsetSeconds);
	file->Deserialize(data, "_drawDebug", _drawDebug);
	RebuildArms();
}

bool TrafficSignalControllerComponent::CreateWidget(HexEngine::ComponentWidget* widget)
{
	const int32_t w = widget->GetSize().x - 20;

	auto* arms = new HexEngine::LineEdit(widget, widget->GetNextPos(), HexEngine::Point(w, 18), L"Arms / phase order (CSV of N,E,S,W)");
	arms->SetValue(std::wstring(_armsCsv.begin(), _armsCsv.end()));
	arms->SetDoesCallbackWaitForReturn(false);
	arms->SetOnInputFn([this](HexEngine::LineEdit*, const std::wstring& value)
		{
			_armsCsv = ws2s(value);
			RebuildArms();
		});

	auto* crossings = new HexEngine::LineEdit(widget, widget->GetNextPos(), HexEngine::Point(w, 18), L"Controlled Crossings (CSV of link entities)");
	crossings->SetValue(std::wstring(_crossingsCsv.begin(), _crossingsCsv.end()));
	crossings->SetDoesCallbackWaitForReturn(false);
	crossings->SetOnInputFn([this](HexEngine::LineEdit*, const std::wstring& value)
		{
			_crossingsCsv = ws2s(value);
			RebuildArms();
		});

	new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(w, 18), L"Stop Line Distance", &_stopLineDistance, 0.0f, 60.0f, 0.1f, 2);
	new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(w, 18), L"Approach Half Width", &_approachHalfWidth, 0.5f, 20.0f, 0.1f, 2);
	new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(w, 18), L"Green (s)", &_greenSeconds, 1.0f, 120.0f, 0.5f, 1);
	new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(w, 18), L"Amber (s)", &_amberSeconds, 0.0f, 10.0f, 0.1f, 1);
	new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(w, 18), L"All Red (s)", &_allRedSeconds, 0.0f, 10.0f, 0.1f, 1);
	new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(w, 18), L"Pedestrian Walk (s)", &_pedestrianWalkSeconds, 0.0f, 60.0f, 0.5f, 1);
	new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(w, 18), L"Pedestrian Clear (s)", &_pedestrianClearSeconds, 0.0f, 30.0f, 0.5f, 1);
	new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(w, 18), L"Cycle Offset (s)", &_cycleOffsetSeconds, 0.0f, 300.0f, 0.5f, 1);
	new HexEngine::Checkbox(widget, widget->GetNextPos(), HexEngine::Point(w, 18), L"Draw Debug", &_drawDebug);
	return true;
}
