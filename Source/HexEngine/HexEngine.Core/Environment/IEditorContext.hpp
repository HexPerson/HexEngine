
#pragma once

#include <string>

namespace HexEngine
{
	class Entity;

	// Editor-only context surfaced to Core plugins (e.g. the read-only MCP editor
	// bridge). `g_pEnv->_editorContext` is null in shipped game builds and in any
	// headless host; the editor sets it on startup and clears it on shutdown.
	//
	// All methods are invoked on the editor main thread (bridge callers marshal to
	// it), so implementations may touch editor UI state directly. Read-only.
	class IEditorContext
	{
	public:
		virtual ~IEditorContext() = default;

		// The single entity currently inspected/selected in the editor, or nullptr
		// when nothing is selected. The pointer is only valid on the main thread and
		// only for the duration of the call.
		virtual Entity* GetSelectedEntity() = 0;

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
	};
}
