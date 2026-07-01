
#include "NetworkReplicationSystem.hpp"
#include "NetworkMessages.hpp"
#include "Scene.hpp"
#include "SceneManager.hpp"
#include "../Entity/Entity.hpp"
#include "../Entity/Component/NetworkComponent.hpp"
#include "../Entity/Component/NetworkPlayerComponent.hpp"
#include "../Entity/Component/Transform.hpp"
#include "../Entity/Component/RigidBody.hpp"
#include "../Entity/Component/Camera.hpp"
#include "../Physics/IRigidBody.hpp"
#include "../Input/InputSystem.hpp"
#include "../Network/INetworkSystem.hpp"
#include "../Network/IReplicated.hpp"
#include "../Network/IRpc.hpp"
#include "../Network/Net.hpp"
#include "../Steam/ISteamworksProvider.hpp"
#include "../Utility/CRC32.hpp"
#include "../FileSystem/PrefabLoader.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../Environment/LogFile.hpp"
#include "../Input/Hvar.hpp"

#include <cstring>
#include <algorithm>

namespace HexEngine
{
	namespace
	{
		// Compile-time validation of the BEGIN_REPLICATED/REPLICATE/END_REPLICATED
		// macros and the NetSerialize/NetDeserialize<T> dispatch (POD types via the
		// primary template, std::string via specialization). This never runs game
		// logic; the forced access below just makes the compiler instantiate it.
		struct NetVarMacroCompileTest : public IReplicated
		{
			int32_t       _i = 0;
			float         _f = 0.0f;
			bool          _b = false;
			math::Vector3 _v;
			std::string   _s;
			BEGIN_REPLICATED(NetVarMacroCompileTest)
				REPLICATE(_i)
				REPLICATE(_f)
				REPLICATE(_b)
				REPLICATE(_v)
				REPLICATE(_s)
			END_REPLICATED()
		};
		[[maybe_unused]] static const auto& s_netvarCompileTest = NetVarMacroCompileTest().GetReplicatedProperties();

		// Same, for the RPC macros + the RpcDispatcher<> arg-deduction machinery.
		struct NetRpcMacroCompileTest : public IRpcReceiver
		{
			void SvrDoThing(int32_t a, float b) { (void)a; (void)b; }
			void MultiEvent(math::Vector3 p) { (void)p; }
			void NoArgs() {}
			BEGIN_RPCS(NetRpcMacroCompileTest)
				RPC(SvrDoThing, HexEngine::RpcDirection::Server, true)
				RPC(MultiEvent, HexEngine::RpcDirection::Multicast, false)
				RPC(NoArgs, HexEngine::RpcDirection::Client, true)
			END_RPCS()
		};
		[[maybe_unused]] static const auto& s_rpcCompileTest = NetRpcMacroCompileTest().GetRpcEntries();
	}

	// Network send rate for transform snapshots (host -> clients).
	static constexpr float    kSendInterval       = 1.0f / 20.0f;
	// Cap entries per snapshot packet so each stays comfortably under a UDP MTU
	// (each NetTransformEntry is 32 bytes).
	static constexpr size_t   kMaxEntriesPerPacket = 32;

	// In-engine test hooks. Settable from the console (e.g. `net_host 27015`);
	// polled + auto-reset each frame. net_connect targets 127.0.0.1 for loopback
	// testing between two instances on one machine.
	static HVar g_netHost("net_host", "Host a multiplayer session on this UDP port (auto-resets)", 0, 0, 65535);
	static HVar g_netConnect("net_connect", "Connect to 127.0.0.1 on this UDP port (auto-resets)", 0, 0, 65535);
	static HVar g_netDisconnect("net_disconnect", "Set to 1 to leave the current session (auto-resets)", 0, 0, 1);
	// Steam P2P host hook: set to 1 to host via the active backend's P2P path
	// (Steam) and log the local SteamID. Connecting a client is SteamID-driven
	// (a 64-bit id can't be a cvar), so call g_pEnv->_networkSystem->ConnectP2P(id)
	// from lobby/UI code with the host's SteamID.
	static HVar g_netHostP2P("net_host_p2p", "Set to 1 to host a P2P session (Steam backend; auto-resets)", 0, 0, 1);
	// First-person possession: on spawn, make the local player's Camera the active
	// view and lock the mouse. Off = observe from the existing camera.
	static HVar g_netPossess("net_possess", "Possess (first-person) the local player camera on spawn", true, false, true);
	// Client read-only enforcement: revert locally-written replicated vars to the
	// last server value each frame (makes client tampering last <1 frame).
	static HVar g_netEnforceReadOnly("net_enforce_readonly", "Client: revert local writes to replicated vars", true, false, true);

	// === Authority helpers (Network/Net.hpp) ===
	namespace Net
	{
		bool HasSession()
		{
			INetworkSystem* n = (g_pEnv != nullptr) ? g_pEnv->_networkSystem : nullptr;
			return n != nullptr && n->GetRole() != INetworkSystem::NetRole::None;
		}
		bool IsServer()
		{
			INetworkSystem* n = (g_pEnv != nullptr) ? g_pEnv->_networkSystem : nullptr;
			// Authority unless we're a connected client (host or single-player = server).
			return n == nullptr || n->GetRole() != INetworkSystem::NetRole::Client;
		}
		bool IsClient()
		{
			INetworkSystem* n = (g_pEnv != nullptr) ? g_pEnv->_networkSystem : nullptr;
			return n != nullptr && n->GetRole() == INetworkSystem::NetRole::Client;
		}
	}

	NetworkReplicationSystem::NetworkReplicationSystem(Scene* scene) :
		_scene(scene)
	{
	}

	NetworkReplicationSystem::~NetworkReplicationSystem()
	{
	}

