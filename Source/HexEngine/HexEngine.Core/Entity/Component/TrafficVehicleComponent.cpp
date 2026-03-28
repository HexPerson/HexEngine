#include "TrafficVehicleComponent.hpp"
#include "TrafficLaneComponent.hpp"
#include "Transform.hpp"
#include "../Entity.hpp"
#include "../../GUI/Elements/Button.hpp"
#include "../../GUI/Elements/Checkbox.hpp"
#include "../../GUI/Elements/ComponentWidget.hpp"
#include "../../GUI/Elements/DragFloat.hpp"
#include "../../GUI/Elements/LineEdit.hpp"
#include "../../Scene/Scene.hpp"
#include "../../Graphics/DebugRenderer.hpp"
#include <algorithm>
#include <cmath>

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

	TrafficVehicleComponent::TrafficVehicleComponent(Entity* entity) :
		UpdateComponent(entity)
	{
	}

	TrafficVehicleComponent::TrafficVehicleComponent(Entity* entity, TrafficVehicleComponent* copy) :
		UpdateComponent(entity, copy)
	{
		if (copy != nullptr)
		{
			_laneEntityName = copy->_laneEntityName;
			_targetIndex = copy->_targetIndex;
			_speed = copy->_speed;
			_acceleration = copy->_acceleration;
			_rotationLerp = copy->_rotationLerp;
			_arrivalDistance = copy->_arrivalDistance;
			_currentSpeed = 0.0f;
			_useLaneSpeedLimit = copy->_useLaneSpeedLimit;
			_invertDirection = copy->_invertDirection;
			_drawDebug = copy->_drawDebug;
		}
	}

	void TrafficVehicleComponent::SetLaneEntityName(const std::string& laneEntityName)
	{
		_laneEntityName = laneEntityName;
		RestartPath();
	}

