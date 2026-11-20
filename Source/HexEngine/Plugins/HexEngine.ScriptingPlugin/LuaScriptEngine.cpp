
#include "LuaScriptEngine.hpp"
#include "LuaScriptInstance.hpp"

#include <fstream>
#include <sstream>

namespace HexScripting
{
	using namespace HexEngine;

	namespace
	{
		// Instruction budget per top-level call: the hook fires every
		// kHookInterval VM instructions; past kMaxHookTicks the call errors out
		// instead of freezing the editor (an accidental `while true do end`).
		constexpr int kHookInterval = 10000;
		constexpr uint32_t kMaxHookTicks = 20000; // 200M instructions

		// Seconds between on-disk change checks.
		constexpr float kPollInterval = 0.25f;

		// Error handler for every protected call: message + traceback. Errors
		// thrown from C++ bindings arrive without a position ("lua: error: ...");
		// prefix them with the innermost script line so agents still get
		// "GameData.Scripts/Door.lua:12: ...".
		int TracebackHandler(lua_State* L)
		{
			const char* raw = lua_tostring(L, 1);
			if (raw == nullptr)
				raw = luaL_tolstring(L, 1, nullptr);
			std::string msg = raw != nullptr ? raw : "unknown error";

			static const std::string kSolPrefix = "lua: error: ";
			if (msg.rfind(kSolPrefix, 0) == 0)
				msg = msg.substr(kSolPrefix.size());

			if (msg.find(".lua:") == std::string::npos)
			{
				lua_Debug ar;
				for (int level = 1; lua_getstack(L, level, &ar); ++level)
				{
					if (lua_getinfo(L, "Sl", &ar) && ar.currentline > 0 && ar.source != nullptr && ar.source[0] == '@')
					{
						msg = std::string(ar.source + 1) + ":" + std::to_string(ar.currentline) + ": " + msg;
						break;
					}
				}
			}

			luaL_traceback(L, L, msg.c_str(), 1);
			return 1;
		}

		// A script's own globals table. Reads fall back to the shared sandbox;
		// writes stay here (no __newindex), so scripts can't clobber each other
		// or the API tables. (sol::environment's fallback constructor would
		// also forward writes to the fallback.)
		sol::environment MakeScriptEnvironment(sol::state& lua)
		{
			sol::environment env(lua, sol::create);
			sol::table mt = lua.create_table();
			mt[sol::meta_function::index] = lua.globals();
			env[sol::metatable_key] = mt;
			return env;
		}

		bool StatFile(const fs::path& path, fs::file_time_type& writeTime, uintmax_t& size)
		{
			std::error_code ec;
			if (!fs::is_regular_file(path, ec))
				return false;
			writeTime = fs::last_write_time(path, ec);
			if (ec)
				return false;
			size = fs::file_size(path, ec);
			return !ec;
		}

	}

	// Lua error messages carry a long traceback; keep the first lines.
	std::string TrimTraceback(const std::string& error, size_t maxLines)
	{
		std::string out;
		size_t lines = 0, start = 0;
		while (start < error.size() && lines < maxLines)
		{
			size_t end = error.find('\n', start);
			if (end == std::string::npos)
				end = error.size();
			out.append(error, start, end - start);
			out += '\n';
			start = end + 1;
			++lines;
		}
		if (!out.empty())
			out.pop_back();
		return out;
	}

	LuaScriptEngine::LuaScriptEngine() = default;

	LuaScriptEngine::~LuaScriptEngine()
	{
		Destroy();
	}

	bool LuaScriptEngine::Create()
	{
		if (_created)
			return true;

		g_pScriptEngine = this;

		try
		{
			CreateState();
		}
		catch (const std::exception& e)
		{
			LOG_CRIT("[Script] Failed to create the Lua state: %s", e.what());
			g_pScriptEngine = nullptr;
			return false;
		}

		_created = true;
		LOG_INFO("[Script] %s scripting ready (sol2 %s)", GetLanguageName(), SOL_VERSION_STRING);
		return true;
	}