	void NetworkReplicationSystem::RebuildIndex()
	{
		_index.clear();
		std::vector<NetworkComponent*> comps;
		if (_scene == nullptr || !_scene->GetComponents<NetworkComponent>(comps))
			return;
		for (NetworkComponent* c : comps)
		{
			const uint32_t id = c->GetEffectiveNetId();
			if (id != 0)
				_index[id] = c;
		}
	}

	Entity* NetworkReplicationSystem::InstantiatePrefabRoot(const std::string& prefabPath, NetworkComponent** outComp)
	{
		if (outComp != nullptr)
			*outComp = nullptr;

		auto scene = (g_pEnv != nullptr && g_pEnv->_sceneManager != nullptr)
			? g_pEnv->_sceneManager->GetCurrentScene() : nullptr;
		if (!scene || g_pEnv->_prefabLoader == nullptr)
			return nullptr;

		std::vector<Entity*> ents = g_pEnv->_prefabLoader->LoadPrefab(scene, prefabPath);
		if (ents.empty())
			return nullptr;

		Entity* root = nullptr;
		for (Entity* e : ents)
		{
			if (e->IsPrefabInstanceRoot())
			{
				root = e;
				break;
			}
		}
		if (root == nullptr)
			root = ents.front();

		NetworkComponent* nc = root->GetComponent<NetworkComponent>();
		if (nc == nullptr)
			nc = root->AddComponent<NetworkComponent>();

		if (outComp != nullptr)
			*outComp = nc;
		return root;
	}

	void NetworkReplicationSystem::SerializeSpawn(uint32_t netId, Entity* root, const std::string& prefabPath, std::vector<uint8_t>& out) const
	{
		Transform* tf = root->GetComponent<Transform>();
		const math::Vector3 pos = tf ? tf->GetPosition(TransformState::Current) : math::Vector3::Zero;
		const math::Quaternion rot = tf ? tf->GetRotation(TransformState::Current) : math::Quaternion::Identity;

		NetSpawnHeader sh{};
		sh.netId = netId;
		sh.px = pos.x; sh.py = pos.y; sh.pz = pos.z;
		sh.qx = rot.x; sh.qy = rot.y; sh.qz = rot.z; sh.qw = rot.w;
		sh.pathLen = (uint16_t)prefabPath.size();

		NetMsgHeader h{};
		h.type = (uint8_t)NetMsgType::Spawn;
		h.flags = 0;
		h.payloadLen = (uint16_t)(sizeof(NetSpawnHeader) + prefabPath.size());

		out.resize(sizeof(NetMsgHeader) + h.payloadLen);
		uint8_t* p = out.data();
		std::memcpy(p, &h, sizeof(h)); p += sizeof(h);
		std::memcpy(p, &sh, sizeof(sh)); p += sizeof(sh);
		if (!prefabPath.empty())
			std::memcpy(p, prefabPath.data(), prefabPath.size());
	}

	uint32_t NetworkReplicationSystem::SpawnNetworked(const std::string& prefabPath, const math::Vector3& pos, const math::Quaternion& rot)
	{
		INetworkSystem* net = (g_pEnv != nullptr) ? g_pEnv->_networkSystem : nullptr;
		if (net == nullptr || net->GetRole() != INetworkSystem::NetRole::Host)
			return 0;

		NetworkComponent* nc = nullptr;
		Entity* root = InstantiatePrefabRoot(prefabPath, &nc);
		if (root == nullptr || nc == nullptr)
			return 0;

		const uint32_t id = _nextRuntimeNetId++;
		nc->SetExplicitNetId(id);
		root->SetPosition(pos);
		root->SetRotation(rot);

		_spawned[id] = { root, prefabPath };

		std::vector<uint8_t> buf;
		SerializeSpawn(id, root, prefabPath, buf);
		net->Broadcast(buf.data(), (uint32_t)buf.size(), true);
		return id;
	}

	void NetworkReplicationSystem::DespawnNetworked(uint32_t netId)
	{
		auto it = _spawned.find(netId);
		if (it == _spawned.end())
			return;

		INetworkSystem* net = (g_pEnv != nullptr) ? g_pEnv->_networkSystem : nullptr;
		if (net != nullptr && net->GetRole() == INetworkSystem::NetRole::Host)
		{
			NetMsgHeader h{};
			h.type = (uint8_t)NetMsgType::Despawn;
			h.flags = 0;
			h.payloadLen = (uint16_t)sizeof(NetDespawnBody);
			NetDespawnBody b{};
			b.netId = netId;

			std::vector<uint8_t> buf(sizeof(h) + sizeof(b));
			std::memcpy(buf.data(), &h, sizeof(h));
			std::memcpy(buf.data() + sizeof(h), &b, sizeof(b));
			net->Broadcast(buf.data(), (uint32_t)buf.size(), true);
		}

		auto scene = (g_pEnv != nullptr && g_pEnv->_sceneManager != nullptr)
			? g_pEnv->_sceneManager->GetCurrentScene() : nullptr;
		if (scene)
			scene->DestroyEntity(it->second.entity);
		_spawned.erase(it);
	}

	void NetworkReplicationSystem::SendBaselineTo(uint32_t connId)
	{
		INetworkSystem* net = (g_pEnv != nullptr) ? g_pEnv->_networkSystem : nullptr;
		if (net == nullptr)
			return;
		for (auto& kv : _spawned)
		{
			std::vector<uint8_t> buf;
			SerializeSpawn(kv.first, kv.second.entity, kv.second.prefabPath, buf);
			net->Send(connId, buf.data(), (uint32_t)buf.size(), true);
		}

		// Full replicated-variable state for the joining client (ongoing changes
		// then arrive as deltas via GatherAndSendProperties).
		SendPropertyBaselineTo(connId);
	}

