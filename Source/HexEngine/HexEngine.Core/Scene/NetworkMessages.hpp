
#pragma once

#include <cstdint>

namespace HexEngine
{
	// Compact binary wire format for the replication layer. Both peers are
	// Windows/x64 (little-endian) for v1, so POD structs are memcpy'd directly.
	// Every packet starts with NetMsgHeader, followed by `payloadLen` bytes whose
	// layout depends on `type`.
	enum class NetMsgType : uint8_t
	{
		Spawn              = 1, // host -> client: instantiate a prefab and bind a netId (reliable)
		Despawn            = 2, // host -> client: destroy the entity with this netId (reliable)
		TransformSnapshot  = 3, // host -> clients: batched pos/rot for N netIds (unreliable)
		PlayerInput        = 4, // client -> host: intent (input) for the client's owned player (unreliable, redundant)
		PlayerReconcile    = 5, // host -> owner: authoritative pose + last-applied input seq (unreliable)
		LocalPlayerAssigned = 6, // host -> owning client: "this netId is your player" (reliable)
		PropertyUpdate     = 7, // host -> clients: changed replicated variables (reliable)
		RpcCall            = 8, // remote procedure call (client->host, host->owner, or host->all)
	};

	// Buttons bitfield in NetInputCmd.
	enum NetInputButtons : uint32_t
	{
		NetBtn_Jump   = 1u << 0,
		NetBtn_Sprint = 1u << 1,
	};

	// How many recent input commands each PlayerInput packet carries, for
	// redundancy against packet loss (unreliable transport). Cheap insurance.
	static const uint32_t kInputRedundancy = 4;

#pragma pack(push, 1)

	struct NetMsgHeader
	{
		uint8_t  type;        // NetMsgType
		uint8_t  flags;       // reserved (0)
		uint16_t payloadLen;  // number of bytes following this header
	};

	// One entity's transform in a TransformSnapshot batch.
	struct NetTransformEntry
	{
		uint32_t netId;
		float    px, py, pz;
		float    qx, qy, qz, qw;
	};

	// TransformSnapshot payload = NetSnapshotHeader followed by `count` entries.
	struct NetSnapshotHeader
	{
		uint16_t serverTick; // wraps; used to drop out-of-order snapshots
		uint16_t count;
	};

	// Spawn payload = NetSpawnHeader followed by `pathLen` UTF-8 path bytes.
	struct NetSpawnHeader
	{
		uint32_t netId;
		float    px, py, pz;
		float    qx, qy, qz, qw;
		uint16_t pathLen;
	};

	struct NetDespawnBody
	{
		uint32_t netId;
	};

	// One tick of player INTENT. Crucially this is never a position - the server
	// simulates movement from this input under its own rules (speed cap,
	// collision via the character controller), so a client cannot teleport,
	// noclip, or speedhack by lying. `seq` lets the server ack progress and the
	// owning client reconcile its prediction. `yaw` is the facing the client is
	// looking (movement is relative to it). `dt` is the client tick length.
	struct NetInputCmd
	{
		uint32_t seq;
		float    moveX;   // strafe axis [-1,1]
		float    moveZ;   // forward axis [-1,1]
		float    yaw;     // radians
		uint32_t buttons; // NetInputButtons
		float    dt;
	};

	// PlayerInput payload = NetPlayerInputHeader followed by `count` NetInputCmd
	// (most-recent-last), a small redundant window against packet loss.
	struct NetPlayerInputHeader
	{
		uint32_t netId;   // the sender's own player (server validates ownership)
		uint32_t count;
	};

	// PlayerReconcile payload: the server's authoritative pose for the owner's
	// player plus the last input seq the server has applied. The owner snaps to
	// this and replays any inputs newer than lastProcessedSeq.
	struct NetPlayerReconcile
	{
		uint32_t netId;
		float    px, py, pz;
		float    yaw;
		uint32_t lastProcessedSeq;
	};

	// LocalPlayerAssigned payload: tells a client which netId is its own player.
	struct NetLocalPlayerAssigned
	{
		uint32_t netId;
	};

	// PropertyUpdate payload = NetPropUpdateHeader followed by `count` entries,
	// each a NetPropEntryHeader + `valueLen` value bytes. componentHash / propId
	// are CRC32 name hashes so the client routes each value to the right component
	// + member without relying on registration order.
	struct NetPropUpdateHeader
	{
		uint32_t netId;
		uint16_t count;
	};

	struct NetPropEntryHeader
	{
		uint32_t componentHash;
		uint32_t propId;
		uint16_t valueLen;
	};

	// RpcCall payload = NetRpcCall followed by the serialized argument bytes.
	struct NetRpcCall
	{
		uint32_t netId;
		uint32_t componentHash;
		uint32_t rpcId;
	};

#pragma pack(pop)
}
