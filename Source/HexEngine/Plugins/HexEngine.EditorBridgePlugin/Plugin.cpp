
// -----------------------------------------------------------------------------
// HexEngine.EditorBridgePlugin - editor-only live inspection bridge.
//
// An IPlugin that also implements IEditorToolPlugin. It starts a local named-pipe
// server ONLY when (a) it's running inside the editor (its OnCreateUI hook fires -
// the shipped game never calls it) AND (b) the bridge is explicitly opted in via
// `--enable-editor-bridge` or HEXENGINE_EDITOR_BRIDGE=1. Fail-closed: off by
// default, never active in a shipped game.
//
// Read-only, Phase 1. No mutation, no writes (except the discovery session file),
// no shell. ECS/scene reads are marshalled to the editor main thread (drained
// every frame via OnEditorFrameTick, and also opportunistically in OnMessage); if
// the editor isn't running the pump the request times out cleanly rather than
// touching engine state from the pipe thread.
// -----------------------------------------------------------------------------

#include "EditorBridgeProtocol.hpp"
#include "BridgeServer.hpp"

#include "../../HexEngine.Core/Plugin/IPlugin.hpp"
#include "../../HexEngine.Core/Environment/IEnvironment.hpp"
#include "../../HexEngine.Core/Environment/LogFile.hpp"
#include "../../HexEngine.Core/Scene/SceneManager.hpp"
#include "../../HexEngine.Core/Scene/Scene.hpp"
#include "../../HexEngine.Core/Entity/Entity.hpp"
#include "../../HexEngine.Core/Entity/Component/BaseComponent.hpp"
#include "../../HexEngine.Core/FileSystem/ResourceSystem.hpp"

#include <deque>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <mutex>
#include <queue>

namespace fs = std::filesystem;

namespace HexEngine
{
	using namespace EditorBridge;

	namespace
	{
		std::string WideToUtf8(const std::wstring& w)
		{
			if (w.empty()) return std::string();
			int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
			std::string s(n, '\0');
			WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
			return s;
		}

		bool BridgeOptedIn()
		{
			if (const char* e = std::getenv("HEXENGINE_EDITOR_BRIDGE"))
			{
				std::string v = e;
				if (v == "1" || v == "true" || v == "TRUE" || v == "on")
					return true;
			}
			if (const wchar_t* cmd = GetCommandLineW())
			{
				std::wstring w = cmd;
				if (w.find(L"--enable-editor-bridge") != std::wstring::npos)
					return true;
			}
			return false;
		}

		fs::path SessionDirPath()
		{
			wchar_t tmp[MAX_PATH];
			DWORD n = GetTempPathW(MAX_PATH, tmp);
			fs::path p = (n > 0) ? fs::path(std::wstring(tmp, n)) : fs::temp_directory_path();
			return p / "HexEngine" / "EditorBridge";
		}

		json EntityIdToJson(const EntityId& id)
		{
			return json{ {"index", id.index}, {"generation", id.generation} };
		}

		// The editor keeps utility scenes (e.g. the IconService's icon-preview
		// scene) registered - and sometimes even current - in the background
		// alongside the scene the user is actually editing. Always report the real
		// user scene, never a utility/background one. Mirrors how the editor's
		// PrefabController picks a non-utility scene to restore.
		std::shared_ptr<Scene> PrimaryUserScene()
		{
			if (!g_pEnv) return nullptr;
			auto& sm = g_pEnv->GetSceneManager();
			auto isUtility = [](const std::shared_ptr<Scene>& s) {
				return s && HEX_HASFLAG(s->GetFlags(), SceneFlags::Utility);
			};
			if (auto cur = sm.GetCurrentScene(); cur && !isUtility(cur))
				return cur;
			// Current is a utility/background scene (or null): fall back to the
			// most-recently-registered non-utility scene.
			const auto& all = sm.GetAllScenes();
			for (auto it = all.rbegin(); it != all.rend(); ++it)
				if (!isUtility(*it))
					return *it;
			return nullptr;
		}

