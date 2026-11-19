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
}
