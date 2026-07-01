
#include "NetworkComponent.hpp"
#include "Transform.hpp"
#include "../Entity.hpp"
#include "../../Network/INetworkSystem.hpp"
#include "../../Utility/CRC32.hpp"
#include "../../Environment/IEnvironment.hpp"

#include <algorithm>

namespace HexEngine
{
	// Render-rate smoothing factor for remote proxies. Snapshots arrive at the
	// network rate (~20 Hz); we lerp the visible transform toward the latest one.
	static constexpr float kProxyInterpRate = 12.0f;

	NetworkComponent::NetworkComponent(Entity* entity) :
		UpdateComponent(entity)
	{
	}

	NetworkComponent::NetworkComponent(Entity* entity, NetworkComponent* copy) :
		UpdateComponent(entity, copy)
	{
		// Carry authored config across prefab spawn / duplicate (clone path doesn't
		// deserialize). Leave _explicitNetworkId at 0 so the host assigns a fresh
		// runtime id to spawned instances (a copied id would collide).
		if (copy != nullptr)
		{
			_syncFlags = copy->_syncFlags;
		}
	}

	NetworkComponent::~NetworkComponent()
	{
	}

	uint32_t NetworkComponent::GetEffectiveNetId() const
	{
		if (_explicitNetworkId != 0)
			return _explicitNetworkId;

		Entity* e = GetEntity();
		if (e == nullptr)
			return 0;

		const std::string& name = e->GetName();
		if (name.empty())
			return 0;

		return (uint32_t)CRC32::HashString(name.c_str());
	}

	void NetworkComponent::ReceiveSnapshot(const math::Vector3& pos, const math::Quaternion& rot)
	{
		_targetPos = pos;
		_targetRot = rot;
		_hasTarget = true;
	}

	void NetworkComponent::Update(float dt)
	{
		// Only client-side remote proxies smooth toward received snapshots. On the
		// host (or with no networking) the entity is driven by normal game logic
		// and this component is a passive tag.
		INetworkSystem* net = (g_pEnv != nullptr) ? g_pEnv->_networkSystem : nullptr;
		if (net == nullptr || net->GetRole() != INetworkSystem::NetRole::Client)
			return;

		if ((_syncFlags & SyncTransform) == 0 || !_hasTarget)
			return;

		Transform* tf = GetEntity()->GetComponent<Transform>();
		if (tf == nullptr)
			return;

		if (_spawnSnap)
		{
			// First snapshot: place exactly and collapse interpolation so the proxy
			// doesn't visibly lerp from its old/spawn position to here.
			tf->SetPositionNoNotify(_targetPos);
			tf->SetRotationNoNotify(_targetRot);
			tf->SnapInterpolation();
			_spawnSnap = false;
			return;
		}

		const float a = std::clamp(dt * kProxyInterpRate, 0.0f, 1.0f);
		const math::Vector3 p = math::Vector3::Lerp(tf->GetPosition(TransformState::Current), _targetPos, a);
		const math::Quaternion q = math::Quaternion::Slerp(tf->GetRotation(TransformState::Current), _targetRot, a);
		tf->SetPositionNoNotify(p);
		tf->SetRotationNoNotify(q);
	}

	void NetworkComponent::Serialize(json& data, JsonFile* file)
	{
		SERIALIZE_VALUE(_explicitNetworkId);
		SERIALIZE_VALUE(_syncFlags);
	}

	void NetworkComponent::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		(void)mask;
		DESERIALIZE_VALUE(_explicitNetworkId);
		DESERIALIZE_VALUE(_syncFlags);
	}
}