	void LuaScriptEngine::Destroy()
	{
		if (!_created)
			return;

		if (_inputRegistered && g_pEnv != nullptr && g_pEnv->_inputSystem != nullptr)
			g_pEnv->_inputSystem->RemoveInputListener(this);
		_inputRegistered = false;

		// Components that outlived the scene manager: detach them (they forget
		// the instance) and free the instances while the Lua state still exists.
		for (LuaScriptInstance* instance : _instances)
		{
			instance->Detach();
			delete instance;
		}
		_instances.clear();

		_assets.clear();
		_modules.clear();
		_tracebackHandler = sol::function();
		_lua.reset();

		if (g_pScriptEngine == this)
			g_pScriptEngine = nullptr;
		_created = false;
	}

	void LuaScriptEngine::InstructionHook(lua_State* L, lua_Debug*)
	{
		LuaScriptEngine* engine = g_pScriptEngine;
		if (engine != nullptr && ++engine->_budgetTicks > kMaxHookTicks)
		{
			engine->_budgetTicks = 0;
			luaL_error(L, "script exceeded its instruction budget (%d million instructions in one call) - infinite loop?",
				(int)((uint64_t)kMaxHookTicks * kHookInterval / 1000000));
		}
	}

	void LuaScriptEngine::CreateState()
	{
		_lua = std::make_unique<sol::state>();
		sol::state& lua = *_lua;

		// Only libraries with no file system / process / native-code access
		// (os is cut down to its clock functions in BuildSandbox). Opened with
		// luaL_openselectedlibs rather than sol::open_libraries: the vcpkg
		// lua-c++ DLL doesn't export the individual luaopen_* functions.
		luaL_openselectedlibs(lua.lua_state(),
			LUA_GLIBK | LUA_MATHLIBK | LUA_STRLIBK | LUA_TABLIBK | LUA_UTF8LIBK | LUA_COLIBK | LUA_OSLIBK, 0);

		lua_sethook(lua.lua_state(), &LuaScriptEngine::InstructionHook, LUA_MASKCOUNT, kHookInterval);

		lua_pushcfunction(lua.lua_state(), &TracebackHandler);
		_tracebackHandler = sol::function(lua.lua_state(), -1);
		lua_pop(lua.lua_state(), 1);

		BuildSandbox();
		RegisterBindings(lua);
	}

	void LuaScriptEngine::BuildSandbox()
	{
		sol::state& lua = *_lua;
		sol::table g = lua.globals();

		// No loading code from files, strings or bytecode: scripts come only
		// through ScriptComponent / require(), which go through the engine's
		// file systems.
		g["dofile"] = sol::lua_nil;
		g["loadfile"] = sol::lua_nil;
		g["load"] = sol::lua_nil;
		g["collectgarbage"] = sol::lua_nil;

		// string.dump produces bytecode; drop it (bytecode can be crafted to
		// escape the VM's safety checks).
		if (sol::optional<sol::table> str = g["string"]; str)
			(*str)["dump"] = sol::lua_nil;

		// os: time only. No execute / exit / getenv / remove / rename / tmpname.
		if (sol::optional<sol::table> os = g["os"]; os)
		{
			sol::table safeOs = lua.create_table();
			safeOs["clock"] = (*os)["clock"];
			safeOs["time"] = (*os)["time"];
			safeOs["date"] = (*os)["date"];
			safeOs["difftime"] = (*os)["difftime"];
			g["os"] = safeOs;
		}

		// print -> engine log, attributed to the running script.
		g.set_function("print", [](sol::variadic_args args, sol::this_state ts)
		{
			sol::state_view lua(ts);
			sol::protected_function tostr = lua["tostring"];
			std::string line;
			for (auto arg : args)
			{
				if (!line.empty())
					line += '\t';
				sol::protected_function_result r = tostr(arg.get<sol::object>());
				line += r.valid() ? r.get<std::string>() : std::string("?");
			}
			const std::string who = g_pScriptEngine ? g_pScriptEngine->DescribeContext() : std::string("script");
			LOG_INFO("[Script] %s: %s", who.c_str(), line.c_str());
		});

		// require("GameData.Scripts/Util.lua"): modules from the engine file
		// systems only, cached, shared by all scripts.
		g.set_function("require", [](const std::string& path) -> sol::object
		{
			return g_pScriptEngine->RequireModule(path);
		});
	}

	// --- Path resolution -------------------------------------------------------