		const char* LogLevelName(LogLevel l)
		{
			switch (l)
			{
			case LogLevel::Debug: return "debug";
			case LogLevel::Info:  return "info";
			case LogLevel::Warn:  return "warn";
			case LogLevel::Crit:  return "crit";
			default:              return "info";
			}
		}
	}

	// Marshals tasks from the pipe thread onto the editor main thread. The pipe
	// thread submits a task and waits (bounded); the main thread drains + fulfils.
	class MainThreadQueue
	{
	public:
		// Pipe thread: run `fn` on the main thread, wait up to timeoutMs for its
		// result. Returns false on timeout (task stays queued and is drained later,
		// harmlessly).
		bool Run(std::function<json()> fn, json& out, unsigned timeoutMs)
		{
			auto task = std::make_shared<Task>();
			task->fn = std::move(fn);
			auto fut = task->result.get_future();
			{
				std::lock_guard<std::mutex> lk(_mutex);
				_queue.push(task);
			}
			if (fut.wait_for(std::chrono::milliseconds(timeoutMs)) != std::future_status::ready)
				return false;
			out = fut.get();
			return true;
		}

		// Main thread: execute all pending tasks.
		void Drain()
		{
			for (;;)
			{
				std::shared_ptr<Task> task;
				{
					std::lock_guard<std::mutex> lk(_mutex);
					if (_queue.empty()) return;
					task = _queue.front();
					_queue.pop();
				}
				json r;
				try { r = task->fn(); }
				catch (const std::exception& e) { r = json{ {"__error", e.what()} }; }
				task->result.set_value(std::move(r));
			}
		}

	private:
		struct Task { std::function<json()> fn; std::promise<json> result; };
		std::mutex _mutex;
		std::queue<std::shared_ptr<Task>> _queue;
	};

	class EditorBridgePlugin : public IPlugin, public IEditorToolPlugin, public ILogFileListener
	{
	public:
		~EditorBridgePlugin() { StopBridge(); }

		// --- ILogFileListener: capture recent log lines into a bounded ring
		//     buffer (thread-safe; read directly by get_recent_engine_logs). ---
		void OnLogMessage(const LogMessage& message) override
		{
			std::lock_guard<std::mutex> lk(_logMutex);
			_logRing.push_back(std::string("[") + LogLevelName(message.level) + "] " + message.text);
			while (_logRing.size() > kMaxLogRing)
				_logRing.pop_front();
		}

		// --- IPlugin ---
		void Destroy() override { StopBridge(); }

		void GetVersionData(VersionData* data) override
		{
			data->majorVersion = 0;
			data->minorVersion = 1;
			data->name = "HexEngine.EditorBridge";
			data->description = "Editor-only read-only live inspection bridge (MCP). Off unless --enable-editor-bridge.";
			data->author = "HexEngine";
			data->enabled = true;
		}

		IPluginInterface* CreateInterface(const std::string&) override { return nullptr; }
		void GetDependencies(std::vector<std::string>&) const override {}
		IEditorToolPlugin* GetEditorToolPlugin() override { return this; }

		// --- IEditorToolPlugin (editor-only entry points) ---
		void OnCreateUI(MenuBar*) override
		{
			// Editor-only + opt-in gate. The shipped game never calls OnCreateUI.
			if (_started)
				return;
			if (!BridgeOptedIn())
			{
				LOG_INFO("EditorBridge: disabled (pass --enable-editor-bridge or set HEXENGINE_EDITOR_BRIDGE=1 to enable).");
				return;
			}
			StartBridge();
		}

		void OnAssetExplorerCreateNew(ContextMenu*, ContextRoot*, const fs::path&, FileSystem*, std::function<void()>) override {}

		void OnMessage(Message*, MessageListener*) override
		{
			// Runs on the editor main thread; service any queued inspection tasks.
			_mainThread.Drain();
		}

