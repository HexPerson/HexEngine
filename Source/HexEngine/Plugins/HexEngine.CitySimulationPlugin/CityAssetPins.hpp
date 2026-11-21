#pragma once

#include <HexEngine.Core/HexEngine.hpp>
#include <HexEngine.Core/Scene/Prefab.hpp>
#include <HexEngine.Core/Audio/SoundEffect.hpp>
#include <string>
#include <unordered_map>

// Keeps the assets that city simulation spawns at runtime resident while the
// game runs: traffic + commuter car prefabs and their engine/horn sounds.
//
// The ResourceSystem caches a resource only while something holds it. A spawn
// does Create -> clone/instantiate -> drop, so every car (and every honk)
// unloaded the asset and the next one re-read and re-parsed it from disk - a
// load/unload pair per spawn in the log and a chunk of each spawn's hitch.
// Pinning keeps one reference per path; Clear() runs when play stops so the
// editor doesn't hold them. Main thread only.
namespace CityAssetPins
{
	inline std::unordered_map<std::string, std::shared_ptr<HexEngine::Prefab>>& Prefabs()
	{
		static std::unordered_map<std::string, std::shared_ptr<HexEngine::Prefab>> pins;
		return pins;
	}

	inline std::unordered_map<std::string, std::shared_ptr<HexEngine::SoundEffect>>& Sounds()
	{
		static std::unordered_map<std::string, std::shared_ptr<HexEngine::SoundEffect>> pins;
		return pins;
	}

	inline std::shared_ptr<HexEngine::Prefab> Prefab(const fs::path& path)
	{
		if (path.empty())
			return nullptr;
		auto& slot = Prefabs()[path.string()];
		if (slot == nullptr)
			slot = HexEngine::Prefab::Create(path);
		return slot;
	}

	inline std::shared_ptr<HexEngine::SoundEffect> Sound(const std::string& path)
	{
		if (path.empty())
			return nullptr;
		auto& slot = Sounds()[path];
		if (slot == nullptr)
			slot = HexEngine::SoundEffect::Create(path);
		return slot;
	}

	inline void Clear()
	{
		Prefabs().clear();
		Sounds().clear();
	}
}