	void NetworkReplicationSystem::HandleMessage(uint32_t connId, const uint8_t* data, uint32_t size)
	{
		(void)connId;
		if (size < sizeof(NetMsgHeader))
			return;

		NetMsgHeader h;
		std::memcpy(&h, data, sizeof(h));
		const uint8_t* payload = data + sizeof(h);
		const uint32_t avail = size - (uint32_t)sizeof(h);
		if (h.payloadLen > avail)
			return;

		switch ((NetMsgType)h.type)
		{
		case NetMsgType::Spawn:
		{
			if (avail < sizeof(NetSpawnHeader))
				return;
			NetSpawnHeader sh;
			std::memcpy(&sh, payload, sizeof(sh));
			if (avail < sizeof(NetSpawnHeader) + sh.pathLen)
				return;
			std::string path((const char*)(payload + sizeof(NetSpawnHeader)), sh.pathLen);

			if (_spawned.find(sh.netId) != _spawned.end())
				return; // already have it

			NetworkComponent* nc = nullptr;
			Entity* root = InstantiatePrefabRoot(path, &nc);
			if (root == nullptr || nc == nullptr)
				return;

			nc->SetExplicitNetId(sh.netId);
			const math::Vector3 pos(sh.px, sh.py, sh.pz);
			const math::Quaternion rot(sh.qx, sh.qy, sh.qz, sh.qw);
			root->SetPosition(pos);
			root->SetRotation(rot);
			nc->ReceiveSnapshot(pos, rot);

			_spawned[sh.netId] = { root, path };
			break;
		}

		case NetMsgType::Despawn:
		{
			if (avail < sizeof(NetDespawnBody))
				return;
			NetDespawnBody b;
			std::memcpy(&b, payload, sizeof(b));
			if (b.netId == _localPlayerNetId)
			{
				UnpossessLocalPlayer();
				_localPlayerNetId = 0;
			}
			auto it = _spawned.find(b.netId);
			if (it != _spawned.end())
			{
				auto scene = (g_pEnv != nullptr && g_pEnv->_sceneManager != nullptr)
					? g_pEnv->_sceneManager->GetCurrentScene() : nullptr;
				if (scene)
					scene->DestroyEntity(it->second.entity);
				_spawned.erase(it);
			}
			break;
		}

		case NetMsgType::TransformSnapshot:
		{
			if (avail < sizeof(NetSnapshotHeader))
				return;
			NetSnapshotHeader snh;
			std::memcpy(&snh, payload, sizeof(snh));

			// Drop whole packets that are older than the last one we applied.
			if (_hasAppliedTick && (int16_t)(snh.serverTick - _lastAppliedTick) < 0)
				return;

			const uint8_t* p = payload + sizeof(NetSnapshotHeader);
			uint32_t remaining = avail - (uint32_t)sizeof(NetSnapshotHeader);
			for (uint16_t i = 0; i < snh.count; ++i)
			{
				if (remaining < sizeof(NetTransformEntry))
					break;
				NetTransformEntry e;
				std::memcpy(&e, p, sizeof(e));
				p += sizeof(e);
				remaining -= (uint32_t)sizeof(e);

				// The owner PREDICTS its own player - never let the generic snapshot
				// fight prediction (the server sends it a PlayerReconcile instead).
				if (e.netId == _localPlayerNetId)
					continue;

				auto it = _index.find(e.netId);
				if (it != _index.end())
					it->second->ReceiveSnapshot(math::Vector3(e.px, e.py, e.pz),
						math::Quaternion(e.qx, e.qy, e.qz, e.qw));
			}
			_lastAppliedTick = snh.serverTick;
			_hasAppliedTick = true;
			break;
		}

		case NetMsgType::PlayerInput:
		{
			// Host: a client's input for its own player. SECURITY: the sender must
			// actually own the netId it's controlling, else we ignore it.
			if (avail < sizeof(NetPlayerInputHeader))
				return;
			NetPlayerInputHeader ih;
			std::memcpy(&ih, payload, sizeof(ih));
			if (avail < sizeof(NetPlayerInputHeader) + ih.count * sizeof(NetInputCmd))
				return;

			auto it = _index.find(ih.netId);
			if (it == _index.end())
				return;
			NetworkComponent* nc = it->second;
			if (nc->GetOwnerConnId() != connId)
				return; // reject: this client does not own that player

			Entity* e = nc->GetEntity();
			NetworkPlayerComponent* pc = (e != nullptr) ? e->GetComponent<NetworkPlayerComponent>() : nullptr;
			if (pc == nullptr)
				return;

			const uint8_t* p = payload + sizeof(NetPlayerInputHeader);
			for (uint32_t i = 0; i < ih.count; ++i)
			{
				NetInputCmd c;
				std::memcpy(&c, p, sizeof(c));
				p += sizeof(c);
				pc->EnqueueRemoteInput(c);
			}
			break;
		}

		case NetMsgType::PlayerReconcile:
		{
			// Owning client: authoritative correction for our own player.
			if (avail < sizeof(NetPlayerReconcile))
				return;
			NetPlayerReconcile r;
			std::memcpy(&r, payload, sizeof(r));
			if (r.netId != _localPlayerNetId)
				return;
			auto it = _index.find(r.netId);
			if (it == _index.end())
				return;
			Entity* e = it->second->GetEntity();
			NetworkPlayerComponent* pc = (e != nullptr) ? e->GetComponent<NetworkPlayerComponent>() : nullptr;
			if (pc != nullptr)
				pc->ReceiveReconcile(math::Vector3(r.px, r.py, r.pz), r.yaw, r.lastProcessedSeq);
			break;
		}

		case NetMsgType::LocalPlayerAssigned:
		{
			// Client: the host told us which entity is our own player.
			if (avail < sizeof(NetLocalPlayerAssigned))
				return;
			NetLocalPlayerAssigned b;
			std::memcpy(&b, payload, sizeof(b));
			_localPlayerNetId = b.netId;
			LOG_INFO("Network: local player assigned netId %u.", b.netId);
			PossessLocalPlayer();
			break;
		}

		case NetMsgType::PropertyUpdate:
		{
			// Client: apply authoritative replicated-variable values to the entity's
			// IReplicated components (routed by component + property name hash).
			if (avail < sizeof(NetPropUpdateHeader))
				return;
			NetPropUpdateHeader ph;
			std::memcpy(&ph, payload, sizeof(ph));

			auto it = _index.find(ph.netId);
			if (it == _index.end())
				return;
			Entity* e = it->second->GetEntity();
			if (e == nullptr)
				return;

			const uint8_t* p = payload + sizeof(NetPropUpdateHeader);
			uint32_t remaining = avail - (uint32_t)sizeof(NetPropUpdateHeader);
			for (uint16_t i = 0; i < ph.count; ++i)
			{
				if (remaining < sizeof(NetPropEntryHeader))
					break;
				NetPropEntryHeader eh;
				std::memcpy(&eh, p, sizeof(eh));
				p += sizeof(eh);
				remaining -= (uint32_t)sizeof(eh);
				if (remaining < eh.valueLen)
					break;
				const uint8_t* valPtr = p;
				p += eh.valueLen;
				remaining -= eh.valueLen;

				for (BaseComponent* comp : e->GetAllComponents())
				{
					if ((uint32_t)CRC32::HashString(comp->GetComponentName()) != eh.componentHash)
						continue;
					IReplicated* rep = dynamic_cast<IReplicated*>(comp);
					if (rep == nullptr)
						break;
					for (const ReplicatedProperty& prop : rep->GetReplicatedProperties())
					{
						if (prop.id != eh.propId)
							continue;
						NetReader rdr(valPtr, eh.valueLen);
						prop.deserialize(rep, rdr);
						// Remember the authoritative value so we can revert local writes.
						const uint64_t key = ((uint64_t)eh.componentHash << 32) | eh.propId;
						_clientPropShadow[ph.netId][key].assign(valPtr, valPtr + eh.valueLen);
						break;
					}
					break;
				}
			}
			break;
		}

		case NetMsgType::RpcCall:
		{
			if (avail < sizeof(NetRpcCall))
				return;
			NetRpcCall rc;
			std::memcpy(&rc, payload, sizeof(rc));
			const uint8_t* argPtr = payload + sizeof(NetRpcCall);
			const uint32_t argLen = avail - (uint32_t)sizeof(NetRpcCall);

			auto it = _index.find(rc.netId);
			if (it == _index.end())
				return;
			NetworkComponent* nc = it->second;
			Entity* e = nc->GetEntity();
			if (e == nullptr)
				return;

			INetworkSystem* net = (g_pEnv != nullptr) ? g_pEnv->_networkSystem : nullptr;
			const INetworkSystem::NetRole role = (net != nullptr) ? net->GetRole() : INetworkSystem::NetRole::None;

			for (BaseComponent* comp : e->GetAllComponents())
			{
				if ((uint32_t)CRC32::HashString(comp->GetComponentName()) != rc.componentHash)
					continue;
				IRpcReceiver* recv = dynamic_cast<IRpcReceiver*>(comp);
				if (recv == nullptr)
					break;
				const RpcEntry* entry = nullptr;
				for (const RpcEntry& en : recv->GetRpcEntries())
					if (en.id == rc.rpcId) { entry = &en; break; }
				if (entry == nullptr)
					break;

				// Direction + authority checks.
				if (entry->direction == RpcDirection::Server)
				{
					// Only the host runs server RPCs, and only for the client that
					// actually owns the target entity (anti-spoof, v1).
					if (role != INetworkSystem::NetRole::Host)
						break;
					if (nc->GetOwnerConnId() != connId)
						break;
				}
				else
				{
					// Client / Multicast RPCs are only executed on clients (from the host).
					if (role != INetworkSystem::NetRole::Client)
						break;
				}

				NetReader r(argPtr, argLen);
				entry->dispatch(recv, r);
				break;
			}
			break;
		}

		default:
			break;
		}
	}

