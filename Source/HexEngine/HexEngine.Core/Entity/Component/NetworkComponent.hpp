
#pragma once

#include "UpdateComponent.hpp"

namespace HexEngine
{
	/**
	 * @brief Marks an entity for network replication.
	 *
	 * Host-authoritative (v1): on the host the component is a passive tag - the
	 * NetworkReplicationSystem reads the entity's transform and broadcasts it. On
	 * a client the same entity is a "remote proxy": its transform is driven by
	 * incoming snapshots and smoothed here in Update() (engine interpolation is
	 * bypassed via the Transform NoNotify writes to avoid double-smoothing and
	 * per-frame TransformChanged churn).
	 *
	 * Identity: every networked entity has a stable cross-machine net id. If
	 * _explicitNetworkId is non-zero it is used verbatim (authored, or assigned by
	 * the host for runtime-spawned prefab instances); otherwise the id is derived
	 * from a CRC32 of the entity name, so authored entities present in the same
	 * scene on both peers match automatically with zero authoring.
	 *
	 * Only _explicitNetworkId and _syncFlags are serialized - runtime state
	 * (owner, snapshot buffer, proxy flag) is never saved.
	 */
	class HEX_API NetworkComponent : public UpdateComponent
	{
	public:
		CREATE_COMPONENT_ID(NetworkComponent);
		DEFINE_COMPONENT_CTOR(NetworkComponent);

		enum SyncFlags : uint32_t
		{
			SyncNone      = 0,
			SyncTransform = 1 << 0, // replicate position + rotation
		};

		virtual ~NetworkComponent();

		virtual void Update(float dt) override;

		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;

		// Stable cross-machine id: explicit if set, else CRC32 of the entity name.
		uint32_t GetEffectiveNetId() const;

		uint32_t GetExplicitNetId() const { return _explicitNetworkId; }
		void     SetExplicitNetId(uint32_t id) { _explicitNetworkId = id; }

		uint32_t GetSyncFlags() const { return _syncFlags; }
		void     SetSyncFlags(uint32_t flags) { _syncFlags = flags; }

		uint32_t GetOwnerConnId() const { return _ownerConnId; }
		void     SetOwnerConnId(uint32_t connId) { _ownerConnId = connId; }

		// Called by the replication system when a snapshot for this entity arrives
		// (client side). The transform is interpolated toward it in Update().
		void ReceiveSnapshot(const math::Vector3& pos, const math::Quaternion& rot);

	private:
		uint32_t _explicitNetworkId = 0;            // serialized (0 => derive from name)
		uint32_t _syncFlags         = SyncTransform; // serialized
		uint32_t _ownerConnId       = 0;             // runtime (0 = host-owned)

		// Remote-proxy interpolation target (client side only).
		bool             _hasTarget = false;
		bool             _spawnSnap = true; // snap (no lerp) the first time we apply
		math::Vector3    _targetPos;
		math::Quaternion _targetRot;
	};
}