	bool LuaScriptEngine::ResolveScriptPath(const std::string& scriptPath, fs::path& absolutePath, std::string* canonical)
	{
		if (scriptPath.empty() || g_pEnv == nullptr)
			return false;

		auto& resources = g_pEnv->GetResourceSystem();
		const std::wstring wpath = Utf8ToWide(scriptPath);
		const fs::path asPath(wpath);

		if (asPath.is_absolute())
		{
			for (FileSystem* fsys : resources.GetFileSystems())
			{
				if (fsys == nullptr)
					continue;
				std::error_code ec;
				fs::path rel = fs::relative(asPath, fsys->GetDataDirectory(), ec);
				if (ec || rel.empty() || rel.native().rfind(L"..", 0) == 0)
					continue;
				if (!fsys->DoesAbsolutePathExist(asPath))
					continue;
				absolutePath = asPath;
				if (canonical)
					*canonical = WideToUtf8(fsys->GetName() + L"." + rel.generic_wstring());
				return true;
			}

			std::error_code ec;
			if (fs::is_regular_file(asPath, ec))
			{
				absolutePath = asPath;
				if (canonical)
					*canonical = WideToUtf8(asPath.generic_wstring());
				return true;
			}
			return false;
		}

		// "FsName.rel/path" (the dot must come before the first separator,
		// as in ResourceSystem) or a plain data-relative path.
		std::wstring prefix;
		std::wstring rest = wpath;
		const size_t firstSep = wpath.find_first_of(L"\\/");
		const size_t dot = wpath.find(L'.');
		if (dot != std::wstring::npos && (firstSep == std::wstring::npos || dot < firstSep))
		{
			prefix = wpath.substr(0, dot);
			rest = wpath.substr(dot + 1);
		}

		for (FileSystem* fsys : resources.GetFileSystems())
		{
			if (fsys == nullptr)
				continue;
			if (!prefix.empty() && fsys->GetName() != prefix)
				continue;

			const fs::path full = (fsys->GetDataDirectory() / fs::path(rest)).lexically_normal();
			if (!fsys->DoesAbsolutePathExist(full))
				continue;

			absolutePath = full;
			if (canonical)
				*canonical = WideToUtf8(fsys->GetName() + L"." + fs::path(rest).lexically_normal().generic_wstring());
			return true;
		}

		return false;
	}

	bool LuaScriptEngine::ReadScriptSource(const fs::path& absolute, std::string& source, std::string& error)
	{
		source.clear();

		// Prefer the owning file system (asset packages override GetFileData);
		// fall back to a plain read.
		if (g_pEnv != nullptr)
		{
			for (FileSystem* fsys : g_pEnv->GetResourceSystem().GetFileSystems())
			{
				if (fsys == nullptr)
					continue;
				std::error_code ec;
				fs::path rel = fs::relative(absolute, fsys->GetDataDirectory(), ec);
				if (ec || rel.empty() || rel.native().rfind(L"..", 0) == 0)
					continue;
				std::vector<uint8_t> bytes;
				fsys->GetFileData(absolute, bytes);
				if (!bytes.empty())
				{
					source.assign(bytes.begin(), bytes.end());
					return true;
				}
				break;
			}
		}

		std::ifstream file(absolute, std::ios::in | std::ios::binary);
		if (!file)
		{
			error = "cannot open '" + WideToUtf8(absolute.wstring()) + "'";
			return false;
		}
		std::ostringstream ss;
		ss << file.rdbuf();
		source = ss.str();
		return true;
	}

	// --- Assets ------------------------------------------------------------------

	std::shared_ptr<ScriptAsset> LuaScriptEngine::AcquireScript(const std::string& scriptPath)
	{
		if (auto it = _assets.find(scriptPath); it != _assets.end())
			return it->second;

		auto asset = std::make_shared<ScriptAsset>();
		asset->requestedPath = scriptPath;
		asset->path = scriptPath;
		_assets[scriptPath] = asset;

		LoadAsset(*asset);
		return asset;
	}