	void NetworkReplicationSystem::EnsureHostPlayer()
	{
		if (_hostPlayerSpawned || _playerPrefabPath.empty())
			return;
		const uint32_t id = SpawnPlayerForConnection(0);
		if (id != 0)
		{
			_hostPlayerSpawned = true;
			_localPlayerNetId = id;
			PossessLocalPlayer();
		}
	}

	uint32_t NetworkReplicationSystem::SpawnPlayerForConnection(uint32_t connId)
	{
		INetworkSystem* net = (g_pEnv != nullptr) ? g_pEnv->_networkSystem : nullptr;
		if (net == nullptr || net->GetRole() != INetworkSystem::NetRole::Host || _playerPrefabPath.empty())
			return 0;

		NetworkComponent* nc = nullptr;
		Entity* root = InstantiatePrefabRoot(_playerPrefabPath, &nc);
		if (root == nullptr || nc == nullptr)
			return 0;

		// Ensure the input-authoritative controller is present.
		if (root->GetComponent<NetworkPlayerComponent>() == nullptr)
			root->AddComponent<NetworkPlayerComponent>();

		const uint32_t id = _nextRuntimeNetId++;
		nc->SetExplicitNetId(id);
		nc->SetOwnerConnId(connId);

		// Spread spawn points so players don't stack on the origin.
		const math::Vector3 pos(_playerSpawnOffset, 2.0f, 0.0f);
		_playerSpawnOffset += 2.0f;
		root->SetPosition(pos);
		if (RigidBody* rb = root->GetComponentDerived<RigidBody>())
			if (IRigidBody* cct = rb->GetIRigidBody())
				cct->UpdatePosePosition(pos);

		_spawned[id] = { root, _playerPrefabPath };
		_connPlayer[connId] = id;

		// Broadcast the spawn to everyone (reliable).
		std::vector<uint8_t> buf;
		SerializeSpawn(id, root, _playerPrefabPath, buf);
		net->Broadcast(buf.data(), (uint32_t)buf.size(), true);

		// Tell the owning client which netId is its own player (reliable).
		if (connId != 0)
		{
			NetMsgHeader h{};
			h.type = (uint8_t)NetMsgType::LocalPlayerAssigned;
			h.flags = 0;
			h.payloadLen = (uint16_t)sizeof(NetLocalPlayerAssigned);
			NetLocalPlayerAssigned b{};
			b.netId = id;
			std::vector<uint8_t> m(sizeof(h) + sizeof(b));
			std::memcpy(m.data(), &h, sizeof(h));
			std::memcpy(m.data() + sizeof(h), &b, sizeof(b));
			net->Send(connId, m.data(), (uint32_t)m.size(), true);
		}

		LOG_INFO("Network: spawned player netId %u for conn %u.", id, connId);
		return id;
	}

