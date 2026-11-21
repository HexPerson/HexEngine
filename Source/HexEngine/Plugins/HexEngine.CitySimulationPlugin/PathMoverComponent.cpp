#include "PathMoverComponent.hpp"
#include <HexEngine.Core/Entity/Entity.hpp>
#include <HexEngine.Core/Entity/Component/Transform.hpp>
#include <HexEngine.Core/Entity/Component/StaticMeshComponent.hpp>
#include <HexEngine.Core/Graphics/DebugRenderer.hpp>
#include <HexEngine.Core/GUI/Elements/Checkbox.hpp>
#include <HexEngine.Core/GUI/Elements/ComponentWidget.hpp>
#include <HexEngine.Core/GUI/Elements/DragFloat.hpp>
#include <HexEngine.Core/GUI/Elements/EntitySearch.hpp>
#include <HexEngine.Core/Scene/Scene.hpp>
#include <algorithm>
#include <cmath>

namespace
{
	constexpr float kDegToRad = 0.0174532925f;

	float WrapAngle(float a)
	{
		while (a > 3.14159265f) a -= 6.28318531f;
		while (a < -3.14159265f) a += 6.28318531f;
		return a;
	}
}

PathMoverComponent::PathMoverComponent(HexEngine::Entity* entity) :
	UpdateComponent(entity)
{
}

PathMoverComponent::PathMoverComponent(HexEngine::Entity* entity, PathMoverComponent* copy) :
	UpdateComponent(entity)
{
	if (copy != nullptr)
	{
		_pathEntityName = copy->_pathEntityName;
		_speed = copy->_speed;
		_closedLoop = copy->_closedLoop;
		_startFraction = copy->_startFraction;
		_yawOffsetDegrees = copy->_yawOffsetDegrees;
		_turnRate = copy->_turnRate;
		_lookAhead = copy->_lookAhead;
		_bobAmplitude = copy->_bobAmplitude;
		_bobFrequency = copy->_bobFrequency;
		_rollAmplitude = copy->_rollAmplitude;
		_previewInEditor = copy->_previewInEditor;
	}
}

bool PathMoverComponent::RebuildPath()
{
	_points.clear();
	_cumulative.clear();
	_length = 0.0f;
	auto* entity = GetEntity();
	auto* scene = entity != nullptr ? entity->GetScene() : nullptr;
	auto* path = (scene != nullptr && !_pathEntityName.empty()) ? scene->GetEntityByName(_pathEntityName) : nullptr;
	if (path == nullptr)
		return false;

	std::vector<HexEngine::Entity*> waypoints;
	for (auto* child : path->GetChildren())
	{
		if (child != nullptr && !child->IsPendingDeletion())
			waypoints.push_back(child);
	}
	std::sort(waypoints.begin(), waypoints.end(), [](HexEngine::Entity* a, HexEngine::Entity* b) { return a->GetName() < b->GetName(); });
	for (auto* wp : waypoints)
		_points.push_back(wp->GetWorldTM().Translation());
	if (_points.size() < 2)
	{
		_points.clear();
		return false;
	}

	_cumulative.push_back(0.0f);
	for (size_t i = 1; i < _points.size(); ++i)
		_cumulative.push_back(_cumulative.back() + math::Vector3::Distance(_points[i - 1], _points[i]));
	if (_closedLoop)
		_cumulative.push_back(_cumulative.back() + math::Vector3::Distance(_points.back(), _points.front()));
	_length = _cumulative.back();
	return _length > 0.01f;
}

math::Vector3 PathMoverComponent::SampleAt(float d) const
{
	if (_points.empty())
		return math::Vector3::Zero;
	if (_closedLoop)
		d = std::fmod(std::fmod(d, _length) + _length, _length);
	else
		d = std::clamp(d, 0.0f, _length);
	const size_t segments = _closedLoop ? _points.size() : _points.size() - 1;
	for (size_t i = 0; i < segments; ++i)
	{
		if (d <= _cumulative[i + 1] || i + 1 == segments)
		{
			const float seg = std::max(1e-4f, _cumulative[i + 1] - _cumulative[i]);
			const float t = std::clamp((d - _cumulative[i]) / seg, 0.0f, 1.0f);
			const math::Vector3& a = _points[i];
			const math::Vector3& b = _points[(i + 1) % _points.size()];
			return a + (b - a) * t;
		}
	}
	return _points.back();
}

