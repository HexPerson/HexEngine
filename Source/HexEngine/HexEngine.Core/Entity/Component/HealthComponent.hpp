
#pragma once

#include "UpdateComponent.hpp"
#include "../../Network/IReplicated.hpp"
#include "../../Network/IRpc.hpp"

namespace HexEngine
{
	/**
	 * @brief Example gameplay component exercising the whole netcode stack:
	 *  - _health / _isDead are REPLICATED VARIABLES (server-authoritative, pushed
	 *    to clients; a client editing them is reverted by read-only enforcement).
	 *  - ApplyDamage() is server-side gameplay (guarded by Net::IsServer()).
	 *  - MultiOnDamaged is a MULTICAST RPC (host -> everyone) for hit reactions.
	 *  - ServerRespawn is a SERVER RPC (a client asks the host to respawn it; the
	 *    host validates ownership before running it).
	 *
	 * Drop it on the player prefab (or any networked entity) to see it work.
	 */
	class HEX_API HealthComponent : public UpdateComponent, public IReplicated, public IRpcReceiver
	{
	public:
		CREATE_COMPONENT_ID(HealthComponent);
		DEFINE_COMPONENT_CTOR(HealthComponent);

		virtual ~HealthComponent();

		// Server-side: apply damage. No-op on clients (authority guard). Replicates
		// the new health automatically and multicasts a hit event.
		void ApplyDamage(int32_t amount);

		// Ask the server to respawn this entity (callable from the owning client).
		void RequestRespawn();

		int32_t GetHealth() const { return _health; }
		int32_t GetMaxHealth() const { return _maxHealth; }
		bool    IsDead() const { return _isDead; }

		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;

		// RPC handlers (registered in the BEGIN_RPCS block).
		void ServerRespawn();
		void MultiOnDamaged(int32_t amount);

	private:
		int32_t _health    = 100;
		int32_t _maxHealth = 100;
		bool    _isDead    = false;

		BEGIN_REPLICATED(HealthComponent)
			REPLICATE(_health)
			REPLICATE(_isDead)
		END_REPLICATED()

		BEGIN_RPCS(HealthComponent)
			RPC(ServerRespawn,  HexEngine::RpcDirection::Server,    true)
			RPC(MultiOnDamaged, HexEngine::RpcDirection::Multicast, false)
		END_RPCS()
	};
}