	void NetworkReplicationSystem::DespawnPlayerForConnection(uint32_t connId)
	{
		auto it = _connPlayer.find(connId);
		if (it == _connPlayer.end())
			return;
		DespawnNetworked(it->second);
		_connPlayer.erase(it);
	}

	void NetworkReplicationSystem::SendReconcileMessages()
	{
		INetworkSystem* net = (g_pEnv != nullptr) ? g_pEnv->_networkSystem : nullptr;
		if (net == nullptr || net->GetRole() != INetworkSystem::NetRole::Host)
			return;

		for (auto& kv : _index)
		{
			NetworkComponent* nc = kv.second;
			const uint32_t owner = nc->GetOwnerConnId();
			if (owner == 0)
				continue; // host's own player needs no reconcile

			Entity* e = nc->GetEntity();
			NetworkPlayerComponent* pc = (e != nullptr) ? e->GetComponent<NetworkPlayerComponent>() : nullptr;
			if (pc == nullptr)
				continue;

			math::Vector3 foot(0.0f, 0.0f, 0.0f);
			if (RigidBody* rb = e->GetComponentDerived<RigidBody>())
				if (IRigidBody* cct = rb->GetIRigidBody())
					foot = cct->GetPhysicsPosition();

			NetPlayerReconcile r{};
			r.netId = kv.first;
			r.px = foot.x; r.py = foot.y; r.pz = foot.z;
			r.yaw = pc->GetYaw();
			r.lastProcessedSeq = pc->GetLastProcessedSeq();

			NetMsgHeader h{};
			h.type = (uint8_t)NetMsgType::PlayerReconcile;
			h.flags = 0;
			h.payloadLen = (uint16_t)sizeof(NetPlayerReconcile);
			std::vector<uint8_t> buf(sizeof(h) + sizeof(r));
			std::memcpy(buf.data(), &h, sizeof(h));
			std::memcpy(buf.data() + sizeof(h), &r, sizeof(r));
			net->Send(owner, buf.data(), (uint32_t)buf.size(), false);
		}
	}

	void NetworkReplicationSystem::SendPlayerInput(uint32_t netId, const NetInputCmd* cmds, uint32_t count)
	{
		INetworkSystem* net = (g_pEnv != nullptr) ? g_pEnv->_networkSystem : nullptr;
		if (net == nullptr || cmds == nullptr || count == 0)
			return;

		NetPlayerInputHeader ih{};
		ih.netId = netId;
		ih.count = count;

		NetMsgHeader h{};
		h.type = (uint8_t)NetMsgType::PlayerInput;
		h.flags = 0;
		h.payloadLen = (uint16_t)(sizeof(NetPlayerInputHeader) + count * sizeof(NetInputCmd));

		std::vector<uint8_t> buf(sizeof(NetMsgHeader) + h.payloadLen);
		uint8_t* p = buf.data();
		std::memcpy(p, &h, sizeof(h)); p += sizeof(h);
		std::memcpy(p, &ih, sizeof(ih)); p += sizeof(ih);
		std::memcpy(p, cmds, count * sizeof(NetInputCmd));

		// A client has only the host connection, so Broadcast == "send to host".
		net->Broadcast(buf.data(), (uint32_t)buf.size(), false);
	}

	void NetworkReplicationSystem::BuildEntityProperties(uint32_t netId, Entity* e, bool changedOnly, std::vector<uint8_t>& out, uint16_t& count)
	{
		count = 0;
		if (e == nullptr)
			return;

		for (BaseComponent* comp : e->GetAllComponents())
		{
			IReplicated* rep = dynamic_cast<IReplicated*>(comp);
			if (rep == nullptr)
				continue;

			const uint32_t compHash = (uint32_t)CRC32::HashString(comp->GetComponentName());
			for (const ReplicatedProperty& prop : rep->GetReplicatedProperties())
			{
				std::vector<uint8_t> value;
				NetWriter w(value);
				prop.serialize(rep, w);

				if (changedOnly)
				{
					const uint64_t key = ((uint64_t)compHash << 32) | prop.id;
					std::vector<uint8_t>& shadow = _propShadow[netId][key];
					if (shadow == value)
						continue;      // unchanged since last send
					shadow = value;    // remember what we're about to send
				}

				NetPropEntryHeader eh{};
				eh.componentHash = compHash;
				eh.propId = prop.id;
				eh.valueLen = (uint16_t)value.size();

				const uint8_t* ehp = (const uint8_t*)&eh;
				out.insert(out.end(), ehp, ehp + sizeof(eh));
				out.insert(out.end(), value.begin(), value.end());
				++count;
			}
		}
	}