		void OnEditorFrameTick() override
		{
			// Reliable per-frame main-thread pump: drain queued ECS inspection tasks
			// every editor frame, so marshalled reads no longer depend on incidental
			// editor tool messages (mouse-over-viewport). This is what makes
			// get_editor_status / get_open_scene / list_entities respond when idle.
			_mainThread.Drain();
		}

	private:
		void StartBridge()
		{
			const uint32_t pid = (uint32_t)GetCurrentProcessId();
			const std::string pipeName = PipeNameForPid(pid);

			auto handler = [this](const std::string& line) -> std::string { return DispatchLine(line); };
			if (!_server.Start(pipeName, handler))
			{
				LOG_CRIT("EditorBridge: failed to start named-pipe server on '%s'", pipeName.c_str());
				return;
			}
			WriteSessionFile(pid, pipeName);
			if (g_pEnv)
				g_pEnv->GetLogFile().AddListener(this);
			_started = true;
			LOG_INFO("EditorBridge: listening on '%s' (opt-in enabled). Read-only inspection bridge active.", pipeName.c_str());
		}

		void StopBridge()
		{
			if (!_started) return;
			if (g_pEnv)
				g_pEnv->GetLogFile().RemoveListener(this);
			_server.Stop();
			RemoveSessionFile();
			_started = false;
			LOG_INFO("EditorBridge: stopped.");
		}

		void WriteSessionFile(uint32_t pid, const std::string& pipeName)
		{
			SessionInfo s;
			s.pid = pid;
			s.pipeName = pipeName;
			s.startedAtUnix = (uint64_t)std::time(nullptr);
			if (auto scene = PrimaryUserScene())
				s.projectName = WideToUtf8(scene->GetName());

			std::error_code ec;
			fs::create_directories(SessionDirPath(), ec);
			_sessionFile = SessionDirPath() / SessionFileName(pid);
			std::ofstream out(_sessionFile, std::ios::binary | std::ios::trunc);
			if (out.is_open())
				out << SessionToJson(s).dump(2);
		}

		void RemoveSessionFile()
		{
			if (_sessionFile.empty()) return;
			std::error_code ec;
			fs::remove(_sessionFile, ec);
			_sessionFile.clear();
		}

		// Pipe thread: parse -> dispatch -> serialise (capped).
		std::string DispatchLine(const std::string& line)
		{
			Request req;
			std::string err;
			if (!ParseRequestText(line, req, err))
				return MakeError(0, ErrorCode::InvalidRequest, err).dump();

			json response = Dispatch(req);
			return SerializeResponseCapped(response, req.id);
		}