	void LuaScriptEngine::LoadAsset(ScriptAsset& asset)
	{
		sol::state& lua = *_lua;

		fs::path absolute;
		std::string canonical;
		if (!ResolveScriptPath(asset.requestedPath, absolute, &canonical))
		{
			const std::string error = "script '" + asset.requestedPath + "' not found (expected e.g. GameData.Scripts/Name.lua)";
			if (asset.error != error)
				LOG_WARN("[Script] %s", error.c_str());
			asset.error = error;
			asset.absolute.clear();
			return;
		}

		asset.path = canonical;
		asset.absolute = absolute;
		StatFile(absolute, asset.writeTime, asset.fileSize);

		std::string source, error;
		if (!ReadScriptSource(absolute, source, error))
		{
			asset.error = error;
			LOG_WARN("[Script] %s", error.c_str());
			return;
		}

		// "@path" chunk name: errors read "GameData.Scripts/Door.lua:12: ...".
		sol::load_result chunk = lua.load(source, "@" + asset.path, sol::load_mode::text);
		if (!chunk.valid())
		{
			sol::error err = chunk;
			asset.error = err.what();
			LOG_WARN("[Script] compile error: %s", asset.error.c_str());
			return;
		}

		// Each script gets its own globals table that falls back to the
		// sandbox, so scripts can't trample each other's globals.
		sol::environment env = MakeScriptEnvironment(lua);
		sol::protected_function fn = chunk;
		sol::set_environment(env, fn);

		sol::object returned;
		{
			std::string runError;
			ContextScope scope(*this, nullptr);
			fn.set_error_handler(_tracebackHandler);
			sol::protected_function_result result = fn();
			if (!result.valid())
			{
				sol::error err = result;
				asset.error = TrimTraceback(err.what());
				LOG_WARN("[Script] error running %s: %s", asset.path.c_str(), asset.error.c_str());
				return;
			}
			if (result.return_count() > 0)
				returned = result.get<sol::object>(0);
		}

		sol::table klass = returned.is<sol::table>() ? returned.as<sol::table>() : sol::table(env);

		asset.env = env;
		asset.klass = klass;
		asset.defaults = ExtractPropertyDefaults(klass, asset.path);
		asset.loaded = true;
		asset.error.clear();
		++asset.generation;
	}

	json LuaScriptEngine::ExtractPropertyDefaults(const sol::table& klass, const std::string& scriptPath)
	{
		json out = json::object();

		sol::object props = klass["Properties"];
		if (!props.is<sol::table>())
			props = klass["properties"];
		if (!props.is<sol::table>())
			return out;

		for (auto& [key, value] : props.as<sol::table>())
		{
			if (!key.is<std::string>())
				continue;
			const std::string name = key.as<std::string>();
			bool ok = false;
			json j = LuaToJson(value, ok);
			if (!ok || j.is_null() || j.is_object())
			{
				LOG_WARN("[Script] %s: property '%s' has an unsupported type (use number, boolean, string or Vec3)", scriptPath.c_str(), name.c_str());
				continue;
			}
			out[name] = j;
		}
		return out;
	}

	sol::object LuaScriptEngine::RequireModule(const std::string& modulePath)
	{
		sol::state& lua = *_lua;

		fs::path absolute;
		std::string canonical;
		if (!ResolveScriptPath(modulePath, absolute, &canonical))
			throw sol::error("require: module '" + modulePath + "' not found (use a resource path such as GameData.Scripts/Util.lua)");

		if (auto it = _modules.find(canonical); it != _modules.end())
			return it->second.value;

		if (_modulesLoading.count(canonical) != 0)
			throw sol::error("require: circular dependency on '" + canonical + "'");

		std::string source, error;
		if (!ReadScriptSource(absolute, source, error))
			throw sol::error("require: " + error);

		sol::load_result chunk = lua.load(source, "@" + canonical, sol::load_mode::text);
		if (!chunk.valid())
		{
			sol::error err = chunk;
			throw sol::error(std::string(err.what()));
		}

		sol::environment env = MakeScriptEnvironment(lua);
		sol::protected_function fn = chunk;
		sol::set_environment(env, fn);

		_modulesLoading.insert(canonical);
		sol::protected_function_result result = fn();
		_modulesLoading.erase(canonical);
		if (!result.valid())
		{
			sol::error err = result;
			throw sol::error(std::string(err.what()));
		}

		ScriptModule module;
		module.absolute = absolute;
		StatFile(absolute, module.writeTime, module.fileSize);
		module.value = result.return_count() > 0 ? result.get<sol::object>(0) : sol::make_object(lua, true);
		_modules[canonical] = module;
		return module.value;
	}

	// --- Instances -------------------------------------------------------------------

	IScriptInstance* LuaScriptEngine::CreateInstance(ScriptComponent* owner)
	{
		if (!_created || owner == nullptr)
			return nullptr;

		auto* instance = new LuaScriptInstance(*this, owner);
		_instances.insert(instance);
		return instance;
	}