	void NetworkReplicationSystem::GatherAndSendProperties()
	{
		INetworkSystem* net = (g_pEnv != nullptr) ? g_pEnv->_networkSystem : nullptr;
		if (net == nullptr || net->GetRole() != INetworkSystem::NetRole::Host)
			return;

		for (auto& kv : _index)
		{
			std::vector<uint8_t> body;
			uint16_t count = 0;
			BuildEntityProperties(kv.first, kv.second->GetEntity(), /*changedOnly*/ true, body, count);
			if (count == 0)
				continue;

			NetPropUpdateHeader ph{};
			ph.netId = kv.first;
			ph.count = count;

			NetMsgHeader h{};
			h.type = (uint8_t)NetMsgType::PropertyUpdate;
			h.flags = 0;
			h.payloadLen = (uint16_t)(sizeof(NetPropUpdateHeader) + body.size());

			std::vector<uint8_t> buf(sizeof(NetMsgHeader) + h.payloadLen);
			uint8_t* p = buf.data();
			std::memcpy(p, &h, sizeof(h)); p += sizeof(h);
			std::memcpy(p, &ph, sizeof(ph)); p += sizeof(ph);
			std::memcpy(p, body.data(), body.size());

			net->Broadcast(buf.data(), (uint32_t)buf.size(), true); // reliable: state must not be lost
		}
	}

	void NetworkReplicationSystem::SendPropertyBaselineTo(uint32_t connId)
	{
		INetworkSystem* net = (g_pEnv != nullptr) ? g_pEnv->_networkSystem : nullptr;
		if (net == nullptr)
			return;

		for (auto& kv : _index)
		{
			std::vector<uint8_t> body;
			uint16_t count = 0;
			BuildEntityProperties(kv.first, kv.second->GetEntity(), /*changedOnly*/ false, body, count);
			if (count == 0)
				continue;

			NetPropUpdateHeader ph{};
			ph.netId = kv.first;
			ph.count = count;

			NetMsgHeader h{};
			h.type = (uint8_t)NetMsgType::PropertyUpdate;
			h.flags = 0;
			h.payloadLen = (uint16_t)(sizeof(NetPropUpdateHeader) + body.size());

			std::vector<uint8_t> buf(sizeof(NetMsgHeader) + h.payloadLen);
			uint8_t* p = buf.data();
			std::memcpy(p, &h, sizeof(h)); p += sizeof(h);
			std::memcpy(p, &ph, sizeof(ph)); p += sizeof(ph);
			std::memcpy(p, body.data(), body.size());

			net->Send(connId, buf.data(), (uint32_t)buf.size(), true);
		}
	}

	void NetworkReplicationSystem::EnforceClientReadOnly()
	{
		static bool warnedOnce = false;
		for (auto& kv : _index)
		{
			auto shadowIt = _clientPropShadow.find(kv.first);
			if (shadowIt == _clientPropShadow.end())
				continue;
			Entity* e = kv.second->GetEntity();
			if (e == nullptr)
				continue;

			for (BaseComponent* comp : e->GetAllComponents())
			{
				IReplicated* rep = dynamic_cast<IReplicated*>(comp);
				if (rep == nullptr)
					continue;
				const uint32_t compHash = (uint32_t)CRC32::HashString(comp->GetComponentName());
				for (const ReplicatedProperty& prop : rep->GetReplicatedProperties())
				{
					const uint64_t key = ((uint64_t)compHash << 32) | prop.id;
					auto valIt = shadowIt->second.find(key);
					if (valIt == shadowIt->second.end())
						continue; // server hasn't sent this one yet

					std::vector<uint8_t> current;
					NetWriter w(current);
					prop.serialize(rep, w);
					if (current == valIt->second)
						continue; // no local write

					// The game wrote a replicated var on a client - revert it to the
					// server's value. Replicated state is server-authoritative.
					NetReader r(valIt->second.data(), valIt->second.size());
					prop.deserialize(rep, r);
					if (!warnedOnce)
					{
						warnedOnce = true;
						LOG_WARN("Network: reverted a client-side write to a replicated var (comp '%s'). Replicated state is server-authoritative - guard writes with Net::IsServer().", comp->GetComponentName());
					}
				}
			}
		}
	}

	// Free function declared in Network/IRpc.hpp; the CallRpc<> templates funnel
	// here after serializing args. Delegates to the current scene's rep system.
	void RouteRpc(BaseComponent* comp, uint32_t rpcId, const std::vector<uint8_t>& args)
	{
		if (comp == nullptr || g_pEnv == nullptr || g_pEnv->_sceneManager == nullptr)
			return;
		auto scene = g_pEnv->_sceneManager->GetCurrentScene();
		if (scene)
			scene->GetNetworkReplicationSystem()->DispatchOutgoingRpc(comp, rpcId, args);
	}

	void NetworkReplicationSystem::SendRpcMessage(uint32_t conn, uint32_t netId, uint32_t compHash, uint32_t rpcId, const std::vector<uint8_t>& args, bool reliable)
	{
		INetworkSystem* net = (g_pEnv != nullptr) ? g_pEnv->_networkSystem : nullptr;
		if (net == nullptr)
			return;

		NetRpcCall rc{};
		rc.netId = netId;
		rc.componentHash = compHash;
		rc.rpcId = rpcId;

		NetMsgHeader h{};
		h.type = (uint8_t)NetMsgType::RpcCall;
		h.flags = 0;
		h.payloadLen = (uint16_t)(sizeof(NetRpcCall) + args.size());

		std::vector<uint8_t> buf(sizeof(NetMsgHeader) + h.payloadLen);
		uint8_t* p = buf.data();
		std::memcpy(p, &h, sizeof(h)); p += sizeof(h);
		std::memcpy(p, &rc, sizeof(rc)); p += sizeof(rc);
		if (!args.empty())
			std::memcpy(p, args.data(), args.size());

		if (conn == 0)
			net->Broadcast(buf.data(), (uint32_t)buf.size(), reliable);
		else
			net->Send(conn, buf.data(), (uint32_t)buf.size(), reliable);
	}

