#include "TransitPortalComponent.hpp"
#include <HexEngine.Core/Entity/Entity.hpp>
#include <HexEngine.Core/Entity/Component/Transform.hpp>
#include <HexEngine.Core/Entity/Component/Camera.hpp>
#include <HexEngine.Core/Entity/Component/RigidBody.hpp>
#include <HexEngine.Core/Entity/Component/FirstPersonCameraController.hpp>
#include <HexEngine.Core/Physics/IRigidBody.hpp>
#include <HexEngine.Core/Graphics/DebugRenderer.hpp>
#include <HexEngine.Core/GUI/Elements/ComponentWidget.hpp>
#include <HexEngine.Core/GUI/Elements/DragFloat.hpp>
#include <HexEngine.Core/GUI/Elements/EntitySearch.hpp>
#include <HexEngine.Core/Scene/Scene.hpp>
#include <cmath>

TransitPortalComponent::TransitPortalComponent(HexEngine::Entity* entity) :
	UpdateComponent(entity)
{
}

TransitPortalComponent::TransitPortalComponent(HexEngine::Entity* entity, TransitPortalComponent* copy) :
	UpdateComponent(entity)
{
	if (copy != nullptr)
	{
		_targetEntityName = copy->_targetEntityName;
		_radius = copy->_radius;
		_heightTolerance = copy->_heightTolerance;
	}
}

void TransitPortalComponent::Update(float frameTime)
{
	UpdateComponent::Update(frameTime);
	if (HexEngine::g_pEnv == nullptr || !HexEngine::g_pEnv->IsGameRunning())
	{
		_armed = true;
		return;
	}
	auto* scene = GetEntity()->GetScene();
	auto* camera = scene != nullptr ? scene->GetMainCamera() : nullptr;
	auto* player = camera != nullptr ? camera->GetEntity() : nullptr;
	if (player == nullptr || player->GetComponent<HexEngine::FirstPersonCameraController>() == nullptr)
		return;

	const math::Vector3 here = GetEntity()->GetWorldTM().Translation();
	const math::Vector3 p = player->GetWorldTM().Translation();
	math::Vector3 d = p - here;
	const bool inside = std::fabs(d.y) < _heightTolerance && (d.x * d.x + d.z * d.z) < _radius * _radius;
	if (!inside)
	{
		_armed = true;
		return;
	}
	if (!_armed)
		return;

	auto* target = scene->GetEntityByName(_targetEntityName);
	if (target == nullptr)
		return;
	const math::Vector3 to = target->GetWorldTM().Translation();

	// The play-mode player is a character controller: move the capsule, then the transform.
	if (auto* rb = player->GetComponent<HexEngine::RigidBody>(); rb != nullptr && rb->GetIRigidBody() != nullptr)
		rb->GetIRigidBody()->UpdatePosePosition(to);
	if (auto* tf = player->GetComponent<HexEngine::Transform>())
	{
		tf->SetPosition(to);
		tf->SnapInterpolation();
	}
	// Face the way the target points (its +Z).
	const math::Matrix tm = target->GetWorldTM();
	const math::Vector3 fwd(tm._31, tm._32, tm._33);
	if (fwd.x * fwd.x + fwd.z * fwd.z > 1e-4f)
		camera->SetYaw(std::atan2(-fwd.x, -fwd.z) * 57.2957795f);

	_armed = false;
	if (auto* other = target->GetComponent<TransitPortalComponent>())
		other->Disarm();
	LOG_INFO("TransitPortal '%s': player -> '%s'", GetEntity()->GetName().c_str(), _targetEntityName.c_str());
}

void TransitPortalComponent::OnRenderEditorGizmo(bool isSelected, bool& isHovering)
{
	(void)isHovering;
	if (!isSelected || HexEngine::g_pEnv == nullptr || HexEngine::g_pEnv->_debugRenderer == nullptr)
		return;
	const math::Vector3 c = GetEntity()->GetWorldTM().Translation();
	for (int32_t i = 0; i < 24; ++i)
	{
		const float a0 = i / 24.0f * 6.2831853f, a1 = (i + 1) / 24.0f * 6.2831853f;
		HexEngine::g_pEnv->_debugRenderer->DrawLine(c + math::Vector3(std::cos(a0) * _radius, 0.05f, std::sin(a0) * _radius),
			c + math::Vector3(std::cos(a1) * _radius, 0.05f, std::sin(a1) * _radius), math::Color(0.6f, 0.4f, 1.0f, 0.9f));
	}
	if (auto* target = GetEntity()->GetScene()->GetEntityByName(_targetEntityName))
		HexEngine::g_pEnv->_debugRenderer->DrawLine(c, target->GetWorldTM().Translation(), math::Color(0.6f, 0.4f, 1.0f, 0.6f));
}

void TransitPortalComponent::Serialize(json& data, HexEngine::JsonFile* file)
{
	file->Serialize(data, "_targetEntityName", _targetEntityName);
	file->Serialize(data, "_radius", _radius);
	file->Serialize(data, "_heightTolerance", _heightTolerance);
}

void TransitPortalComponent::Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask)
{
	(void)mask;
	file->Deserialize(data, "_targetEntityName", _targetEntityName);
	file->Deserialize(data, "_radius", _radius);
	file->Deserialize(data, "_heightTolerance", _heightTolerance);
}

bool TransitPortalComponent::CreateWidget(HexEngine::ComponentWidget* widget)
{
	using namespace HexEngine;
	const int32_t w = widget->GetSize().x - 20;
	auto* target = new EntitySearch(widget, widget->GetNextPos(), Point(w, 18), L"Target (arrival point)");
	target->SetValue(std::wstring(_targetEntityName.begin(), _targetEntityName.end()));
	target->SetOnInputFn([this](EntitySearch*, const std::wstring& value) { _targetEntityName = ws2s(value); });
	target->SetOnSelectFn([this](EntitySearch*, const EntitySearchResult& result) { _targetEntityName = result.entityName; });
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Radius (m)", &_radius, 0.3f, 10.0f, 0.05f, 2);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Height Tolerance (m)", &_heightTolerance, 0.3f, 10.0f, 0.05f, 2);
	return true;
}
