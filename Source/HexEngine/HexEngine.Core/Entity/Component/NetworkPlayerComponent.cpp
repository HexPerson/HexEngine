
#include "NetworkPlayerComponent.hpp"
#include "NetworkComponent.hpp"
#include "Transform.hpp"
#include "Camera.hpp"
#include "RigidBody.hpp"
#include "../Entity.hpp"
#include "../../Physics/IRigidBody.hpp"
#include "../../Network/INetworkSystem.hpp"
#include "../../Input/InputSystem.hpp"
#include "../../Scene/Scene.hpp"
#include "../../Scene/SceneManager.hpp"
#include "../../Scene/NetworkReplicationSystem.hpp"
#include "../../Environment/IEnvironment.hpp"

#include <cmath>
#include <algorithm>

namespace HexEngine
{
	static constexpr float kGroundStickSpeed = 4.0f;
	static constexpr float kReconcileEpsilon = 0.05f; // metres of divergence before we correct
	static const size_t    kMaxHistory       = 128;

	NetworkPlayerComponent::NetworkPlayerComponent(Entity* entity) :
		UpdateComponent(entity)
	{
	}

	NetworkPlayerComponent::NetworkPlayerComponent(Entity* entity, NetworkPlayerComponent* copy) :
		UpdateComponent(entity, copy)
	{
		if (copy != nullptr)
		{
			_moveSpeed     = copy->_moveSpeed;
			_strafeSpeed   = copy->_strafeSpeed;
			_runMultiplier = copy->_runMultiplier;
			_jumpSpeed     = copy->_jumpSpeed;
			_gravity       = copy->_gravity;
			_maxSpeed      = copy->_maxSpeed;
		}
	}

	NetworkPlayerComponent::~NetworkPlayerComponent()
	{
	}

	IRigidBody* NetworkPlayerComponent::ResolveController()
	{
		if (!_resolved)
		{
			// The player's CCT is a RigidBody SUBCLASS (CharacterController), so an
			// exact-id GetComponent<RigidBody> would miss it - use the derived query.
			_rigidBody = GetEntity()->GetComponentDerived<RigidBody>();
			_resolved = true;
		}
		return (_rigidBody != nullptr) ? _rigidBody->GetIRigidBody() : nullptr;
	}

	void NetworkPlayerComponent::ApplyInput(const NetInputCmd& cmd, float dt)
	{
		IRigidBody* cct = ResolveController();
		if (cct == nullptr)
			return;

		_yaw = cmd.yaw;

		// Sanitise the axes server-side: intent is a unit-ish direction, never a
		// magnitude. Combined with the speed cap below this makes speedhacking by
		// inflating the axes impossible.
		const float moveX = std::clamp(cmd.moveX, -1.0f, 1.0f);
		const float moveZ = std::clamp(cmd.moveZ, -1.0f, 1.0f);

		const float s = std::sin(_yaw);
		const float c = std::cos(_yaw);
		const math::Vector3 forward(s, 0.0f, c);
		const math::Vector3 right(c, 0.0f, -s);

		const float fwdSpeed = _moveSpeed * ((cmd.buttons & NetBtn_Sprint) ? _runMultiplier : 1.0f);
		math::Vector3 planar = forward * (moveZ * fwdSpeed) + right * (moveX * _strafeSpeed);

		// Server horizontal speed cap - the wall that enforces "no speedhack".
		const float hs = planar.Length();
		if (hs > _maxSpeed && hs > 0.0f)
			planar *= (_maxSpeed / hs);

		// Vertical: gravity in air, ground-stick when grounded, jump on request.
		if (cct->IsOnGround())
			_verticalVelocity = (cmd.buttons & NetBtn_Jump) ? _jumpSpeed : -kGroundStickSpeed;
		else
			_verticalVelocity -= _gravity * dt;

		const math::Vector3 disp = (planar + math::Vector3::Up * _verticalVelocity) * dt;
		cct->Move(disp, 0.0f, dt);

		if (auto* tf = GetEntity()->GetComponent<Transform>())
			tf->SetRotation(math::Quaternion::CreateFromYawPitchRoll(_yaw, 0.0f, 0.0f));
	}

	void NetworkPlayerComponent::SampleLocalInput(NetInputCmd& out, float dt)
	{
		out.moveX = 0.0f;
		out.moveZ = 0.0f;
		out.buttons = 0;

		InputSystem* in = (g_pEnv != nullptr) ? g_pEnv->_inputSystem : nullptr;
		if (in != nullptr && in->IsInputEnabled())
		{
			if (GetAsyncKeyState('W') & 0x8000)      out.moveZ += 1.0f;
			if (GetAsyncKeyState('S') & 0x8000)      out.moveZ -= 1.0f;
			if (GetAsyncKeyState('D') & 0x8000)      out.moveX += 1.0f;
			if (GetAsyncKeyState('A') & 0x8000)      out.moveX -= 1.0f;
			if (GetAsyncKeyState(VK_SPACE) & 0x8000) out.buttons |= NetBtn_Jump;
			if (GetAsyncKeyState(VK_SHIFT) & 0x8000) out.buttons |= NetBtn_Sprint;

			_yaw   -= in->GetXAxis();
			_pitch -= in->GetYAxis();
		}

		out.yaw = _yaw;
		out.dt = dt;
	}

	void NetworkPlayerComponent::UpdateLocalView()
	{
		if (auto* cam = GetEntity()->GetComponent<Camera>())
		{
			cam->SetYaw(_yaw);
			cam->SetPitch(_pitch);
		}
	}