void PathMoverComponent::Update(float frameTime)
{
	UpdateComponent::Update(frameTime);

	const bool running = HexEngine::g_pEnv != nullptr &&
		(HexEngine::g_pEnv->IsGameRunning() || (_previewInEditor && HexEngine::g_pEnv->IsEditorMode()));
	if (!running)
	{
		_started = false;   // re-read the path (and the start fraction) next run
		return;
	}
	auto* entity = GetEntity();
	auto* tf = entity != nullptr ? entity->GetComponent<HexEngine::Transform>() : nullptr;
	if (tf == nullptr)
		return;

	const float dt = std::clamp(frameTime, 0.0f, 0.1f);
	if (!_started)
	{
		if (!RebuildPath())
			return;
		_distance = std::clamp(_startFraction, 0.0f, 1.0f) * _length;
		_direction = 1.0f;
		const math::Vector3 a = SampleAt(_distance), b = SampleAt(_distance + std::max(0.5f, _lookAhead));
		_yaw = std::atan2(b.x - a.x, b.z - a.z);
		_time = _startFraction * 37.0f;   // de-sync the bob between movers
		// A mesh that moves every frame would re-voxelise into the GI each frame
		// (the "GI revision thrash" trap) - keep the mover and its parts out of it.
		std::vector<HexEngine::Entity*> stack{ entity };
		while (!stack.empty())
		{
			auto* e = stack.back(); stack.pop_back();
			if (auto* mesh = e->GetComponent<HexEngine::StaticMeshComponent>(); mesh != nullptr)
				mesh->SetExcludeFromGI(true);
			for (auto* child : e->GetChildren())
				if (child != nullptr) stack.push_back(child);
		}
		_started = true;
	}

	_time += dt;
	_distance += _speed * dt * _direction;
	if (!_closedLoop && (_distance >= _length || _distance <= 0.0f))
	{
		_direction = -_direction;
		_distance = std::clamp(_distance, 0.0f, _length);
	}

	const math::Vector3 pos = SampleAt(_distance);
	const math::Vector3 ahead = SampleAt(_distance + std::max(0.5f, _lookAhead) * _direction);
	const math::Vector3 dir = ahead - pos;
	if (dir.x * dir.x + dir.z * dir.z > 1e-6f)
	{
		const float target = std::atan2(dir.x, dir.z);
		_yaw += WrapAngle(target - _yaw) * std::clamp(_turnRate * dt, 0.0f, 1.0f);
	}

	const float phase = _time * _bobFrequency * 6.28318531f;
	const float bob = _bobAmplitude * std::sin(phase);
	const float roll = _rollAmplitude * kDegToRad * std::sin(phase * 0.73f + 1.3f);
	const math::Quaternion rotation =
		math::Quaternion::CreateFromYawPitchRoll(_yaw + _yawOffsetDegrees * kDegToRad, 0.0f, roll);
	math::Vector3 world = pos + math::Vector3(0.0f, bob, 0.0f);

	// The path is in world space; express it in the parent's frame if parented.
	if (auto* parent = entity->GetParent(); parent != nullptr)
	{
		world = math::Vector3::Transform(world, parent->GetWorldTM().Invert());
	}
	tf->SetPosition(world);
	tf->SetRotation(rotation);
}