	void NetworkReplicationSystem::DispatchOutgoingRpc(BaseComponent* comp, uint32_t rpcId, const std::vector<uint8_t>& args)
	{
		IRpcReceiver* recv = dynamic_cast<IRpcReceiver*>(comp);
		if (recv == nullptr)
			return;

		const RpcEntry* entry = nullptr;
		for (const RpcEntry& en : recv->GetRpcEntries())
			if (en.id == rpcId) { entry = &en; break; }
		if (entry == nullptr)
		{
			LOG_WARN("RPC id %u not registered on component '%s'.", rpcId, comp->GetComponentName());
			return;
		}

		INetworkSystem* net = (g_pEnv != nullptr) ? g_pEnv->_networkSystem : nullptr;
		const INetworkSystem::NetRole role = (net != nullptr) ? net->GetRole() : INetworkSystem::NetRole::None;

		// Single-player / no active session: run the handler locally so RPC-using
		// gameplay code works offline too.
		if (net == nullptr || role == INetworkSystem::NetRole::None)
		{
			NetReader r(args.data(), args.size());
			entry->dispatch(recv, r);
			return;
		}

		Entity* e = comp->GetEntity();
		NetworkComponent* nc = (e != nullptr) ? e->GetComponent<NetworkComponent>() : nullptr;
		if (nc == nullptr)
		{
			LOG_WARN("RPC on an entity with no NetworkComponent; ignored.");
			return;
		}
		const uint32_t netId = nc->GetEffectiveNetId();
		const uint32_t compHash = (uint32_t)CRC32::HashString(comp->GetComponentName());

		switch (entry->direction)
		{
		case RpcDirection::Server:
			if (role == INetworkSystem::NetRole::Host)
			{
				NetReader r(args.data(), args.size()); // we ARE the server
				entry->dispatch(recv, r);
			}
			else
			{
				SendRpcMessage(0, netId, compHash, rpcId, args, entry->reliable); // client -> host
			}
			break;

		case RpcDirection::Client:
			if (role == INetworkSystem::NetRole::Host)
			{
				const uint32_t owner = nc->GetOwnerConnId();
				if (owner != 0)
				{
					SendRpcMessage(owner, netId, compHash, rpcId, args, entry->reliable);
				}
				else
				{
					NetReader r(args.data(), args.size()); // host owns it -> run locally
					entry->dispatch(recv, r);
				}
			}
			// A client cannot originate a Client RPC; ignore.
			break;

		case RpcDirection::Multicast:
			if (role == INetworkSystem::NetRole::Host)
			{
				NetReader r(args.data(), args.size());
				entry->dispatch(recv, r);                                          // run on the server
				SendRpcMessage(0, netId, compHash, rpcId, args, entry->reliable);  // and every client
			}
			// A client cannot originate a Multicast RPC; ignore.
			break;
		}
	}

	void NetworkReplicationSystem::PossessLocalPlayer()
	{
		if (!g_netPossess._val.b || _possessed || _localPlayerNetId == 0)
			return;
		auto it = _spawned.find(_localPlayerNetId);
		if (it == _spawned.end() || it->second.entity == nullptr)
			return;
		Camera* cam = it->second.entity->GetComponent<Camera>();
		if (cam == nullptr)
		{
			LOG_WARN("Network: local player prefab has no Camera component to possess.");
			return;
		}
		auto scene = (g_pEnv->_sceneManager != nullptr) ? g_pEnv->_sceneManager->GetCurrentScene() : nullptr;
		if (!scene)
			return;
		_prevMainCamera = scene->GetMainCamera();
		scene->SetMainCamera(cam);
		if (g_pEnv->_inputSystem != nullptr)
			g_pEnv->_inputSystem->SetMouseLockMode(MouseLockMode::Locked);
		_possessed = true;
		LOG_INFO("Network: possessed local player camera (netId %u).", _localPlayerNetId);
	}

	void NetworkReplicationSystem::UnpossessLocalPlayer()
	{
		if (!_possessed)
			return;
		if (_prevMainCamera != nullptr)
		{
			if (auto scene = (g_pEnv->_sceneManager != nullptr) ? g_pEnv->_sceneManager->GetCurrentScene() : nullptr)
				scene->SetMainCamera(_prevMainCamera);
		}
		if (g_pEnv->_inputSystem != nullptr)
			g_pEnv->_inputSystem->SetMouseLockMode(MouseLockMode::Free);
		_prevMainCamera = nullptr;
		_possessed = false;
		LOG_INFO("Network: unpossessed local player camera.");
	}

	void NetworkReplicationSystem::Pump(float dt)
	{
		(void)dt;
		INetworkSystem* net = (g_pEnv != nullptr) ? g_pEnv->_networkSystem : nullptr;
		if (net == nullptr)
			return;

		PollControlVars();

		if (net->GetRole() == INetworkSystem::NetRole::None)
		{
			// Session ended - drop possession and forget our player id.
			if (_possessed)
				UnpossessLocalPlayer();
			_localPlayerNetId = 0;
			return;
		}

		// Host: make sure its own player exists once hosting is active.
		if (net->GetRole() == INetworkSystem::NetRole::Host)
			EnsureHostPlayer();

		// Build the netId -> component index before applying inbound messages so
		// snapshots resolve their targets (and baseline spawns can be looked up).
		RebuildIndex();

		INetworkSystem::NetEvent ev;
		while (net->PollEvent(ev))
		{
			if (net->GetRole() == INetworkSystem::NetRole::Host)
			{
				if (ev.type == INetworkSystem::NetEvent::Type::Connected)
				{
					SendBaselineTo(ev.connId);            // existing entities/players
					SpawnPlayerForConnection(ev.connId);  // this client's own player
				}
				else if (ev.type == INetworkSystem::NetEvent::Type::Disconnected)
				{
					DespawnPlayerForConnection(ev.connId);
				}
			}
		}

		INetworkSystem::NetMessage msg;
		while (net->PollMessage(msg))
		{
			if (!msg.bytes.empty())
				HandleMessage(msg.connId, msg.bytes.data(), (uint32_t)msg.bytes.size());
		}

		// Client: revert any locally-written replicated vars back to the server's
		// authoritative value (read-only enforcement).
		if (net->GetRole() == INetworkSystem::NetRole::Client && g_netEnforceReadOnly._val.b)
			EnforceClientReadOnly();
	}