	void LuaScriptEngine::DestroyInstance(IScriptInstance* instance)
	{
		auto* lua = static_cast<LuaScriptInstance*>(instance);
		if (lua == nullptr || _instances.erase(lua) == 0)
			return;
		delete lua;

		// Drop assets no instance uses any more (their next use reloads them).
		for (auto it = _assets.begin(); it != _assets.end();)
		{
			if (it->second.use_count() == 1)
				it = _assets.erase(it);
			else
				++it;
		}
	}

	Scene* LuaScriptEngine::GetCurrentScene() const
	{
		if (_current != nullptr)
		{
			if (Scene* scene = _current->GetScene())
				return scene;
		}
		if (g_pEnv != nullptr && g_pEnv->_sceneManager != nullptr)
			return g_pEnv->_sceneManager->GetCurrentScene().get();
		return nullptr;
	}

	std::shared_ptr<Scene> LuaScriptEngine::GetSharedScene(Scene* scene) const
	{
		if (scene == nullptr || g_pEnv == nullptr || g_pEnv->_sceneManager == nullptr)
			return nullptr;
		for (const auto& s : g_pEnv->_sceneManager->GetAllScenes())
		{
			if (s.get() == scene)
				return s;
		}
		return nullptr;
	}

	bool LuaScriptEngine::IsSceneAlive(Scene* scene) const
	{
		return GetSharedScene(scene) != nullptr;
	}

	std::string LuaScriptEngine::DescribeContext() const
	{
		return _current != nullptr ? _current->Describe() : std::string("script");
	}

	// --- Reload ------------------------------------------------------------------------

	void LuaScriptEngine::OnAssetReloaded(const std::shared_ptr<ScriptAsset>& asset)
	{
		for (LuaScriptInstance* instance : _instances)
		{
			if (instance->GetAsset() == asset)
				instance->OnAssetReloaded();
		}
	}

	void LuaScriptEngine::ReloadAllScripts()
	{
		if (!_created)
			return;

		_modules.clear();
		for (auto& [key, asset] : _assets)
		{
			LoadAsset(*asset);
			if (asset->loaded)
				OnAssetReloaded(asset);
		}
		LOG_INFO("[Script] Reloaded %d script(s)", (int)_assets.size());
	}

	void LuaScriptEngine::PollFileChanges()
	{
		// A changed module invalidates the cache and every script (any of them
		// may have captured the old module table).
		bool moduleChanged = false;
		for (auto& [path, module] : _modules)
		{
			fs::file_time_type t{};
			uintmax_t size = 0;
			if (!StatFile(module.absolute, t, size) || t != module.writeTime || size != module.fileSize)
			{
				LOG_INFO("[Script] Module %s changed; reloading all scripts", path.c_str());
				moduleChanged = true;
				break;
			}
		}
		if (moduleChanged)
		{
			ReloadAllScripts();
			return;
		}

		for (auto& [key, asset] : _assets)
		{
			if (asset->absolute.empty())
			{
				// Not found last time: it may have been created since.
				fs::path absolute;
				if (!ResolveScriptPath(asset->requestedPath, absolute, nullptr))
					continue;
			}
			else
			{
				fs::file_time_type t{};
				uintmax_t size = 0;
				if (!StatFile(asset->absolute, t, size))
					continue; // deleted or mid-save; keep the loaded version
				if (t == asset->writeTime && size == asset->fileSize)
					continue;
			}

			const uint32_t before = asset->generation;
			LoadAsset(*asset);
			if (asset->generation != before)
			{
				LOG_INFO("[Script] Reloaded %s", asset->path.c_str());
				OnAssetReloaded(asset);
			}
			// On failure LoadAsset logged the error; instances keep the old version.
		}
	}

	bool LuaScriptEngine::CheckScript(const std::string& scriptPath, std::string& error)
	{
		if (!_created)
		{
			error = "script engine not initialised";
			return false;
		}

		fs::path absolute;
		std::string canonical;
		if (!ResolveScriptPath(scriptPath, absolute, &canonical))
		{
			error = "script '" + scriptPath + "' not found";
			return false;
		}

		std::string source;
		if (!ReadScriptSource(absolute, source, error))
			return false;

		sol::load_result chunk = _lua->load(source, "@" + canonical, sol::load_mode::text);
		if (!chunk.valid())
		{
			sol::error err = chunk;
			error = err.what();
			return false;
		}
		return true;
	}

