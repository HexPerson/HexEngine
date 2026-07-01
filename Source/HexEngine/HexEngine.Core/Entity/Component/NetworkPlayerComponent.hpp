
#pragma once

#include "UpdateComponent.hpp"
#include "../../Scene/NetworkMessages.hpp"

#include <vector>
#include <deque>

namespace HexEngine
{
	class RigidBody;
	class IRigidBody;

	/**
	 * @brief Server-authoritative networked player controller (input-driven).
	 *
	 * Security model: a client NEVER asserts a position. The owning client streams
	 * INPUT (which keys, look yaw) and the HOST simulates the movement through the
	 * PhysX character controller under its own rules (collision + a server speed
	 * cap), so teleport / noclip / speedhack are structurally impossible. The host
	 * is the sole authority on where the player ends up.
	 *
	 * Roles (decided per-frame from g_pEnv->_networkSystem->GetRole() and ownership):
	 *  - Host, host's own player (ownerConnId == 0): samples local input, simulates.
	 *  - Host, a client's player: drains that client's received input, simulates,
	 *    and sends PlayerReconcile back to the owner.
	 *  - Client, the local player: samples input, sends it, PREDICTS locally, and
	 *    reconciles against the server correction (snap + replay of unacked input).
	 *  - Client, a remote player: does nothing here - NetworkComponent interpolates.
	 *
	 * Movement runs in FixedUpdate with the engine's fixed timestep so host sim and
	 * client prediction step identically (a prerequisite for clean reconciliation).
	 */
	class HEX_API NetworkPlayerComponent : public UpdateComponent
	{
	public:
		CREATE_COMPONENT_ID(NetworkPlayerComponent);
		DEFINE_COMPONENT_CTOR(NetworkPlayerComponent);

		virtual ~NetworkPlayerComponent();

		virtual void FixedUpdate(float dt) override;

		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;

		// Host: queue an input command received from the owning client.
		void EnqueueRemoteInput(const NetInputCmd& cmd);

		// Owning client: apply the server's authoritative correction - snap the
		// controller to footPos, then replay any inputs newer than lastProcessedSeq.
		void ReceiveReconcile(const math::Vector3& footPos, float yaw, uint32_t lastProcessedSeq);

		uint32_t GetLastProcessedSeq() const { return _lastProcessedSeq; }
		float    GetYaw() const { return _yaw; }

	private:
		IRigidBody* ResolveController();
		// The shared movement step - identical on host (authority) and owning
		// client (prediction). Advances the CCT by one fixed tick from `cmd`.
		void ApplyInput(const NetInputCmd& cmd, float dt);
		void SampleLocalInput(NetInputCmd& out, float dt);
		void UpdateLocalView(); // apply yaw/pitch to a Camera on this entity, if present

		// Authored config (serialized). _maxSpeed is the server-side horizontal
		// speed cap - the wall that makes speedhacking impossible.
		float _moveSpeed      = 4.0f;
		float _strafeSpeed    = 3.0f;
		float _runMultiplier  = 2.0f;
		float _jumpSpeed      = 7.0f;
		float _gravity        = 19.62f;
		float _maxSpeed       = 8.0f;

		// Runtime.
		float _verticalVelocity = 0.0f;
		float _yaw = 0.0f;
		float _pitch = 0.0f;
		uint32_t _seq = 0;              // client: next input sequence number
		uint32_t _lastProcessedSeq = 0; // host: last applied; client: last acked

		std::deque<NetInputCmd> _remoteInputs;      // host: inputs from the owning client
		struct PredEntry { NetInputCmd cmd; math::Vector3 footPos; };
		std::vector<PredEntry> _history;            // client owner: for reconciliation
		NetInputCmd _redundancy[kInputRedundancy];  // client: recent cmds resent for loss
		uint32_t _redundancyCount = 0;

		RigidBody* _rigidBody = nullptr;
		bool _resolved = false;
	};
}