	void NetworkReplicationSystem::SendUpdates(float dt)
	{
		INetworkSystem* net = (g_pEnv != nullptr) ? g_pEnv->_networkSystem : nullptr;
		if (net == nullptr || net->GetRole() != INetworkSystem::NetRole::Host)
			return;

		_sendAccum += dt;
		if (_sendAccum < kSendInterval)
			return;
		_sendAccum = 0.0f;
		++_serverTick;

		RebuildIndex();

		// Send each client its own player's authoritative pose + last-applied input
		// seq, so it can reconcile its prediction.
		SendReconcileMessages();

		// Broadcast changed replicated variables (reliable, delta-compressed).
		GatherAndSendProperties();

		if (_index.empty())
			return;

		std::vector<NetTransformEntry> entries;
		entries.reserve(_index.size());
		for (auto& kv : _index)
		{
			NetworkComponent* nc = kv.second;
			if ((nc->GetSyncFlags() & NetworkComponent::SyncTransform) == 0)
				continue;
			Entity* e = nc->GetEntity();
			Transform* tf = (e != nullptr) ? e->GetComponent<Transform>() : nullptr;
			if (tf == nullptr)
				continue;

			const math::Vector3 pos = tf->GetPosition(TransformState::Current);
			const math::Quaternion rot = tf->GetRotation(TransformState::Current);
			NetTransformEntry ent;
			ent.netId = kv.first;
			ent.px = pos.x; ent.py = pos.y; ent.pz = pos.z;
			ent.qx = rot.x; ent.qy = rot.y; ent.qz = rot.z; ent.qw = rot.w;
			entries.push_back(ent);
		}
		if (entries.empty())
			return;

		for (size_t off = 0; off < entries.size(); off += kMaxEntriesPerPacket)
		{
			const size_t n = std::min(kMaxEntriesPerPacket, entries.size() - off);

			NetSnapshotHeader snh{};
			snh.serverTick = _serverTick;
			snh.count = (uint16_t)n;

			NetMsgHeader h{};
			h.type = (uint8_t)NetMsgType::TransformSnapshot;
			h.flags = 0;
			h.payloadLen = (uint16_t)(sizeof(NetSnapshotHeader) + n * sizeof(NetTransformEntry));

			std::vector<uint8_t> buf(sizeof(NetMsgHeader) + h.payloadLen);
			uint8_t* p = buf.data();
			std::memcpy(p, &h, sizeof(h)); p += sizeof(h);
			std::memcpy(p, &snh, sizeof(snh)); p += sizeof(snh);
			std::memcpy(p, &entries[off], n * sizeof(NetTransformEntry));

			net->Broadcast(buf.data(), (uint32_t)buf.size(), false);
		}
	}

	void NetworkReplicationSystem::PollControlVars()
	{
		INetworkSystem* net = (g_pEnv != nullptr) ? g_pEnv->_networkSystem : nullptr;
		if (net == nullptr)
			return;

		if (g_netHost._val.i32 > 0)
		{
			const uint16_t port = (uint16_t)g_netHost._val.i32;
			g_netHost._val.i32 = 0;
			if (net->GetRole() == INetworkSystem::NetRole::None && net->StartHost(port))
				LOG_INFO("Network: hosting on UDP port %u.", (uint32_t)port);
		}

		if (g_netConnect._val.i32 > 0)
		{
			const uint16_t port = (uint16_t)g_netConnect._val.i32;
			g_netConnect._val.i32 = 0;
			if (net->GetRole() == INetworkSystem::NetRole::None && net->Connect("127.0.0.1", port))
				LOG_INFO("Network: connecting to 127.0.0.1:%u.", (uint32_t)port);
		}

		if (g_netHostP2P._val.i32 != 0)
		{
			g_netHostP2P._val.i32 = 0;
			if (net->GetRole() == INetworkSystem::NetRole::None)
			{
				if (!net->SupportsP2P())
				{
					LOG_WARN("Network: net_host_p2p set but the active backend has no P2P support (use net_backend=2 for Steam).");
				}
				else if (net->StartHostP2P(0))
				{
					LOG_INFO("Network: hosting P2P. Local id (share with peers): %llu", (unsigned long long)net->GetLocalIdentity());
				}
			}
		}

		if (g_netDisconnect._val.i32 != 0)
		{
			g_netDisconnect._val.i32 = 0;
			net->Disconnect();
			LOG_INFO("Network: disconnected.");
		}

		// Steam lobby -> P2P transport bridge. When the Steam matchmaking layer
		// creates or enters a lobby (e.g. a friend accepted an overlay invite),
		// automatically drive the P2P transport so hosting/joining "just works".
		// Only meaningful with the P2P-capable (Steam) backend.
		if (net->SupportsP2P() && g_pEnv->_steamworksProvider != nullptr)
		{
			ISteamworksProvider* sw = g_pEnv->_steamworksProvider;

			if (sw->ConsumePendingHostStart() && net->GetRole() == INetworkSystem::NetRole::None)
			{
				if (net->StartHostP2P(0))
					LOG_INFO("Network: hosting P2P for Steam lobby %llu (local id %llu).",
						(unsigned long long)sw->GetLobbyId(), (unsigned long long)net->GetLocalIdentity());
			}

			uint64_t hostId = 0;
			if (sw->ConsumePendingClientConnect(hostId) && net->GetRole() == INetworkSystem::NetRole::None)
			{
				if (net->ConnectP2P(hostId))
					LOG_INFO("Network: connecting P2P to Steam lobby host %llu.", (unsigned long long)hostId);
			}
		}
	}
}
