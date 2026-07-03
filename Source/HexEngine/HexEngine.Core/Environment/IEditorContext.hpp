
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
	};
}
