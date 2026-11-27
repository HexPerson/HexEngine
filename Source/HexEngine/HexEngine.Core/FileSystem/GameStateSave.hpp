#pragma once

#include "JsonFile.hpp"
#include <memory>
#include <string>

namespace HexEngine
{
	class Scene;

	/**
	 * @brief Game save/load as a diff over the authored scene.
	 *
	 * CaptureBaseline() records every saveable entity as the level was authored (per
	 * component, exactly as the scene codec writes it) once the level has loaded and
	 * before play changes it. Save() then writes only the components that differ from
	 * that baseline, plus the baseline entities that were destroyed - a save is small
	 * and the static world (buildings, roads, terrain) is never rewritten.
	 *
	 * Load() brings the LIVE scene to baseline + save: components that already match
	 * are left alone (re-deserialising an unchanged component is not free - terrain
	 * recooks its colliders), everything else is re-deserialised with transforms
	 * first, destroyed baseline entities are recreated and saved-as-destroyed ones are
	 * removed. ResetToBaseline() is the same with an empty save (a new game).
	 *
	 * Components that keep runtime game state (an active job, money, a door's open
	 * state...) write it in Serialize() only when file->IsGameStateSave() - scene and
	 * prefab saves never see it - and read it back in Deserialize() when present.
	 *
	 * Entities spawned at runtime (not in the baseline) are not saved: the systems
	 * that own them (traffic, routines) respawn them. DoNotSave entities are ignored,
	 * as everywhere else.
	 */
	class HEX_API GameStateSave
	{
	public:
		static constexpr int32_t Version = 1;

		/** Snapshot the scene as it is now as the baseline saves are diffed against. */
		static void CaptureBaseline(const std::shared_ptr<Scene>& scene);
		static void ClearBaseline();
		static bool HasBaseline(const Scene* scene = nullptr);

		/** Write a save. `header` is free-form game metadata (slot name, money...) that
		 *  ReadHeader() returns without applying anything. */
		static bool Save(const std::shared_ptr<Scene>& scene, const fs::path& path, const json& header, std::string* error = nullptr);

		/** The same save, time-sliced for autosaves: BeginSave, then StepSave once a
		 *  frame until it returns true (`written` says whether the file was written).
		 *  Starting another save, or ClearBaseline, drops one in flight. */
		static bool BeginSave(const std::shared_ptr<Scene>& scene, const fs::path& path, const json& header, std::string* error = nullptr);
		static bool StepSave(float budgetMs, bool* written = nullptr, std::string* error = nullptr);
		static bool IsSaving();
		static void CancelSave();

		/** Bring the live scene to baseline + the save at `path`. */
		static bool Load(const std::shared_ptr<Scene>& scene, const fs::path& path, json* headerOut = nullptr, std::string* error = nullptr);

		/** Bring the live scene back to the baseline (as authored). */
		static bool ResetToBaseline(const std::shared_ptr<Scene>& scene, std::string* error = nullptr);

		static bool ReadHeader(const fs::path& path, json& header);
	};
}
