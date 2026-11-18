
#pragma once

#include "../Required.hpp"

#include <string>

namespace HexEngine
{
	class Entity;
	class BaseComponent;
	class FileSystem;

	// Editor-only context surfaced to Core plugins (e.g. the MCP editor
	// bridge). `g_pEnv->_editorContext` is null in shipped game builds and in any
	// headless host; the editor sets it on startup and clears it on shutdown.
	//
	// All methods are invoked on the editor main thread (bridge callers marshal to
	// it), so implementations may touch editor UI state directly.
	class IEditorContext
	{
	public:
		virtual ~IEditorContext() = default;

		// The single entity currently inspected/selected in the editor, or nullptr
		// when nothing is selected. The pointer is only valid on the main thread and
		// only for the duration of the call.
		virtual Entity* GetSelectedEntity() = 0;

		// Select (inspect) an entity in the editor UI, or clear the selection with
		// nullptr. Defaulted so non-editor hosts don't need to care.
		virtual bool SetSelectedEntity(Entity* entity)
		{
			(void)entity;
			return false;
		}

		// Open-project metadata. All return empty strings when no project is open.
		virtual std::string GetProjectName() = 0;       // display name (no extension)
		virtual std::string GetProjectFolderPath() = 0; // absolute project root dir
		virtual std::string GetProjectFilePath() = 0;   // absolute .hexproj path

		// Open a project from its project-file path (an entry from the editor's
		// recent-projects list). Kicks off the same load flow as clicking the
		// project in the project browser; the load itself completes
		// asynchronously on a worker thread with a loading dialog. Fails when a
		// project is already open (switching requires an editor restart) or the
		// file doesn't exist. Defaulted so non-editor hosts don't need to care.
		virtual bool OpenProject(const std::string& projectFilePath, std::string& error)
		{
			error = "opening projects is not supported by this host";
			return false;
		}

		// ---------------------------------------------------------------------
		// Everything below was added for programmatic authoring (editor bridge).
		// All defaulted, so other hosts compile and behave as before. Main
		// thread only.
		// ---------------------------------------------------------------------

		// The open project's file system (resolves asset paths), or nullptr.
		virtual FileSystem* GetProjectFileSystem() { return nullptr; }

		// --- Undo + prefab-override tracking for programmatic edits ---------
		// Everything recorded between Begin/EndUndoGroup becomes ONE undo step.
		// Recording also feeds prefab-instance override tracking, exactly like
		// edits made in the inspector / gizmos.
		virtual void BeginUndoGroup(const std::string& label) { (void)label; }
		virtual void EndUndoGroup() {}
		virtual bool Undo() { return false; }
		virtual bool Redo() { return false; }
		virtual size_t GetUndoCount() { return 0; }
		virtual size_t GetRedoCount() { return 0; }

		virtual void RecordEntityCreated(Entity* entity) { (void)entity; }
		// Call BEFORE destroying / removing (the snapshot is taken now).
		virtual void RecordEntityDeleted(Entity* entity) { (void)entity; }
		virtual void RecordComponentAdded(BaseComponent* component) { (void)component; }
		virtual void RecordComponentDeleted(BaseComponent* component) { (void)component; }
		virtual void RecordEntityRename(Entity* entity, const std::string& before, const std::string& after) { (void)entity; (void)before; (void)after; }
		virtual void RecordEntityParentChange(Entity* entity, Entity* before, Entity* after) { (void)entity; (void)before; (void)after; }
		virtual void RecordEntityVisibilityChange(Entity* entity, bool beforeHidden, bool afterHidden) { (void)entity; (void)beforeHidden; (void)afterHidden; }
		// Records whichever of position / rotation / scale differ from the
		// given "before" values (the entity's current transform is "after").
		virtual void RecordEntityTransformChange(Entity* entity, const math::Vector3& positionBefore, const math::Quaternion& rotationBefore, const math::Vector3& scaleBefore)
		{
			(void)entity; (void)positionBefore; (void)rotationBefore; (void)scaleBefore;
		}
		// Snapshot all of an entity's component state, then (after mutating)
		// record the difference as one property edit.
		virtual void BeginComponentEdit(Entity* entity) { (void)entity; }
		virtual void EndComponentEdit(Entity* entity) { (void)entity; }

		// --- Scenes ------------------------------------------------------------
		// Same as the editor's Save action: project file + every open scene
		// (and the prefab stage, when one is open).
		virtual bool SaveAll(std::string& error) { error = "saving is not supported by this host"; return false; }

		// --- Prefabs ------------------------------------------------------------
		// Instance details: {isInstance, isRoot, sourcePath, rootName,
		// outOfDate, overrides:[{component, path, op}]}.
		virtual json DescribePrefabInstance(Entity* entity) { (void)entity; return json::object(); }
		// Write an instance's changes back into its prefab asset (other
		// instances then update per the editor's sync policy).
		virtual bool ApplyPrefabInstance(Entity* entity, std::string& error) { (void)entity; error = "not supported by this host"; return false; }
		// Discard an instance's overrides; returns the rebuilt instance root.
		virtual Entity* RevertPrefabInstanceToAsset(Entity* entity, std::string& error) { (void)entity; error = "not supported by this host"; return nullptr; }
		// Pull the latest prefab asset into an out-of-date instance.
		virtual bool UpdatePrefabInstance(Entity* entity, std::string& error) { (void)entity; error = "not supported by this host"; return false; }
		// Save `entity` + hierarchy as a NEW prefab asset. With
		// replaceWithInstance the original is swapped for a linked instance of
		// the new prefab (same parent/transform/name; one undo step) and that
		// instance is returned; otherwise the untouched entity is returned.
		virtual Entity* CreatePrefabFromEntity(Entity* entity, const fs::path& prefabPath, bool replaceWithInstance, std::string& error)
		{
			(void)entity; (void)prefabPath; (void)replaceWithInstance; error = "not supported by this host"; return nullptr;
		}
		// Prefab editing stage: while open, the prefab's own scene is the
		// active scene, so ordinary entity/component edits author the prefab.
		virtual bool OpenPrefabEditStage(const fs::path& prefabPath, std::string& error) { (void)prefabPath; error = "not supported by this host"; return false; }
		virtual bool SavePrefabEditStage(std::string& error) { error = "not supported by this host"; return false; }
		virtual bool ClosePrefabEditStage(bool saveChanges, std::string& error) { (void)saveChanges; error = "not supported by this host"; return false; }
		virtual bool IsPrefabEditStageOpen() { return false; }

		// Play mode (the scene view's Run / Stop). Start loads the project's
		// Build/Game.dll if needed; stopping restores the scene to its pre-play
		// state.
		virtual bool StartPlayMode(std::string& error) { error = "not supported by this host"; return false; }
		virtual bool StopPlayMode(std::string& error) { error = "not supported by this host"; return false; }
		virtual bool IsPlayModeActive() { return false; }
	};
}
