#pragma once

#include <HexEngine.Core/HexEngine.hpp>
#include <HexEngine.Core/Entity/Entity.hpp>
#include <HexEngine.Core/Scene/Scene.hpp>
#include <deque>

// Resolves the waypoint ("anchor") entities that PlaceOfWork / Residence name.
//
// Buildings are prefabs, and every instance after the first gets its child
// names de-duplicated by the scene ("WP_Entry" -> "WP_Entry2", ...), so a
// prefab can't name its own child waypoint with a scene-unique string. The
// owner's descendants are therefore searched first - exact name, then the
// name followed only by digits (the scene's AutoRename suffix) - before
// falling back to a scene-wide lookup (waypoints authored directly in the
// scene, e.g. lane waypoints).
namespace CityAnchors
{
	inline bool IsDedupedVariant(const std::string& candidate, const std::string& base)
	{
		if (candidate.size() <= base.size() || candidate.compare(0, base.size(), base) != 0)
			return false;
		for (size_t i = base.size(); i < candidate.size(); ++i)
		{
			if (candidate[i] < '0' || candidate[i] > '9')
				return false;
		}
		return true;
	}

	inline HexEngine::Entity* FindDescendant(HexEngine::Entity* root, const std::string& name)
	{
		if (root == nullptr || name.empty())
			return nullptr;

		HexEngine::Entity* variant = nullptr;
		std::deque<HexEngine::Entity*> open;
		for (auto* child : root->GetChildren())
			open.push_back(child);

		while (!open.empty())
		{
			auto* e = open.front();
			open.pop_front();
			if (e == nullptr || e->IsPendingDeletion())
				continue;

			if (e->GetName() == name)
				return e;
			if (variant == nullptr && IsDedupedVariant(e->GetName(), name))
				variant = e;

			for (auto* child : e->GetChildren())
				open.push_back(child);
		}
		return variant;
	}

	inline HexEngine::Entity* Resolve(HexEngine::Entity* owner, const std::string& name)
	{
		if (owner == nullptr || name.empty())
			return nullptr;

		if (auto* local = FindDescendant(owner, name); local != nullptr)
			return local;

		auto* scene = owner->GetScene();
		if (scene == nullptr)
			return nullptr;

		auto* global = scene->GetEntityByName(name);
		return global != nullptr && !global->IsPendingDeletion() ? global : nullptr;
	}

	inline std::string ResolveName(HexEngine::Entity* owner, const std::string& name)
	{
		auto* e = Resolve(owner, name);
		return e != nullptr ? e->GetName() : std::string();
	}

	// Per-owner memo for ResolveName. The routine system asks for a building's
	// entry/parking waypoint every frame per agent, and the uncached answer is a
	// breadth-first walk of the whole building prefab (hundreds of entities) -
	// ~10% of play-mode main-thread time. A hit is re-validated with an O(1)
	// scene name lookup; a miss is retried only every kMissRetry calls so a
	// waypoint added later is still picked up.
	struct ResolveCache
	{
		std::string key;
		std::string result;
		uint32_t missCalls = 0;
	};

	inline std::string ResolveNameCached(HexEngine::Entity* owner, const std::string& name, ResolveCache& cache)
	{
		constexpr uint32_t kMissRetry = 240;
		if (owner == nullptr || name.empty())
			return std::string();

		if (cache.key == name)
		{
			if (!cache.result.empty())
			{
				auto* scene = owner->GetScene();
				auto* e = scene != nullptr ? scene->GetEntityByName(cache.result) : nullptr;
				if (e != nullptr && !e->IsPendingDeletion())
					return cache.result;
			}
			else if (++cache.missCalls < kMissRetry)
			{
				return std::string();
			}
		}

		cache.key = name;
		cache.result = ResolveName(owner, name);
		cache.missCalls = 0;
		return cache.result;
	}
}