		json Dispatch(const Request& req)
		{
			const std::string& m = req.method;

			// Marshal a main-thread read; returns the handler's full envelope, or a
			// NotAvailable error if the editor didn't service it in time.
			auto onMain = [&](std::function<json()> fn) -> json {
				json out;
				if (!_mainThread.Run(std::move(fn), out, 2000))
					return MakeError(req.id, ErrorCode::NotAvailable,
						"editor did not service the request in time (idle or busy); retry while interacting with the editor");
				if (out.is_object() && out.contains("__error"))
					return MakeError(req.id, ErrorCode::Internal, out["__error"].get<std::string>());
				return out;
			};

			const int64_t id = req.id;

			if (m == "get_editor_status")
			{
				return onMain([id] {
					std::string sceneName;
					if (auto scene = PrimaryUserScene())
						sceneName = WideToUtf8(scene->GetName());
					return MakeResult(id, json{
						{"editorRunning", true},
						{"sceneName", sceneName},
						{"mode", "edit"},          // TODO: authoritative edit/play mode needs an editor API
						{"projectName", nullptr},  // TODO: project metadata not exposed to plugins yet
						{"protocolVersion", kProtocolVersion},
					});
				});
			}

			if (m == "get_open_scene")
			{
				return onMain([id] {
					auto scene = PrimaryUserScene();
					if (!scene)
						return MakeError(id, ErrorCode::NotAvailable, "no scene is currently open");
					std::vector<EntityId> ids;
					scene->GetLiveEntityIds(ids);
					return MakeResult(id, json{
						{"sceneName", WideToUtf8(scene->GetName())},
						{"entityCount", ids.size()},
					});
				});
			}

			if (m == "list_entities")
			{
				const size_t offset = (size_t)req.params.value("offset", 0);
				size_t limit = (size_t)req.params.value("limit", 200);
				if (limit > 1000) limit = 1000;
				const std::string filter = req.params.value("filter", std::string());
				return onMain([id, offset, limit, filter] {
					auto scene = PrimaryUserScene();
					if (!scene)
						return MakeError(id, ErrorCode::NotAvailable, "no scene is currently open");
					std::vector<EntityId> ids;
					scene->GetLiveEntityIds(ids);
					json list = json::array();
					size_t seen = 0;
					for (const EntityId& eid : ids)
					{
						Entity* e = scene->TryGetEntity(eid);
						if (!e) continue;
						const std::string name = e->GetName();
						if (!filter.empty() && name.find(filter) == std::string::npos) continue;
						if (seen++ < offset) continue;
						if (list.size() >= limit) break;
						json comps = json::array();
						for (BaseComponent* c : e->GetAllComponents())
							if (c && c->GetComponentName()) comps.push_back(c->GetComponentName());
						list.push_back(json{ {"id", EntityIdToJson(eid)}, {"name", name}, {"components", comps} });
					}
					return MakeResult(id, json{ {"total", ids.size()}, {"offset", offset}, {"returned", list.size()}, {"entities", list} });
				});
			}

			if (m == "inspect_entity")
			{
				const json params = req.params;
				return onMain([id, params] {
					auto scene = PrimaryUserScene();
					if (!scene)
						return MakeError(id, ErrorCode::NotAvailable, "no scene is currently open");
					Entity* e = nullptr;
					if (params.contains("name") && params["name"].is_string())
						e = scene->GetEntityByName(params["name"].get<std::string>());
					else if (params.contains("index") && params["index"].is_number_integer())
					{
						const uint32_t idx = params["index"].get<uint32_t>();
						std::vector<EntityId> ids;
						scene->GetLiveEntityIds(ids);
						for (const EntityId& eid : ids)
							if (eid.index == idx) { e = scene->TryGetEntity(eid); break; }
					}
					else
						return MakeError(id, ErrorCode::InvalidParams, "inspect_entity requires a 'name' (string) or 'index' (integer)");

					if (!e)
						return MakeError(id, ErrorCode::NotAvailable, "no matching entity found");

					json comps = json::array();
					for (BaseComponent* c : e->GetAllComponents())
						if (c && c->GetComponentName()) comps.push_back(c->GetComponentName());
					json children = json::array();
					for (Entity* ch : e->GetChildren())
						if (ch) children.push_back(json{ {"id", EntityIdToJson(ch->GetId())}, {"name", ch->GetName()} });
					Entity* parent = e->GetParent();
					return MakeResult(id, json{
						{"id", EntityIdToJson(e->GetId())},
						{"name", e->GetName()},
						{"parent", parent ? json{ {"id", EntityIdToJson(parent->GetId())}, {"name", parent->GetName()} } : json(nullptr)},
						{"children", children},
						{"components", comps},
					});
				});
			}

			if (m == "list_components")
			{
				return onMain([id] {
					auto scene = PrimaryUserScene();
					if (!scene)
						return MakeError(id, ErrorCode::NotAvailable, "no scene is currently open");
					std::vector<EntityId> ids;
					scene->GetLiveEntityIds(ids);
					std::vector<std::string> names;
					for (const EntityId& eid : ids)
					{
						Entity* e = scene->TryGetEntity(eid);
						if (!e) continue;
						for (BaseComponent* c : e->GetAllComponents())
						{
							if (!c || !c->GetComponentName()) continue;
							std::string n = c->GetComponentName();
							if (std::find(names.begin(), names.end(), n) == names.end())
								names.push_back(n);
						}
					}
					std::sort(names.begin(), names.end());
					return MakeResult(id, json{ {"source", "current-scene"}, {"count", names.size()}, {"components", names} });
				});
			}

			if (m == "validate_current_scene")
			{
				return onMain([id] {
					auto scene = PrimaryUserScene();
					if (!scene)
						return MakeError(id, ErrorCode::NotAvailable, "no scene is currently open");
					std::vector<EntityId> ids;
					scene->GetLiveEntityIds(ids);
					json diagnostics = json::array();
					size_t unnamed = 0;
					for (const EntityId& eid : ids)
					{
						Entity* e = scene->TryGetEntity(eid);
						if (!e) continue;
						if (e->GetName().empty()) ++unnamed;
					}
					if (ids.empty())
						diagnostics.push_back(json{ {"severity","warning"}, {"message","scene has no entities"} });
					if (unnamed > 0)
						diagnostics.push_back(json{ {"severity","info"}, {"message", std::to_string(unnamed) + " entities have no name"} });
					return MakeResult(id, json{
						{"sceneName", WideToUtf8(scene->GetName())},
						{"entityCount", ids.size()},
						{"diagnosticCount", diagnostics.size()},
						{"diagnostics", diagnostics},
						{"note", "Phase 1 validation is minimal (entity presence/naming). Deeper reference/asset validation is a follow-up."},
					});
				});
			}

			// --- Not yet implementable safely: editor-only APIs not exposed to a
			//     tool plugin. Return NotImplemented (no faked data). ---
			if (m == "get_open_project")
				return MakeError(id, ErrorCode::NotImplemented, "project metadata is not exposed to editor tool plugins yet (TODO: editor context interface)");
			if (m == "get_selected_entity")
				return MakeError(id, ErrorCode::NotImplemented, "editor selection is not exposed to tool plugins yet (TODO: editor selection interface)");
			if (m == "inspect_component")
				return MakeError(id, ErrorCode::NotImplemented, "safe per-field component serialization needs reflection wiring (TODO)");
			if (m == "list_loaded_resources")
			{
				// ResourceSystem::EnumerateLoadedResources is internally locked and
				// exposes no pointers, so it's safe to call from the pipe thread
				// (no main-thread marshal, no idle-timeout).
				size_t max = (size_t)req.params.value("limit", 500);
				if (max > 2000) max = 2000;
				json arr = json::array();
				for (const auto& r : g_pEnv->GetResourceSystem().EnumerateLoadedResources(max))
					arr.push_back(json{ {"id", r.id}, {"fsPath", WideToUtf8(r.fsPath)}, {"absPath", r.absPath}, {"refCount", r.useCount} });
				return MakeResult(id, json{ {"count", arr.size()}, {"resources", arr} });
			}
			if (m == "find_missing_references")
				return MakeError(id, ErrorCode::NotImplemented, "reference validation is a follow-up (TODO)");
			if (m == "get_recent_engine_logs")
			{
				// Served from the thread-safe log ring buffer (fed by OnLogMessage).
				size_t max = (size_t)req.params.value("max", 200);
				if (max > 1000) max = 1000;
				json logs = json::array();
				{
					std::lock_guard<std::mutex> lk(_logMutex);
					const size_t start = _logRing.size() > max ? _logRing.size() - max : 0;
					for (size_t i = start; i < _logRing.size(); ++i)
						logs.push_back(_logRing[i]);
				}
				return MakeResult(id, json{ {"count", logs.size()}, {"logs", logs} });
			}

			return MakeError(id, ErrorCode::UnknownMethod, "No bridge method named " + m);
		}

	private:
		BridgeServer    _server;
		MainThreadQueue _mainThread;
		fs::path        _sessionFile;
		bool            _started = false;

		static constexpr size_t kMaxLogRing = 500;
		std::mutex              _logMutex;
		std::deque<std::string> _logRing;
	};

	static EditorBridgePlugin* g_pEditorBridgePlugin = nullptr;
	CREATE_PLUGIN(g_pEditorBridgePlugin, EditorBridgePlugin);
}
