#include "SnowFootprintsComponent.hpp"
#include "../Entity.hpp"
#include "RigidBody.hpp"
#include "../../Physics/IRigidBody.hpp"
#include "../../Scene/Scene.hpp"
#include "../../HexEngine.hpp"
#include "../../GUI/Elements/ComponentWidget.hpp"
#include "../../GUI/Elements/DragFloat.hpp"
#include "../../GUI/Elements/Checkbox.hpp"

namespace HexEngine
{
	SnowFootprintsComponent::SnowFootprintsComponent(Entity* entity) :
		UpdateComponent(entity)
	{
	}

	SnowFootprintsComponent::SnowFootprintsComponent(Entity* entity, SnowFootprintsComponent* copy) :
		UpdateComponent(entity)
	{
		if (copy == nullptr)
			return;
		_enabled       = copy->_enabled;
		_strideLength  = copy->_strideLength;
		_footWidth     = copy->_footWidth;
		_footHalfLen   = copy->_footHalfLen;
		_footHalfWidth = copy->_footHalfWidth;
		_lifetime      = copy->_lifetime;
	}

	void SnowFootprintsComponent::Update(float frameTime)
	{
		(void)frameTime;
		if (!_enabled)
			return;

		Entity* e = GetEntity();
		if (e == nullptr)
			return;
		Scene* scene = e->GetScene();
		if (scene == nullptr)
			return;

		// No snow -> nothing to mark, and don't accumulate a stale prev-position.
		if (scene->GetWeatherSurfaceParams().snowCoverage <= 0.001f)
		{
			_havePrev = false;
			return;
		}

		// Ground-contact position + grounded state. Prefer the CCT capsule foot
		// (authoritative and exactly on the ground); fall back to the entity
		// transform for non-physics walkers. CCTs expose no velocity, so movement
		// is derived from the frame-to-frame position delta.
		bool grounded = true;
		math::Vector3 pos;
		if (RigidBody* rb = e->GetComponent<RigidBody>();
			rb != nullptr && rb->GetIRigidBody() != nullptr &&
			rb->GetIRigidBody()->IsCharacterController())
		{
			grounded = rb->GetIRigidBody()->IsOnGround();
			pos = rb->GetIRigidBody()->GetPhysicsPosition();
		}
		else
		{
			pos = e->GetWorldTM().Translation();
		}

		if (!_havePrev)
		{
			_prevPos = pos;
			_havePrev = true;
			return;
		}

		const math::Vector3 delta = pos - _prevPos;
		_prevPos = pos;

		const math::Vector2 planar(delta.x, delta.z);
		const float dist = planar.Length();

		// Airborne or effectively stationary: reset the cadence so the next
		// grounded step lands a print immediately (mirrors the footstep-cadence
		// priming in FirstPersonCameraController).
		if (!grounded || dist < 1e-5f)
		{
			_distanceSinceStep = _strideLength;
			return;
		}

		const math::Vector2 dir = planar / dist;
		_distanceSinceStep += dist;
		if (_distanceSinceStep < _strideLength)
			return;

		_distanceSinceStep = 0.0f;

		// Offset the print laterally to the side of the travel line so left and
		// right prints straddle the path.
		const math::Vector2 lat(dir.y, -dir.x);
		const float s = (_side > 0.5f) ? 1.0f : -1.0f;
		const math::Vector2 foot(
			pos.x + lat.x * s * _footWidth * 0.5f,
			pos.z + lat.y * s * _footWidth * 0.5f);

		const float now = (float)g_pEnv->_timeManager->GetTime();
		scene->GetSnowFootprints().Emit(foot, dir, _side, _footHalfLen, _footHalfWidth, _lifetime, now);
		_side = (_side > 0.5f) ? 0.0f : 1.0f;
	}

	void SnowFootprintsComponent::Serialize(json& data, JsonFile* file)
	{
		SERIALIZE_VALUE(_enabled);
		SERIALIZE_VALUE(_strideLength);
		SERIALIZE_VALUE(_footWidth);
		SERIALIZE_VALUE(_footHalfLen);
		SERIALIZE_VALUE(_footHalfWidth);
		SERIALIZE_VALUE(_lifetime);
	}

	void SnowFootprintsComponent::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		(void)mask;
		_serializationState = BaseComponent::SerializationState::Deserializing;
		DESERIALIZE_VALUE(_enabled);
		DESERIALIZE_VALUE(_strideLength);
		DESERIALIZE_VALUE(_footWidth);
		DESERIALIZE_VALUE(_footHalfLen);
		DESERIALIZE_VALUE(_footHalfWidth);
		DESERIALIZE_VALUE(_lifetime);
		_serializationState = BaseComponent::SerializationState::Ready;
	}

	bool SnowFootprintsComponent::CreateWidget(ComponentWidget* widget)
	{
		const int32_t fullWidth = widget->GetSize().x - 20;
		new Checkbox(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Enabled", &_enabled);
		new DragFloat(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Stride Length",  &_strideLength,  0.1f, 3.0f,  0.01f);
		new DragFloat(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Foot Width",     &_footWidth,     0.0f, 1.0f,  0.01f);
		new DragFloat(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Foot Half Len",  &_footHalfLen,   0.02f, 0.5f, 0.01f);
		new DragFloat(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Foot Half Width",&_footHalfWidth, 0.02f, 0.5f, 0.01f);
		new DragFloat(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Lifetime (s)",   &_lifetime,      1.0f, 300.0f, 1.0f);
		return true;
	}
}
