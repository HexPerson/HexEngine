
#include "HCommand.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../Environment/LogFile.hpp"
#include "CommandManager.hpp"
#include "../Scene/Scene.hpp"
#include "../Entity/Component/UpdateComponent.hpp"
#include <algorithm>
#include <chrono>
#include <random>

namespace HexEngine
{
	HEX_COMMAND(ecs_bench)
	{
		if (!pressed)
			return;

		uint32_t entityCount = 5000;
		uint32_t churnOps = 20000;
		uint32_t seed = 1337;

		if (args->Count() > 0)
			entityCount = std::max(1u, args->Get<uint32_t>(0));
		if (args->Count() > 1)
			churnOps = std::max(1u, args->Get<uint32_t>(1));
		if (args->Count() > 2)
			seed = args->Get<uint32_t>(2);

		Scene benchScene;
		benchScene.CreateEmpty(false);

		std::vector<EntityId> ids;
		ids.reserve(entityCount);

		using Clock = std::chrono::high_resolution_clock;
		auto startCreate = Clock::now();
		for (uint32_t i = 0; i < entityCount; ++i)
		{
			ids.push_back(benchScene.CreateEntityId("ecs_bench_entity_" + std::to_string(i)));
		}
		auto endCreate = Clock::now();

		auto startAdd = Clock::now();
		uint32_t addCount = 0;
		for (const EntityId id : ids)
		{
			Entity* entity = benchScene.TryGetEntity(id);
			if (entity == nullptr)
				continue;

			entity->AddComponent<UpdateComponent>();
			++addCount;
		}
		auto endAdd = Clock::now();

		auto startGet = Clock::now();
		uint32_t getCount = 0;
		for (const EntityId id : ids)
		{
			if (benchScene.GetComponent<UpdateComponent>(id) != nullptr)
				++getCount;
		}
		auto endGet = Clock::now();

		auto startChurn = Clock::now();
		uint32_t churnAdds = 0;
		uint32_t churnRemoves = 0;
		std::mt19937 rng(seed);
		std::uniform_int_distribution<uint32_t> dist(0, static_cast<uint32_t>(ids.size() - 1));

		for (uint32_t op = 0; op < churnOps; ++op)
		{
			const EntityId id = ids[dist(rng)];
			Entity* entity = benchScene.TryGetEntity(id);
			if (entity == nullptr)
				continue;

			if (benchScene.HasComponent<UpdateComponent>(id))
			{
				entity->RemoveComponentById(UpdateComponent::_GetComponentId());
				++churnRemoves;
			}
			else
			{
				entity->AddComponent<UpdateComponent>();
				++churnAdds;
			}
		}
		auto endChurn = Clock::now();

		auto startDestroy = Clock::now();
		for (const EntityId id : ids)
		{
			benchScene.DestroyEntity(id, false);
		}
		auto endDestroy = Clock::now();

		auto toMs = [](const auto& start, const auto& end) -> double
		{
			return std::chrono::duration<double, std::milli>(end - start).count();
		};

		const double createMs = toMs(startCreate, endCreate);
		const double addMs = toMs(startAdd, endAdd);
		const double getMs = toMs(startGet, endGet);
		const double churnMs = toMs(startChurn, endChurn);
		const double destroyMs = toMs(startDestroy, endDestroy);

		auto opsPerSec = [](double ms, uint32_t ops) -> double
		{
			if (ms <= 0.0)
				return 0.0;
			return (static_cast<double>(ops) / ms) * 1000.0;
		};

		CON_ECHO("^g[ECS Bench] entities=%u churnOps=%u seed=%u", entityCount, churnOps, seed);
		CON_ECHO("^g[ECS Bench] create:  %.3f ms (%.0f ops/s)", createMs, opsPerSec(createMs, entityCount));
		CON_ECHO("^g[ECS Bench] add:     %.3f ms (%.0f ops/s)", addMs, opsPerSec(addMs, addCount));
		CON_ECHO("^g[ECS Bench] get:     %.3f ms (%.0f ops/s, hits=%u)", getMs, opsPerSec(getMs, entityCount), getCount);
		CON_ECHO("^g[ECS Bench] churn:   %.3f ms (%.0f ops/s, add=%u remove=%u)", churnMs, opsPerSec(churnMs, churnOps), churnAdds, churnRemoves);
		CON_ECHO("^g[ECS Bench] destroy: %.3f ms (%.0f ops/s)", destroyMs, opsPerSec(destroyMs, entityCount));

		benchScene.Destroy();
	}
}