	// --- Per frame ---------------------------------------------------------------------

	void LuaScriptEngine::ResetInputState()
	{
		_keyDown.fill(false);
		_keyPressedFrame.fill(false);
		_keyReleasedFrame.fill(false);
		_keyPressedPending.fill(false);
		_keyReleasedPending.fill(false);
		_frameKeyEvents.clear();
		_pendingKeyEvents.clear();
		_frameMouseDelta = math::Vector2::Zero;
		_pendingMouseDelta = math::Vector2::Zero;
	}

	void LuaScriptEngine::Tick(float realFrameTime)
	{
		if (!_created || g_pEnv == nullptr)
			return;

		if (!_inputRegistered && g_pEnv->_inputSystem != nullptr)
		{
			g_pEnv->_inputSystem->AddInputListener(this,
				InputEvent::KeyDown | InputEvent::KeyUp | InputEvent::MouseDown | InputEvent::MouseUp | InputEvent::MouseMove);
			_inputRegistered = true;
		}

		const bool running = g_pEnv->IsGameRunning();
		if (running != _wasGameRunning)
		{
			// New play session (or back to edit mode): fresh clock, no stuck keys.
			ResetInputState();
			_gameTime = 0.0f;
			_wasGameRunning = running;
		}

		// Paused simulation with no step this frame: ScriptComponent skips
		// OnUpdate, so publishing the input now would drop it. Hold it (key
		// events, edges, mouse delta) and deliver it on the next frame gameplay
		// advances - e.g. the bridge's step_simulation right after send_input.
		if (running && g_pEnv->_timeManager != nullptr &&
			g_pEnv->_timeManager->_simulationPaused && g_pEnv->_timeManager->GetGameFrameTime() <= 0.0f)
		{
			_keyPressedFrame.fill(false);
			_keyReleasedFrame.fill(false);
			_frameKeyEvents.clear();
			_frameMouseDelta = math::Vector2(0.0f, 0.0f);
			_pollTimer += realFrameTime;
			if (_pollTimer >= kPollInterval)
			{
				_pollTimer = 0.0f;
				PollFileChanges();
			}
			return;
		}

		// Publish the input gathered since the last tick as this frame's edges.
		_keyPressedFrame = _keyPressedPending;
		_keyReleasedFrame = _keyReleasedPending;
		_keyPressedPending.fill(false);
		_keyReleasedPending.fill(false);
		_frameKeyEvents.swap(_pendingKeyEvents);
		_pendingKeyEvents.clear();
		_frameMouseDelta = _pendingMouseDelta;
		_pendingMouseDelta = math::Vector2::Zero;

		if (running && g_pEnv->_timeManager != nullptr)
			_gameTime += g_pEnv->_timeManager->GetGameFrameTime();

		_pollTimer += realFrameTime;
		if (_pollTimer >= kPollInterval)
		{
			_pollTimer = 0.0f;
			PollFileChanges();
		}
	}

	bool LuaScriptEngine::OnInputEvent(InputEvent event, InputData* data)
	{
		// Gameplay input only; never consume (other listeners still see it).
		if (data == nullptr || g_pEnv == nullptr || !g_pEnv->IsGameRunning())
			return false;

		auto press = [this](int32_t vk)
		{
			if (vk <= 0 || vk >= 256 || _keyDown[vk])
				return; // auto-repeat
			_keyDown[vk] = true;
			_keyPressedPending[vk] = true;
			_pendingKeyEvents.emplace_back(vk, true);
		};
		auto release = [this](int32_t vk)
		{
			if (vk <= 0 || vk >= 256 || !_keyDown[vk])
				return;
			_keyDown[vk] = false;
			_keyReleasedPending[vk] = true;
			_pendingKeyEvents.emplace_back(vk, false);
		};

		switch (event)
		{
		case InputEvent::KeyDown:   press(data->KeyDown.key); break;
		case InputEvent::KeyUp:     release(data->KeyUp.key); break;
		case InputEvent::MouseDown: press(data->MouseDown.button); break;
		case InputEvent::MouseUp:   release(data->MouseUp.button); break;
		case InputEvent::MouseMove:
			if (!data->MouseMove.absolute)
			{
				_pendingMouseDelta.x += data->MouseMove.x;
				_pendingMouseDelta.y += data->MouseMove.y;
			}
			break;
		default:
			break;
		}
		return false;
	}
}