void TrafficVehicleComponent::RestartPath()
{
	if (_invertDirection)
	{
		std::vector<math::Vector3> points;
		if (GatherLanePoints(points) && !points.empty())
		{
			_targetIndex = points.size() - 1;
		}
		else
		{
			_targetIndex = 0;
		}
	}
	else
	{
		_targetIndex = 0;
	}

	_currentSpeed = 0.0f;
}

	TrafficLaneComponent* TrafficVehicleComponent::ResolveLane()
	{
		auto* entity = GetEntity();
		if (entity == nullptr)
			return nullptr;

		auto* scene = entity->GetScene();
		if (scene == nullptr)
			return nullptr;

		if (_laneEntityName.empty())
			return nullptr;

		auto* laneEntity = scene->GetEntityByName(_laneEntityName);
		if (laneEntity == nullptr || laneEntity->IsPendingDeletion())
			return nullptr;

		return laneEntity->GetComponent<TrafficLaneComponent>();
	}

	bool TrafficVehicleComponent::GatherLanePoints(std::vector<math::Vector3>& outPoints)
	{
		auto* lane = ResolveLane();
		if (lane == nullptr)
			return false;

		lane->GatherLanePoints(outPoints);
		return outPoints.size() >= 2;
	}

	void TrafficVehicleComponent::AdvanceTargetIndex(size_t numPoints)
	{
		auto* lane = ResolveLane();
		if (lane == nullptr || numPoints < 2)
		{
			_targetIndex = 0;
			return;
		}

		if (_invertDirection)
		{
			if (_targetIndex == 0)
			{
				_targetIndex = lane->IsLooping() ? numPoints - 1 : 0;
			}
			else
			{
				--_targetIndex;
			}
		}
		else
		{
			++_targetIndex;
			if (_targetIndex >= numPoints)
			{
				_targetIndex = lane->IsLooping() ? 0 : (numPoints - 1);
			}
		}
	}

	void TrafficVehicleComponent::Update(float frameTime)
	{
		UpdateComponent::Update(frameTime);

		if (frameTime <= 0.0f)
			return;

		auto* transform = GetEntity()->GetComponent<Transform>();
		if (transform == nullptr)
			return;

		std::vector<math::Vector3> points;
		if (!GatherLanePoints(points))
			return;

		_targetIndex = std::min(_targetIndex, points.size() - 1);
		math::Vector3 targetPoint = points[_targetIndex];
		math::Vector3 currentPosition = transform->GetPosition();

		math::Vector3 toTarget = targetPoint - currentPosition;
		float distanceToTarget = toTarget.Length();

		float arrivalDistance = std::max(_arrivalDistance, 1.0f);
		if (distanceToTarget <= arrivalDistance)
		{
			AdvanceTargetIndex(points.size());
			targetPoint = points[_targetIndex];
			toTarget = targetPoint - currentPosition;
			distanceToTarget = toTarget.Length();
		}

		if (distanceToTarget <= 0.001f)
			return;

		toTarget.Normalize();

		auto* lane = ResolveLane();
		const float laneSpeed = lane != nullptr ? lane->GetSpeedLimit() : _speed;
		const float maxSpeed = std::max(_useLaneSpeedLimit ? laneSpeed : _speed, 0.0f);

		if (maxSpeed <= 0.0f)
		{
			_currentSpeed = 0.0f;
			return;
		}

		const float accel = std::max(_acceleration, 0.0f);
		if (_currentSpeed < maxSpeed)
		{
			_currentSpeed = std::min(_currentSpeed + accel * frameTime, maxSpeed);
		}
		else
		{
			_currentSpeed = std::max(_currentSpeed - accel * frameTime, maxSpeed);
		}

		currentPosition += toTarget * (_currentSpeed * frameTime);
		transform->SetPosition(currentPosition);

		float yaw = atan2f(toTarget.x, toTarget.z);
		const auto targetRotation = math::Quaternion::CreateFromYawPitchRoll(yaw, 0.0f, 0.0f);
		const float rotationBlend = std::clamp(frameTime * std::max(_rotationLerp, 0.0f), 0.0f, 1.0f);
		transform->SetRotation(math::Quaternion::Slerp(transform->GetRotation(), targetRotation, rotationBlend));
	}

	void TrafficVehicleComponent::Serialize(json& data, JsonFile* file)
	{
		file->Serialize(data, "_laneEntityName", _laneEntityName);
		file->Serialize(data, "_speed", _speed);
		file->Serialize(data, "_acceleration", _acceleration);
		file->Serialize(data, "_rotationLerp", _rotationLerp);
		file->Serialize(data, "_arrivalDistance", _arrivalDistance);
		file->Serialize(data, "_useLaneSpeedLimit", _useLaneSpeedLimit);
		file->Serialize(data, "_invertDirection", _invertDirection);
		file->Serialize(data, "_drawDebug", _drawDebug);
	}

	void TrafficVehicleComponent::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		file->Deserialize(data, "_laneEntityName", _laneEntityName);
		file->Deserialize(data, "_speed", _speed);
		file->Deserialize(data, "_acceleration", _acceleration);
		file->Deserialize(data, "_rotationLerp", _rotationLerp);
		file->Deserialize(data, "_arrivalDistance", _arrivalDistance);
		file->Deserialize(data, "_useLaneSpeedLimit", _useLaneSpeedLimit);
		file->Deserialize(data, "_invertDirection", _invertDirection);
		file->Deserialize(data, "_drawDebug", _drawDebug);
		RestartPath();
	}

	bool TrafficVehicleComponent::CreateWidget(ComponentWidget* widget)
	{
		auto* laneName = new LineEdit(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Lane Entity");
		laneName->SetValue(std::wstring(_laneEntityName.begin(), _laneEntityName.end()));
		laneName->SetOnInputFn([this](LineEdit* edit, const std::wstring& value)
		{
			_laneEntityName = std::string(value.begin(), value.end());
			RestartPath();
		});
		laneName->SetPrefabOverrideBinding(GetComponentName(), "/_laneEntityName");

		new Button(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Use Parent Lane", [this, laneName](Button* button) -> bool
		{
			auto* parent = GetEntity() != nullptr ? GetEntity()->GetParent() : nullptr;
			if (parent == nullptr || parent->GetComponent<TrafficLaneComponent>() == nullptr)
				return false;

			_laneEntityName = parent->GetName();
			laneName->SetValue(std::wstring(_laneEntityName.begin(), _laneEntityName.end()));
			RestartPath();
			return true;
		});

		auto* speed = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Speed", &_speed, 0.0f, 5000.0f, 1.0f, 1);
		auto* acceleration = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Acceleration", &_acceleration, 0.0f, 10000.0f, 1.0f, 1);
		auto* rotationLerp = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Rotation Lerp", &_rotationLerp, 0.0f, 100.0f, 0.1f, 2);
		auto* arrivalDistance = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Arrival Distance", &_arrivalDistance, 1.0f, 500.0f, 1.0f, 1);
		auto* useLaneSpeed = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Use Lane Speed", &_useLaneSpeedLimit);
		auto* invertDirection = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Invert Direction", &_invertDirection);
		auto* drawDebug = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Draw Debug", &_drawDebug);

		speed->SetPrefabOverrideBinding(GetComponentName(), "/_speed");
		acceleration->SetPrefabOverrideBinding(GetComponentName(), "/_acceleration");
		rotationLerp->SetPrefabOverrideBinding(GetComponentName(), "/_rotationLerp");
		arrivalDistance->SetPrefabOverrideBinding(GetComponentName(), "/_arrivalDistance");
		useLaneSpeed->SetPrefabOverrideBinding(GetComponentName(), "/_useLaneSpeedLimit");
		invertDirection->SetPrefabOverrideBinding(GetComponentName(), "/_invertDirection");
		drawDebug->SetPrefabOverrideBinding(GetComponentName(), "/_drawDebug");

		return true;
	}

	void TrafficVehicleComponent::OnRenderEditorGizmo(bool isSelected, bool& isHovering)
	{
		if (!g_pEnv->IsEditorMode())
			return;

		if (!_drawDebug && !isSelected)
			return;

		std::vector<math::Vector3> points;
		if (!GatherLanePoints(points) || points.empty())
			return;

		_targetIndex = std::min(_targetIndex, points.size() - 1);
		const math::Vector3 from = GetEntity()->GetWorldTM().Translation();
		const math::Vector3 to = points[_targetIndex];

		g_pEnv->_debugRenderer->DrawLine(from, to, math::Color(HEX_RGB_TO_FLOAT3(52, 152, 219), 1.0f));
		DrawPointMarker(to, 8.0f, math::Color(HEX_RGB_TO_FLOAT3(52, 152, 219), 1.0f));
	}
}