void PathMoverComponent::OnRenderEditorGizmo(bool isSelected, bool& isHovering)
{
	(void)isHovering;
	if (!isSelected || HexEngine::g_pEnv == nullptr || !HexEngine::g_pEnv->IsEditorMode() || HexEngine::g_pEnv->_debugRenderer == nullptr)
		return;
	if (!RebuildPath())
		return;
	const math::Color colour(0.3f, 0.9f, 1.0f, 0.9f);
	for (size_t i = 0; i + 1 < _points.size(); ++i)
		HexEngine::g_pEnv->_debugRenderer->DrawLine(_points[i], _points[i + 1], colour);
	if (_closedLoop)
		HexEngine::g_pEnv->_debugRenderer->DrawLine(_points.back(), _points.front(), colour);
	_started = false;
}

void PathMoverComponent::Serialize(json& data, HexEngine::JsonFile* file)
{
	file->Serialize(data, "_pathEntityName", _pathEntityName);
	file->Serialize(data, "_speed", _speed);
	file->Serialize(data, "_closedLoop", _closedLoop);
	file->Serialize(data, "_startFraction", _startFraction);
	file->Serialize(data, "_yawOffsetDegrees", _yawOffsetDegrees);
	file->Serialize(data, "_turnRate", _turnRate);
	file->Serialize(data, "_lookAhead", _lookAhead);
	file->Serialize(data, "_bobAmplitude", _bobAmplitude);
	file->Serialize(data, "_bobFrequency", _bobFrequency);
	file->Serialize(data, "_rollAmplitude", _rollAmplitude);
	file->Serialize(data, "_previewInEditor", _previewInEditor);
}

void PathMoverComponent::Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask)
{
	(void)mask;
	file->Deserialize(data, "_pathEntityName", _pathEntityName);
	file->Deserialize(data, "_speed", _speed);
	file->Deserialize(data, "_closedLoop", _closedLoop);
	file->Deserialize(data, "_startFraction", _startFraction);
	file->Deserialize(data, "_yawOffsetDegrees", _yawOffsetDegrees);
	file->Deserialize(data, "_turnRate", _turnRate);
	file->Deserialize(data, "_lookAhead", _lookAhead);
	file->Deserialize(data, "_bobAmplitude", _bobAmplitude);
	file->Deserialize(data, "_bobFrequency", _bobFrequency);
	file->Deserialize(data, "_rollAmplitude", _rollAmplitude);
	file->Deserialize(data, "_previewInEditor", _previewInEditor);
	_started = false;
}

bool PathMoverComponent::CreateWidget(HexEngine::ComponentWidget* widget)
{
	using namespace HexEngine;
	const int32_t w = widget->GetSize().x - 20;
	auto* path = new EntitySearch(widget, widget->GetNextPos(), Point(w, 18), L"Path Entity (children = waypoints)");
	path->SetValue(std::wstring(_pathEntityName.begin(), _pathEntityName.end()));
	path->SetOnInputFn([this](EntitySearch*, const std::wstring& value) { _pathEntityName = ws2s(value); _started = false; });
	path->SetOnSelectFn([this](EntitySearch*, const EntitySearchResult& result) { _pathEntityName = result.entityName; _started = false; });
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Speed (m/s)", &_speed, 0.0f, 100.0f, 0.1f, 2);
	new Checkbox(widget, widget->GetNextPos(), Point(w, 18), L"Closed Loop (else ping-pong)", &_closedLoop);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Start Fraction", &_startFraction, 0.0f, 1.0f, 0.01f, 2);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Yaw Offset (deg)", &_yawOffsetDegrees, -360.0f, 360.0f, 1.0f, 1);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Turn Rate", &_turnRate, 0.05f, 20.0f, 0.05f, 2);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Look Ahead (m)", &_lookAhead, 0.5f, 100.0f, 0.5f, 1);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Bob Amplitude (m)", &_bobAmplitude, 0.0f, 3.0f, 0.01f, 2);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Bob Frequency (Hz)", &_bobFrequency, 0.0f, 3.0f, 0.01f, 2);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Roll Amplitude (deg)", &_rollAmplitude, 0.0f, 30.0f, 0.1f, 1);
	new Checkbox(widget, widget->GetNextPos(), Point(w, 18), L"Preview in editor", &_previewInEditor);
	return true;
}