	void NetworkPlayerComponent::EnqueueRemoteInput(const NetInputCmd& cmd)
	{
		if (cmd.seq <= _lastProcessedSeq)
			return;
		for (const NetInputCmd& q : _remoteInputs)
			if (q.seq == cmd.seq)
				return; // duplicate from the redundancy window
		_remoteInputs.push_back(cmd);
		while (_remoteInputs.size() > kMaxHistory)
			_remoteInputs.pop_front();
	}

	void NetworkPlayerComponent::ReceiveReconcile(const math::Vector3& footPos, float yaw, uint32_t lastProcessedSeq)
	{
		IRigidBody* cct = ResolveController();
		if (cct == nullptr)
			return;

		_lastProcessedSeq = lastProcessedSeq;

		// Predicted foot position at the acked sequence (to measure divergence).
		math::Vector3 predictedAtSeq;
		bool found = false;
		for (const PredEntry& e : _history)
		{
			if (e.cmd.seq == lastProcessedSeq)
			{
				predictedAtSeq = e.footPos;
				found = true;
				break;
			}
		}

		// Drop everything the server has already applied.
		_history.erase(
			std::remove_if(_history.begin(), _history.end(),
				[lastProcessedSeq](const PredEntry& e) { return e.cmd.seq <= lastProcessedSeq; }),
			_history.end());

		const float err = found ? (predictedAtSeq - footPos).Length() : 1e9f;
		if (err > kReconcileEpsilon)
		{
			// Misprediction: snap to the authoritative pose and replay the inputs
			// the server hasn't acked yet, so the local player ends up where the
			// server will agree it is - without ever having trusted a client pos.
			cct->UpdatePosePosition(footPos);
			_yaw = yaw;
			for (PredEntry& e : _history)
			{
				ApplyInput(e.cmd, e.cmd.dt);
				e.footPos = cct->GetPhysicsPosition();
			}
		}
	}

	void NetworkPlayerComponent::FixedUpdate(float dt)
	{
		INetworkSystem* net = (g_pEnv != nullptr) ? g_pEnv->_networkSystem : nullptr;
		if (net == nullptr || net->GetRole() == INetworkSystem::NetRole::None)
			return;

		NetworkComponent* nc = GetEntity()->GetComponent<NetworkComponent>();
		if (nc == nullptr)
			return; // a networked player must also carry a NetworkComponent (identity)

		const uint32_t myNetId = nc->GetEffectiveNetId();

		auto scene = (g_pEnv->_sceneManager != nullptr) ? g_pEnv->_sceneManager->GetCurrentScene() : nullptr;
		NetworkReplicationSystem* rep = scene ? scene->GetNetworkReplicationSystem() : nullptr;
		if (rep == nullptr)
			return;

		if (net->GetRole() == INetworkSystem::NetRole::Host)
		{
			if (nc->GetOwnerConnId() == 0)
			{
				// The host's own player: sample local input and simulate.
				NetInputCmd cmd;
				SampleLocalInput(cmd, dt);
				cmd.seq = ++_seq;
				ApplyInput(cmd, dt);
				_lastProcessedSeq = cmd.seq;
				UpdateLocalView();
			}
			else
			{
				// A client's player: drain the input this client sent us and
				// simulate it authoritatively (in-order, skipping already-applied).
				while (!_remoteInputs.empty())
				{
					const NetInputCmd cmd = _remoteInputs.front();
					_remoteInputs.pop_front();
					if (cmd.seq <= _lastProcessedSeq)
						continue;
					ApplyInput(cmd, dt);
					_lastProcessedSeq = cmd.seq;
				}
			}
		}
		else // Client
		{
			if (myNetId != 0 && rep->GetLocalPlayerNetId() == myNetId)
			{
				// This is OUR player: sample input, send it (redundant), predict.
				NetInputCmd cmd;
				SampleLocalInput(cmd, dt);
				cmd.seq = ++_seq;

				ApplyInput(cmd, dt);
				IRigidBody* cct = ResolveController();
				const math::Vector3 foot = (cct != nullptr) ? cct->GetPhysicsPosition() : math::Vector3::Zero;
				_history.push_back({ cmd, foot });
				while (_history.size() > kMaxHistory)
					_history.erase(_history.begin());

				// Send the last few commands as a redundant window against loss.
				NetInputCmd redun[kInputRedundancy];
				uint32_t rc = 0;
				const size_t n = _history.size();
				const size_t start = (n > kInputRedundancy) ? (n - kInputRedundancy) : 0;
				for (size_t i = start; i < n; ++i)
					redun[rc++] = _history[i].cmd;
				rep->SendPlayerInput(myNetId, redun, rc);

				UpdateLocalView();
			}
			// Remote players: nothing here - NetworkComponent interpolates them.
		}
	}

	void NetworkPlayerComponent::Serialize(json& data, JsonFile* file)
	{
		SERIALIZE_VALUE(_moveSpeed);
		SERIALIZE_VALUE(_strafeSpeed);
		SERIALIZE_VALUE(_runMultiplier);
		SERIALIZE_VALUE(_jumpSpeed);
		SERIALIZE_VALUE(_gravity);
		SERIALIZE_VALUE(_maxSpeed);
	}

	void NetworkPlayerComponent::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		(void)mask;
		DESERIALIZE_VALUE(_moveSpeed);
		DESERIALIZE_VALUE(_strafeSpeed);
		DESERIALIZE_VALUE(_runMultiplier);
		DESERIALIZE_VALUE(_jumpSpeed);
		DESERIALIZE_VALUE(_gravity);
		DESERIALIZE_VALUE(_maxSpeed);
	}
}
