
#pragma once

#include "../Required.hpp"
#include "NetworkMessages.hpp"

#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>

namespace HexEngine
{
	class Scene;
	class Entity;
	class Camera;
	class BaseComponent;
	class NetworkComponent;

	/**
	 * @brief Host-authoritative entity replication over g_pEnv->_networkSystem.
	 *
	 * Owned by Scene. Decoupled from the transport: it serializes to/from raw
	 * byte buffers and only ever calls the abstract INetworkSystem, so the
	 * GameNetworkingSockets plugin is swappable and core carries no GNS types.
	 *
	 * v1 model: the host is authoritative for every networked entity. Authored
	 * entities present in the same scene on both peers are matched by a stable
	 * net id (NetworkComponent::GetEffectiveNetId) with zero authoring; the host
	 * broadcasts their transforms and clients render them as smoothed proxies.
	 * Prefab instances created at runtime are spawned/despawned across the wire
	 * (reliable), with a baseline burst sent to each newly-connected client.
	 */
	class HEX_API NetworkReplicationSystem
	{
	public:
		explicit NetworkReplicationSystem(Scene* scene);
		~NetworkReplicationSystem();

		// Receive + apply inbound messages/events. Call once per frame right after
		// g_pEnv->_networkSystem->Tick(), OUTSIDE the scene update lock.
		void Pump(float dt);

		// Host: broadcast transform snapshots (throttled to the network rate). Call
		// once per frame after the scene has finished updating, OUTSIDE the lock.
		void SendUpdates(float dt);

		// Host API: spawn a prefab across the network (instantiated on the host and
		// on every client) / despawn it. Returns the assigned net id (0 on failure).
		uint32_t SpawnNetworked(const std::string& prefabPath, const math::Vector3& pos, const math::Quaternion& rot);
		void     DespawnNetworked(uint32_t netId);

		// Player prefab spawned per connection (host's own + one per client). Must
		// carry a CharacterController (RigidBody CCT) + NetworkComponent +
		// NetworkPlayerComponent. Empty (default) disables auto player spawning.
		void SetPlayerPrefabPath(const std::string& path) { _playerPrefabPath = path; }
		const std::string& GetPlayerPrefabPath() const { return _playerPrefabPath; }

		// The netId of THIS peer's own player (client: assigned by the host via
		// LocalPlayerAssigned; host: its own player). 0 if none. NetworkPlayerComponent
		// uses this to decide whether to predict (owner) or interpolate (remote).
		uint32_t GetLocalPlayerNetId() const { return _localPlayerNetId; }

		// Client -> host: send this player's recent input commands (unreliable,
		// redundant). Called by the owning NetworkPlayerComponent each tick.
		void SendPlayerInput(uint32_t netId, const NetInputCmd* cmds, uint32_t count);

	private:
		void PollControlVars();
		void HandleMessage(uint32_t connId, const uint8_t* data, uint32_t size);
		void SendBaselineTo(uint32_t connId);
		void RebuildIndex();
		void SerializeSpawn(uint32_t netId, Entity* root, const std::string& prefabPath, std::vector<uint8_t>& out) const;
		Entity* InstantiatePrefabRoot(const std::string& prefabPath, NetworkComponent** outComp);

		// Player lifecycle / input (host authority + client ownership).
		void EnsureHostPlayer();                        // host: spawn its own player once
		uint32_t SpawnPlayerForConnection(uint32_t connId); // connId 0 = host's own
		void DespawnPlayerForConnection(uint32_t connId);
		void SendReconcileMessages();                   // host: authoritative pose + acked seq to each owner

		// First-person possession: make the local player's Camera the active view +
		// lock the mouse on spawn, restore on leave. Gated by the net_possess cvar.
		void PossessLocalPlayer();
		void UnpossessLocalPlayer();

		// Replicated variables (netvars). Host: diff each networked entity's
		// IReplicated components against a shadow of last-sent values and broadcast
		// only what changed (reliable). Clients apply in HandleMessage.
		void GatherAndSendProperties();
		void SendPropertyBaselineTo(uint32_t connId); // full state to a joining client
		// Client: revert any replicated var the game wrote locally back to the last
		// value the server sent (enforces read-only on clients). Gated by cvar.
		void EnforceClientReadOnly();
		// Serialize an entity's replicated props into `out` (entry blocks). When
		// changedOnly, updates the shadow and emits only changed props.
		void BuildEntityProperties(uint32_t netId, Entity* e, bool changedOnly, std::vector<uint8_t>& out, uint16_t& count);

	public:
		// RPC routing. Called by the global RouteRpc() (Network/IRpc.hpp) after the
		// args are serialized; routes by the RPC's registered direction.
		void DispatchOutgoingRpc(BaseComponent* comp, uint32_t rpcId, const std::vector<uint8_t>& args);
	private:
		void SendRpcMessage(uint32_t connOrZero, uint32_t netId, uint32_t compHash, uint32_t rpcId, const std::vector<uint8_t>& args, bool reliable);

		Scene* _scene = nullptr;

		float    _sendAccum      = 0.0f;
		uint16_t _serverTick     = 0;
		uint16_t _lastAppliedTick = 0;
		bool     _hasAppliedTick = false;

		// Runtime-spawn id space sits above authored/name-hash ids by convention.
		uint32_t _nextRuntimeNetId = 0x80000000u;

		struct SpawnedEntry { Entity* entity; std::string prefabPath; };
		std::unordered_map<uint32_t, SpawnedEntry> _spawned;

		// Player state.
		std::string _playerPrefabPath;
		uint32_t    _localPlayerNetId = 0;               // this peer's own player
		bool        _hostPlayerSpawned = false;
		float       _playerSpawnOffset = 0.0f;           // spread spawns so they don't stack
		std::unordered_map<uint32_t, uint32_t> _connPlayer; // connId -> player netId (host)

		// First-person possession state.
		Camera* _prevMainCamera = nullptr; // restored on unpossess
		bool    _possessed = false;

		// Netvar shadow: netId -> ((componentHash<<32)|propId) -> last-sent bytes.
		// Diffing serialized bytes is type-agnostic, so no per-type equality needed.
		std::unordered_map<uint32_t, std::unordered_map<uint64_t, std::vector<uint8_t>>> _propShadow;
		// Client mirror of the last server-applied value per prop, used to detect +
		// revert local writes (read-only enforcement).
		std::unordered_map<uint32_t, std::unordered_map<uint64_t, std::vector<uint8_t>>> _clientPropShadow;

		// Rebuilt each tick: effective netId -> NetworkComponent for all networked
		// entities currently in the scene (authored + spawned).
		std::unordered_map<uint32_t, NetworkComponent*> _index;
	};
}
