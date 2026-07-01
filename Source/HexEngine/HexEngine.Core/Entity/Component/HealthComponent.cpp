
#include "HealthComponent.hpp"
#include "../Entity.hpp"
#include "../../Network/Net.hpp"
#include "../../Environment/LogFile.hpp"

namespace HexEngine
{
	HealthComponent::HealthComponent(Entity* entity) :
		UpdateComponent(entity)
	{
	}

	HealthComponent::HealthComponent(Entity* entity, HealthComponent* copy) :
		UpdateComponent(entity, copy)
	{
		if (copy != nullptr)
		{
			_health    = copy->_health;
			_maxHealth = copy->_maxHealth;
			_isDead    = copy->_isDead;
		}
	}

	HealthComponent::~HealthComponent()
	{
	}

	void HealthComponent::ApplyDamage(int32_t amount)
	{
		// Authority guard: health only ever changes on the server. On a client this
		// is a no-op (and read-only enforcement would revert it anyway).
		if (!Net::IsServer())
			return;
		if (_isDead || amount <= 0)
			return;

		_health -= amount;
		if (_health <= 0)
		{
			_health = 0;
			_isDead = true;
		}
		// _health / _isDead now replicate to all clients automatically (netvars).
		// Tell everyone to play a hit reaction (multicast RPC).
		CallRpc(this, "MultiOnDamaged", amount);
	}

	void HealthComponent::RequestRespawn()
	{
		// On a client this sends a Server RPC to the host; on the host (or offline)
		// it runs ServerRespawn locally.
		CallRpc(this, "ServerRespawn");
	}

	void HealthComponent::ServerRespawn()
	{
		if (!Net::IsServer())
			return;
		_health = _maxHealth;
		_isDead = false;
	}

	void HealthComponent::MultiOnDamaged(int32_t amount)
	{
		// Runs on the server AND every client - hook hit VFX/SFX here.
		LOG_INFO("HealthComponent: hit for %d (health %d/%d%s).",
			amount, _health, _maxHealth, _isDead ? ", DEAD" : "");
	}

	void HealthComponent::Serialize(json& data, JsonFile* file)
	{
		SERIALIZE_VALUE(_health);
		SERIALIZE_VALUE(_maxHealth);
	}

	void HealthComponent::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		(void)mask;
		DESERIALIZE_VALUE(_health);
		DESERIALIZE_VALUE(_maxHealth);
	}
}
