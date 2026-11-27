#pragma once

#include "../Plugin/IPlugin.hpp"
#include "../Entity/EntityId.hpp"

namespace HexEngine
{
	class ScriptComponent;

	// Gameplay events delivered to a running script besides its lifecycle calls.
	// Physics raises them inside the fixed step; ScriptComponent queues them and
	// hands them to the instance on the main thread, outside the physics callback.
	enum class ScriptEventType : uint8_t
	{
		TriggerEnter,	// OnTriggerEnter(self, other)
		TriggerExit,	// OnTriggerExit(self, other)
		Collision,		// OnCollision(self, other, point)
	};

	struct ScriptEvent
	{
		ScriptEventType type = ScriptEventType::Collision;
		EntityId other = InvalidEntityId;	// the entity on the other side; may be invalid
		math::Vector3 point;				// world contact point (Collision only)
	};

	/** @brief One script bound to one ScriptComponent.
	 *
	 * Created by IScriptEngine::CreateInstance and owned by the component, which
	 * drives it from the main thread only. The instance reads the component's
	 * script path and property overrides, and publishes the script's declared
	 * property defaults back with ScriptComponent::SetScriptPropertyDefaults.
	 *
	 * "Started" means a play session is active for this instance: Start() runs
	 * OnStart when the script is loaded, and a script that fails to load but is
	 * then fixed by a hot reload starts at that moment. Every callback runs
	 * protected: an error is logged with file and line and the instance stops
	 * calling into the script until the file is reloaded.
	 */
	class IScriptInstance
	{
	public:
		virtual ~IScriptInstance() = default;

		// The script compiled and returned a class table.
		virtual bool IsLoaded() const = 0;
		virtual bool IsStarted() const = 0;

		virtual void Start() = 0;
		virtual void Stop() = 0;

		virtual void Update(float dt) = 0;
		virtual void FixedUpdate(float dt) = 0;
		virtual void LateUpdate(float dt) = 0;
		virtual void DispatchEvent(const ScriptEvent& event) = 0;

		// The component's property overrides changed (inspector, bridge, undo,
		// scene restore). A running instance copies the new values onto `self`.
		virtual void OnPropertiesChanged() = 0;
	};

	/** @brief Script runtime provided by a plugin (HexEngine.ScriptingPlugin: Lua).
	 *
	 * Optional: g_pEnv->_scriptEngine is null when no plugin provides it, and
	 * ScriptComponents then stay inert. All calls are main-thread only.
	 */
	class IScriptEngine : public IPluginInterface
	{
	public:
		DECLARE_PLUGIN_INTERFACE(IScriptEngine, 002);

		// The environment deletes the engine through this interface.
		virtual ~IScriptEngine() = default;

		// e.g. "Lua 5.5"
		virtual const char* GetLanguageName() const = 0;

		// e.g. ".lua"
		virtual const char* GetFileExtension() const = 0;

		// Once per frame, before the scene update: hot-reload polling, input
		// edge bookkeeping. realFrameTime is unscaled wall time.
		virtual void Tick(float realFrameTime) = 0;

		// Never null for a valid component. The instance loads the component's
		// current script; a load failure leaves IsLoaded() false and is logged.
		virtual IScriptInstance* CreateInstance(ScriptComponent* owner) = 0;
		virtual void DestroyInstance(IScriptInstance* instance) = 0;

		// Resolve a script reference ("GameData.Scripts/Door.lua", a plain
		// data-relative path, or an absolute path inside a mounted file system)
		// to a file on disk. canonical (optional) receives the "FsName.rel/path"
		// form that ScriptComponent stores.
		virtual bool ResolveScriptPath(const std::string& scriptPath, fs::path& absolutePath, std::string* canonical) = 0;

		// Compile a script without running it. Returns false with "file:line:
		// message" in error. For tools (the editor bridge) and the console.
		virtual bool CheckScript(const std::string& scriptPath, std::string& error) = 0;

		// Recompile every loaded script now (normally done automatically when a
		// file changes on disk).
		virtual void ReloadAllScripts() = 0;
	};
}
