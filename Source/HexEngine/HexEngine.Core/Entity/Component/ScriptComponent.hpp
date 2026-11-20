#pragma once

#include "UpdateComponent.hpp"
#include "../../Scripting/IScriptEngine.hpp"

#include <atomic>
#include <mutex>

namespace HexEngine
{
	class LineEdit;
	class ComponentWidget;

	/** @brief Runs a gameplay script (Lua, through the IScriptEngine plugin) on its entity.
	 *
	 * Serialised fields:
	 *   "script"     - resource path of the script, e.g. "GameData.Scripts/Rotator.lua"
	 *   "properties" - the script's exposed variables (its `Properties` table),
	 *                  as an object of name -> value. Reads show every declared
	 *                  property (script default unless overridden); writes keep
	 *                  only the values that differ from the script's default.
	 *
	 * Scripts run only while the game is running (play mode / standalone), on
	 * gameplay time: OnUpdate/OnLateUpdate get the scaled frame time and are
	 * skipped while the simulation is paused; OnFixedUpdate runs per fixed tick.
	 * In edit mode the script is still compiled (so its properties appear in the
	 * inspector and the editor bridge) but no callback runs.
	 */
	class HEX_API ScriptComponent : public UpdateComponent
	{
	public:
		CREATE_COMPONENT_ID(ScriptComponent);
		DEFINE_COMPONENT_CTOR(ScriptComponent);

		virtual ~ScriptComponent();

		virtual void Destroy() override;

		void SetScript(const std::string& scriptPath);
		const std::string& GetScript() const { return _scriptPath; }

		// --- Properties ---------------------------------------------------
		// Declared defaults, published by the script engine when the script
		// (re)loads. Main thread.
		void SetScriptPropertyDefaults(const json& defaults);
		const json& GetScriptPropertyDefaults() const { return _propertyDefaults; }

		// Values that differ from the script defaults.
		const json& GetPropertyOverrides() const { return _propertyOverrides; }

		// defaults overlaid with overrides: what `self` starts with.
		json GetEffectiveProperties() const;

		// Set one property (script-side writes go through `self` instead).
		void SetProperty(const std::string& name, const json& value);

		IScriptInstance* GetScriptInstance() const { return _instance; }

		// Called by the script engine when it shuts down before this component:
		// forget the instance (the engine deletes it).
		void OnScriptEngineShutdown() { _instance = nullptr; }

		// --- UpdateComponent / BaseComponent --------------------------------
		virtual void Update(float frameTime) override;
		virtual void FixedUpdate(float frameTime) override;
		virtual void LateUpdate(float frameTime) override;

		virtual void OnMessage(Message* message, MessageListener* sender) override;

		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;

		virtual bool CreateWidget(ComponentWidget* widget) override;

	private:
		// True when scripts should be executing for this entity right now.
		bool ShouldRun();
		// Create / recreate the instance as needed and sync lifecycle. Returns
		// the instance when it is started (callbacks may run).
		IScriptInstance* SyncInstance();
		void ReleaseInstance(bool callStop);
		void DispatchPendingEvents();
		void PruneOverrides();
		void QueueEvent(const ScriptEvent& event);
		void SetScriptFromWidget(LineEdit* edit, const fs::path& path);

	private:
		std::string _scriptPath;
		json _propertyOverrides = json::object();
		json _propertyDefaults = json::object();
		// _propertyDefaults belongs to this script path (not stale from a previous one).
		bool _defaultsValid = false;

		IScriptInstance* _instance = nullptr;
		// Deserialize may run on the scene-loader thread: it only flags changes;
		// the main-thread Update applies them to the instance.
		std::atomic<bool> _recreateInstance = false;
		std::atomic<bool> _propertiesDirty = false;

		std::mutex _eventLock;
		std::vector<ScriptEvent> _pendingEvents;
	};
}
