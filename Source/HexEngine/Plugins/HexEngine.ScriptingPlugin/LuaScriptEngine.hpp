#pragma once

#include <HexEngine.Core\HexEngine.hpp>
#include "LuaCommon.hpp"

#include <array>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace HexScripting
{
	class LuaScriptInstance;

	// One script file, compiled once and shared by every ScriptComponent that
	// uses it. Reloaded in place when the file changes on disk.
	struct ScriptAsset
	{
		std::string requestedPath;			// the path as the component spells it
		std::string path;					// canonical "FsName.rel/path.lua" (or requestedPath until resolved)
		fs::path absolute;					// empty while unresolved
		fs::file_time_type writeTime{};
		uintmax_t fileSize = 0;

		bool loaded = false;				// compiled and returned a class table
		std::string error;					// last load error ("file:line: message")
		uint32_t generation = 0;			// bumps on every successful (re)load

		sol::environment env;				// per-script globals (falls back to the sandbox)
		sol::table klass;					// the script's returned table, or env for global-style scripts
		json defaults = json::object();		// declared Properties
	};

	// A module loaded with require(); shared by every script.
	struct ScriptModule
	{
		fs::path absolute;
		fs::file_time_type writeTime{};
		uintmax_t fileSize = 0;
		sol::object value;
	};

	class LuaScriptEngine final : public HexEngine::IScriptEngine, public HexEngine::IInputListener
	{
	public:
		LuaScriptEngine();
		virtual ~LuaScriptEngine();

		// IPluginInterface
		virtual bool Create() override;
		virtual void Destroy() override;

		// IScriptEngine
		virtual const char* GetLanguageName() const override { return "Lua 5.5"; }
		virtual const char* GetFileExtension() const override { return ".lua"; }
		virtual void Tick(float realFrameTime) override;
		virtual HexEngine::IScriptInstance* CreateInstance(HexEngine::ScriptComponent* owner) override;
		virtual void DestroyInstance(HexEngine::IScriptInstance* instance) override;
		virtual bool ResolveScriptPath(const std::string& scriptPath, fs::path& absolutePath, std::string* canonical) override;
		virtual bool CheckScript(const std::string& scriptPath, std::string& error) override;
		virtual void ReloadAllScripts() override;

		// IInputListener
		virtual bool OnInputEvent(HexEngine::InputEvent event, HexEngine::InputData* data) override;

		// --- Used by instances and bindings ---------------------------------
		sol::state& Lua() { return *_lua; }

		// Shared asset for a script path; loads it on first use. Never null.
		std::shared_ptr<ScriptAsset> AcquireScript(const std::string& scriptPath);

		// Call a Lua function under the instruction budget with `context` as the
		// running script (Scene.* resolves against its scene, print() names
		// it). Returns false with the error (and traceback) in `error`.
		template <typename... Args>
		bool Call(LuaScriptInstance* context, const sol::object& fn, std::string& error, Args&&... args)
		{
			if (!fn.is<sol::function>())
				return true;

			ContextScope scope(*this, context);
			sol::protected_function pf(fn);
			pf.set_error_handler(_tracebackHandler);
			sol::protected_function_result result = pf(std::forward<Args>(args)...);
			if (!result.valid())
			{
				sol::error err = result;
				error = TrimTraceback(err.what());
				return false;
			}
			return true;
		}

		LuaScriptInstance* GetCurrentInstance() const { return _current; }
		HexEngine::Scene* GetCurrentScene() const;
		std::shared_ptr<HexEngine::Scene> GetSharedScene(HexEngine::Scene* scene) const;
		bool IsSceneAlive(HexEngine::Scene* scene) const;

		// Display name for log lines: "Rotator.lua (Cube)".
		std::string DescribeContext() const;

		// Input (virtual-key codes; mouse buttons are VK_LBUTTON etc.).
		bool IsKeyDown(int32_t vk) const { return vk > 0 && vk < 256 && _keyDown[vk]; }
		bool WasKeyPressed(int32_t vk) const { return vk > 0 && vk < 256 && _keyPressedFrame[vk]; }
		bool WasKeyReleased(int32_t vk) const { return vk > 0 && vk < 256 && _keyReleasedFrame[vk]; }
		const std::vector<std::pair<int32_t, bool>>& GetFrameKeyEvents() const { return _frameKeyEvents; }
		math::Vector2 GetMouseDelta() const { return _frameMouseDelta; }

		float GetGameTime() const { return _gameTime; }

		// require(): load (or return the cached) module.
		sol::object RequireModule(const std::string& modulePath);

	private:
		struct ContextScope
		{
			ContextScope(LuaScriptEngine& e, LuaScriptInstance* ctx) : engine(e), previous(e._current)
			{
				engine._current = ctx;
				if (engine._callDepth++ == 0)
					engine._budgetTicks = 0;
			}
			~ContextScope()
			{
				--engine._callDepth;
				engine._current = previous;
			}
			LuaScriptEngine& engine;
			LuaScriptInstance* previous;
		};

		void CreateState();
		void BuildSandbox();
		void LoadAsset(ScriptAsset& asset);
		bool ReadScriptSource(const fs::path& absolute, std::string& source, std::string& error);
		json ExtractPropertyDefaults(const sol::table& klass, const std::string& scriptPath);
		void PollFileChanges();
		void OnAssetReloaded(const std::shared_ptr<ScriptAsset>& asset);
		void ResetInputState();

		static void InstructionHook(lua_State* L, lua_Debug* ar);

	private:
		std::unique_ptr<sol::state> _lua;
		sol::function _tracebackHandler;

		std::unordered_map<std::string, std::shared_ptr<ScriptAsset>> _assets;		// key: requested path
		std::unordered_map<std::string, ScriptModule> _modules;						// key: canonical path
		std::unordered_set<std::string> _modulesLoading;							// require() cycle guard
		std::unordered_set<LuaScriptInstance*> _instances;

		LuaScriptInstance* _current = nullptr;
		int32_t _callDepth = 0;
		uint32_t _budgetTicks = 0;

		bool _created = false;
		bool _inputRegistered = false;
		bool _wasGameRunning = false;
		float _pollTimer = 0.0f;
		float _gameTime = 0.0f;

		// Key state. Events land in _pending*; Tick publishes them as this
		// frame's edges so every script sees the same pressed/released set.
		std::array<bool, 256> _keyDown{};
		std::array<bool, 256> _keyPressedFrame{};
		std::array<bool, 256> _keyReleasedFrame{};
		std::array<bool, 256> _keyPressedPending{};
		std::array<bool, 256> _keyReleasedPending{};
		std::vector<std::pair<int32_t, bool>> _frameKeyEvents;
		std::vector<std::pair<int32_t, bool>> _pendingKeyEvents;
		math::Vector2 _frameMouseDelta;
		math::Vector2 _pendingMouseDelta;
	};

	// Set by LuaScriptEngine::Create; the bindings and the instruction hook use it.
	inline LuaScriptEngine* g_pScriptEngine = nullptr;
}
