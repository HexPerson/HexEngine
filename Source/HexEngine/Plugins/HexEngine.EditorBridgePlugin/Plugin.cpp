
// -----------------------------------------------------------------------------
// HexEngine.EditorBridgePlugin - editor-only live inspection + authoring bridge.
//
// An IPlugin that also implements IEditorToolPlugin. It starts a local named-pipe
// server ONLY when (a) it's running inside the editor (its OnCreateUI hook fires -
// the shipped game never calls it) AND (b) the bridge is explicitly opted in via
// `--enable-editor-bridge` or HEXENGINE_EDITOR_BRIDGE=1. Fail-closed: off by
// default, never active in a shipped game. Mutating methods additionally require
// HEXENGINE_EDITOR_BRIDGE_WRITE=1.
//
// Every method lives in one registry (RegisterMethods) with its write / thread /
// batchability flags. ECS/scene work is marshalled to the editor main thread,
// drained every frame via OnEditorFrameTick. `batch` runs many methods inside a
// SINGLE main-thread task (one frame), with results of earlier ops referenceable
// by later ones - the fast path for agent-driven authoring.
// -----------------------------------------------------------------------------

#include "EditorBridgeProtocol.hpp"
#include "BridgeServer.hpp"

#include "../../HexEngine.Core/Plugin/IPlugin.hpp"
#include "../../HexEngine.Core/Environment/IEnvironment.hpp"
#include "../../HexEngine.Core/Environment/IEditorContext.hpp"
#include "../../HexEngine.Core/Environment/ClassRegistry.hpp"
#include "../../HexEngine.Core/Environment/LogFile.hpp"
#include "../../HexEngine.Core/Scene/SceneManager.hpp"
#include "../../HexEngine.Core/Scene/Scene.hpp"
#include "../../HexEngine.Core/Entity/Entity.hpp"
#include "../../HexEngine.Core/Entity/Component/BaseComponent.hpp"
#include "../../HexEngine.Core/Entity/Component/Transform.hpp"
#include "../../HexEngine.Core/FileSystem/ResourceSystem.hpp"
#include "../../HexEngine.Core/FileSystem/FileSystem.hpp"
#include "../../HexEngine.Core/FileSystem/JsonFile.hpp"
#include "../../HexEngine.Core/Input/CommandManager.hpp"
#include "../../HexEngine.Core/Entity/Component/Camera.hpp"
#include "../../HexEngine.Core/Entity/Component/StaticMeshComponent.hpp"
#include "../../HexEngine.Core/Graphics/ITexture2D.hpp"
#include "../../HexEngine.Core/Graphics/Material.hpp"
#include "../../HexEngine.Core/Scene/Mesh.hpp"

#include <deque>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <random>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace fs = std::filesystem;

namespace HexEngine
{
	using namespace EditorBridge;

	namespace
	{
		// How long a request may wait in the queue for the editor main thread to
		// START it. A request that hasn't started by then is cancelled and is
		// guaranteed NOT to run (so a client retry can't duplicate a mutation).
		// Once started it always runs to completion and its result is returned.
		constexpr unsigned kPendingTimeoutMs = 10000;

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

		// Separate, stricter opt-in for the WRITE surface. The bridge is
		// read-only by default; mutating methods require
		// HEXENGINE_EDITOR_BRIDGE_WRITE=1 on top of the normal opt-in.
		bool BridgeWriteEnabled()
		{
			if (const char* e = std::getenv("HEXENGINE_EDITOR_BRIDGE_WRITE"))
			{
				std::string v = e;
				return (v == "1" || v == "true" || v == "TRUE" || v == "on");
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

		json EntityBrief(Entity* e)
		{
			return json{ {"id", EntityIdToJson(e->GetId())}, {"name", e->GetName()} };
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
			const auto& all = sm.GetAllScenes();
			for (auto it = all.rbegin(); it != all.rend(); ++it)
				if (!isUtility(*it))
					return *it;
			return nullptr;
		}

		// Full single-entity detail (id, name, parent, children, component types).
		// Shared by inspect_entity and get_selected_entity. Call on the main thread.
		json EntityDetailJson(Entity* e)
		{
			json comps = json::array();
			for (BaseComponent* c : e->GetAllComponents())
				if (c && c->GetComponentName()) comps.push_back(c->GetComponentName());
			json children = json::array();
			for (Entity* ch : e->GetChildren())
				if (ch) children.push_back(EntityBrief(ch));
			Entity* parent = e->GetParent();
			const dx::BoundingBox& aabb = e->GetAABB();
			return json{
				{"id", EntityIdToJson(e->GetId())},
				{"name", e->GetName()},
				{"parent", parent ? EntityBrief(parent) : json(nullptr)},
				{"children", children},
				{"components", comps},
				{"aabbCenter", json::array({ aabb.Center.x, aabb.Center.y, aabb.Center.z })},
				{"aabbExtents", json::array({ aabb.Extents.x, aabb.Extents.y, aabb.Extents.z })},
			};
		}

		// A JsonFile with every disk operation neutered. BaseComponent::Serialize's
		// SERIALIZE_VALUE macro is `file->Serialize(data, "key", val)`, which only
		// mutates the json container - but it dereferences `file`, so a null file
		// crashes. Handing components this object makes Serialize/Deserialize work
		// for field inspection/editing while GUARANTEEING no component can touch
		// the disk through it (all I/O redirects to memory). Mirrors the existing
		// MemoryFile pattern: the inherited fstream is never opened.
		class MemoryJsonFile : public JsonFile
		{
		public:
			MemoryJsonFile() :
				JsonFile(fs::path(L"<editor-bridge-memory>"), std::ios::out | std::ios::binary, DiskFileOptions::None)
			{}

			bool     Open() override { _open = true; return true; }
			void     Close() override { _open = false; }
			bool     DoesExist() override { return false; }
			bool     Delete() override { return false; }
			bool     IsOpen() const override { return _open; }
			uint32_t GetSize() override { return (uint32_t)_buffer.size(); }
			uint32_t Read(void*, uint32_t) override { return 0; }
			void     Flush() override {}
			uint32_t Write(void* data, uint32_t size) override
			{
				if (data != nullptr && size > 0)
					_buffer.insert(_buffer.end(), (const uint8_t*)data, (const uint8_t*)data + size);
				return size;
			}

		private:
			std::vector<uint8_t> _buffer;
			bool _open = true;
		};

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

		// 32 hex chars of per-session secret. Only a process that can read the
		// session file (same OS user) learns it; every request must echo it.
		std::string GenerateToken()
		{
			std::random_device rd;
			static const char* hex = "0123456789abcdef";
			std::string t;
			t.reserve(32);
			for (int i = 0; i < 16; ++i)
			{
				const unsigned v = rd();
				t.push_back(hex[(v >> 4) & 0xF]);
				t.push_back(hex[v & 0xF]);
			}
			return t;
		}

		bool ProcessAlive(uint32_t pid)
		{
			HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
			if (!h) return false;
			CloseHandle(h);
			return true;
		}

		// ---- Param helpers. Malformed (present but wrong-typed) params throw
		//      ParamError, which the dispatcher turns into InvalidParams. ----

		struct ParamError : std::runtime_error
		{
			using std::runtime_error::runtime_error;
		};

		// Read `count` numbers from params[key]. False if absent/null; throws if
		// present but malformed.
		bool ReadFloats(const json& p, const char* key, size_t count, float* out)
		{
			if (!p.contains(key) || p[key].is_null())
				return false;
			const json& v = p[key];
			if (!v.is_array() || v.size() != count)
				throw ParamError(std::string("'") + key + "' must be an array of " + std::to_string(count) + " numbers");
			for (size_t i = 0; i < count; ++i)
			{
				if (!v[i].is_number())
					throw ParamError(std::string("'") + key + "' must contain only numbers");
				out[i] = v[i].get<float>();
			}
			return true;
		}

		bool ReadVec3(const json& p, const char* key, math::Vector3& out)
		{
			float f[3];
			if (!ReadFloats(p, key, 3, f)) return false;
			out = math::Vector3(f[0], f[1], f[2]);
			return true;
		}

		// `rotation` [x,y,z,w] quaternion, else `eulerDegrees` [pitch,yaw,roll].
		bool ReadRotation(const json& p, math::Quaternion& out)
		{
			float q[4];
			if (ReadFloats(p, "rotation", 4, q))
			{
				out = math::Quaternion(q[0], q[1], q[2], q[3]);
				out.Normalize();
				return true;
			}
			float e[3];
			if (ReadFloats(p, "eulerDegrees", 3, e))
			{
				const float toRad = 3.14159265358979f / 180.0f;
				out = math::Quaternion::CreateFromYawPitchRoll(e[1] * toRad, e[0] * toRad, e[2] * toRad);
				return true;
			}
			return false;
		}

		std::string RequireString(const json& p, const char* key)
		{
			if (!p.contains(key) || !p[key].is_string() || p[key].get<std::string>().empty())
				throw ParamError(std::string("requires a non-empty '") + key + "' string");
			return p[key].get<std::string>();
		}

		// ---- Entity / component resolution (main thread only) ----

		Entity* ResolveEntity(Scene* scene, const EntityRef& ref, std::string& msg)
		{
			Entity* e = nullptr;
			switch (ref.kind)
			{
			case EntityRef::Kind::Name:
				e = scene->GetEntityByName(ref.name);
				if (!e) msg = "no entity named '" + ref.name + "'";
				break;
			case EntityRef::Kind::Id:
			{
				EntityId eid;
				eid.index = ref.index;
				eid.generation = ref.generation;
				e = scene->TryGetEntity(eid);
				if (!e) msg = "no live entity with id {index:" + std::to_string(ref.index) + ", generation:" + std::to_string(ref.generation) + "} (deleted or stale)";
				break;
			}
			case EntityRef::Kind::Index:
			{
				std::vector<EntityId> ids;
				scene->GetLiveEntityIds(ids);
				for (const EntityId& eid : ids)
					if (eid.index == ref.index) { e = scene->TryGetEntity(eid); break; }
				if (!e) msg = "no live entity at index " + std::to_string(ref.index);
				break;
			}
			default:
				msg = "invalid entity reference";
				break;
			}
			if (e && e->IsPendingDeletion())
			{
				msg = "entity '" + e->GetName() + "' is pending deletion";
				return nullptr;
			}
			return e;
		}

		// Resolve params[key] (plus legacy name/index/id when `legacy`). On
		// failure returns nullptr and sets `err` to a full error envelope.
		Entity* RequireEntity(int64_t id, Scene* scene, const json& p, const char* key, bool legacy, json& err)
		{
			EntityRef ref;
			std::string msg;
			if (!GetEntityRef(p, key, legacy, ref, msg))
			{
				err = MakeError(id, ErrorCode::InvalidParams, msg);
				return nullptr;
			}
			Entity* e = ResolveEntity(scene, ref, msg);
			if (!e)
				err = MakeError(id, ErrorCode::NotAvailable, msg);
			return e;
		}

		BaseComponent* FindComponent(Entity* e, const std::string& name)
		{
			for (BaseComponent* c : e->GetAllComponents())
				if (c && c->GetComponentName() && name == c->GetComponentName())
					return c;
			return nullptr;
		}

		json ComponentNames(Entity* e)
		{
			json comps = json::array();
			for (BaseComponent* c : e->GetAllComponents())
				if (c && c->GetComponentName()) comps.push_back(c->GetComponentName());
			return comps;
		}

		json SerializeComponent(BaseComponent* c)
		{
			MemoryJsonFile mem;
			json fields = json::object();
			c->Serialize(fields, &mem);
			return fields;
		}

		// Read-modify-write through the component's own Serialize/Deserialize:
		// components' Deserialize expects the full field set (missing keys can
		// reset to defaults), so capture current state, overlay the patch, and
		// feed the merged object back. Object-valued fields merge recursively
		// (JSON merge-patch); scalars/arrays replace. Unknown top-level keys are
		// rejected BEFORE anything is applied, so typos never half-apply.
		bool ApplyComponentFields(BaseComponent* c, const json& fields, json& after, std::string& msg)
		{
			if (!fields.is_object())
			{
				msg = "'fields' must be an object of field:value";
				return false;
			}
			MemoryJsonFile mem;
			json current = json::object();
			c->Serialize(current, &mem);
			for (auto& [key, value] : fields.items())
			{
				if (!current.contains(key))
				{
					msg = std::string("component '") + c->GetComponentName() + "' has no serialised field '" + key + "' (see inspect_component)";
					return false;
				}
			}
			for (auto& [key, value] : fields.items())
			{
				if (current[key].is_object() && value.is_object())
					current[key].merge_patch(value);
				else
					current[key] = value;
			}
			c->Deserialize(current, &mem, 0);
			after = json::object();
			c->Serialize(after, &mem);
			return true;
		}

		// Clear the editor selection when it is `e` or a descendant of `e`, so the
		// inspector never keeps widgets bound to a component/entity we are about
		// to destroy. Returns true if the selection was cleared.
		bool ReleaseSelectionOf(Entity* e)
		{
			IEditorContext* ctx = g_pEnv ? g_pEnv->_editorContext : nullptr;
			if (!ctx || !e) return false;
			for (Entity* s = ctx->GetSelectedEntity(); s != nullptr; s = s->GetParent())
			{
				if (s == e)
				{
					ctx->SetSelectedEntity(nullptr);
					return true;
				}
			}
			return false;
		}

		struct FlagName { const char* name; EntityFlags flag; };
		const FlagName kEditableFlags[] = {
			{ "DoNotSave",         EntityFlags::DoNotSave },
			{ "DoNotRender",       EntityFlags::DoNotRender },
			{ "DoNotBlockNavMesh", EntityFlags::DoNotBlockNavMesh },
			{ "ExcludeFromHLOD",   EntityFlags::ExcludeFromHLOD },
			{ "DoNotPickInEditor", EntityFlags::DoNotPickInEditor },
		};

		json EditableFlagsOf(Entity* e)
		{
			json arr = json::array();
			for (const auto& f : kEditableFlags)
				if (e->HasFlag(f.flag)) arr.push_back(f.name);
			return arr;
		}
	}

	// Marshals tasks from pipe threads onto the editor main thread. A pipe thread
	// submits a task and waits; the main thread drains + fulfils.
	//
	// Exactly-once semantics: a task is Pending -> Running -> done, or Pending ->
	// Cancelled. A waiter that times out cancels the task atomically, and Drain
	// skips cancelled tasks - so "timed out" always means "did NOT execute".
	// Once a task is Running the waiter waits for it to finish.
	class MainThreadQueue
	{
	public:
		enum class RunResult { Done, NotStarted };

		RunResult Run(std::function<json()> fn, json& out, unsigned pendingTimeoutMs)
		{
			auto task = std::make_shared<Task>();
			task->fn = std::move(fn);
			auto fut = task->result.get_future();
			{
				std::lock_guard<std::mutex> lk(_mutex);
				if (_stopping.load())
					return RunResult::NotStarted;
				_queue.push(task);
			}
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(pendingTimeoutMs);
			for (;;)
			{
				if (fut.wait_for(std::chrono::milliseconds(20)) == std::future_status::ready)
				{
					out = fut.get();
					return RunResult::Done;
				}
				if (std::chrono::steady_clock::now() >= deadline || _stopping.load())
				{
					int expected = kPending;
					if (task->state.compare_exchange_strong(expected, kCancelled))
						return RunResult::NotStarted;
					// Already running on the main thread: it will complete.
					out = fut.get();
					return RunResult::Done;
				}
			}
		}

		// Main thread: execute the tasks that were queued when the drain began
		// (tasks arriving mid-drain wait for the next frame, so a flood of
		// requests can't stall one frame indefinitely).
		void Drain()
		{
			size_t budget;
			{
				std::lock_guard<std::mutex> lk(_mutex);
				budget = _queue.size();
			}
			while (budget-- > 0)
			{
				std::shared_ptr<Task> task;
				{
					std::lock_guard<std::mutex> lk(_mutex);
					if (_queue.empty()) return;
					task = _queue.front();
					_queue.pop();
				}
				int expected = kPending;
				if (!task->state.compare_exchange_strong(expected, kRunning))
					continue; // cancelled by its waiter
				json r;
				try { r = task->fn(); }
				catch (const std::exception& e) { r = json{ {"__error", e.what()} }; }
				catch (...) { r = json{ {"__error", "unknown exception"} }; }
				task->result.set_value(std::move(r));
			}
		}

		void Shutdown() { _stopping.store(true); }
		void Reset() { _stopping.store(false); }

	private:
		static constexpr int kPending = 0, kRunning = 1, kCancelled = 2;
		struct Task
		{
			std::function<json()> fn;
			std::promise<json>    result;
			std::atomic<int>      state{ kPending };
		};
		std::mutex _mutex;
		std::queue<std::shared_ptr<Task>> _queue;
		std::atomic<bool> _stopping{ false };
	};

	class EditorBridgePlugin : public IPlugin, public IEditorToolPlugin, public ILogFileListener
	{
	public:
		~EditorBridgePlugin() { StopBridge(); }

		// --- ILogFileListener: capture recent log lines into a bounded ring
		//     buffer with monotonically increasing sequence numbers. ---
		void OnLogMessage(const LogMessage& message) override
		{
			std::lock_guard<std::mutex> lk(_logMutex);
			_logRing.push_back({ ++_logSeq, std::string("[") + LogLevelName(message.level) + "] " + message.text, message.level });
			while (_logRing.size() > kMaxLogRing)
				_logRing.pop_front();
		}

		// --- IPlugin ---
		void Destroy() override { StopBridge(); }

		void GetVersionData(VersionData* data) override
		{
			data->majorVersion = 0;
			data->minorVersion = 2;
			data->name = "HexEngine.EditorBridge";
			data->description = "Editor-only live inspection/authoring bridge (MCP). Off unless --enable-editor-bridge; writes need HEXENGINE_EDITOR_BRIDGE_WRITE=1.";
			data->author = "HexEngine";
			data->enabled = true;
		}

		IPluginInterface* CreateInterface(const std::string&) override { return nullptr; }
		void GetDependencies(std::vector<std::string>&) const override {}
		IEditorToolPlugin* GetEditorToolPlugin() override { return this; }

		// --- IEditorToolPlugin (editor-only entry points) ---
		void OnCreateUI(MenuBar*) override
		{
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
			PumpMainThread();
		}

		void OnEditorFrameTick() override
		{
			PumpMainThread();
			_frameCount.fetch_add(1);
		}

	private:
		using Handler = std::function<json(int64_t id, const json& params)>;

		struct MethodInfo
		{
			bool    write = false;      // requires HEXENGINE_EDITOR_BRIDGE_WRITE=1
			bool    mainThread = true;  // marshal to the editor main thread
			bool    batchable = true;   // may appear inside `batch`
			Handler fn;
		};

		// Main thread: run queued tasks, then apply coalesced deferred work.
		void PumpMainThread()
		{
			_mainThread.Drain();
			if (_pvsDirty)
			{
				_pvsDirty = false;
				// One PVS rebuild per frame no matter how many mutations ran -
				// runtime-created/moved entities are invisible until the
				// potentially-visible-set is rebuilt.
				if (auto scene = PrimaryUserScene())
					scene->ForceRebuildPVS();
			}
		}

		// Main thread only (called from handlers).
		void MarkPvsDirty() { _pvsDirty = true; }

		void StartBridge()
		{
			const uint32_t pid = (uint32_t)GetCurrentProcessId();
			const std::string pipeName = PipeNameForPid(pid);

			RegisterMethods();
			_mainThread.Reset();
			_token = GenerateToken();
			CleanStaleSessions(pid);

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
			LOG_INFO("EditorBridge: listening on '%s' (protocol v%d, writes %s).", pipeName.c_str(), kProtocolVersion,
				BridgeWriteEnabled() ? "ENABLED" : "disabled");
		}

		void StopBridge()
		{
			if (!_started) return;
			if (g_pEnv)
				g_pEnv->GetLogFile().RemoveListener(this);
			_mainThread.Shutdown();
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
			s.token = _token;
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

		// Remove session files left behind by editor processes that have exited
		// (crash / kill), so discovery never points a client at a dead pipe.
		void CleanStaleSessions(uint32_t selfPid)
		{
			std::error_code ec;
			for (fs::directory_iterator it(SessionDirPath(), ec), end; !ec && it != end; it.increment(ec))
			{
				if (!it->is_regular_file(ec)) continue;
				const fs::path p = it->path();
				if (p.extension() != ".json" || p.filename().string().rfind("session-", 0) != 0) continue;

				std::ifstream in(p, std::ios::binary);
				if (!in.is_open()) continue;
				std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
				in.close();

				uint32_t pid = 0;
				try { pid = json::parse(text).value("pid", 0u); } catch (...) { fs::remove(p, ec); continue; }
				if (pid == 0) { fs::remove(p, ec); continue; }
				if (pid == selfPid) continue;
				if (!ProcessAlive(pid))
					fs::remove(p, ec);
			}
		}

		// Pipe thread: parse -> authenticate -> dispatch -> serialise (capped).
		std::string DispatchLine(const std::string& line)
		{
			Request req;
			std::string err;
			if (!ParseRequestText(line, req, err))
				return MakeError(0, ErrorCode::InvalidRequest, err).dump();

			if (!TokensMatch(req.token, _token))
				return MakeError(req.id, ErrorCode::Unauthorized, "missing or invalid session token").dump();

			json response = Dispatch(req);
			return SerializeResponseCapped(response, req.id);
		}

		// Run a handler, mapping exceptions to error envelopes.
		static json Invoke(const MethodInfo& info, int64_t id, const json& params)
		{
			try
			{
				return info.fn(id, params);
			}
			catch (const ParamError& e)
			{
				return MakeError(id, ErrorCode::InvalidParams, e.what());
			}
			catch (const json::exception& e)
			{
				return MakeError(id, ErrorCode::InvalidParams, std::string("bad parameter type: ") + e.what());
			}
			catch (const std::exception& e)
			{
				return MakeError(id, ErrorCode::Internal, e.what());
			}
		}

		// Pipe thread: run `fn` on the main thread and return its envelope.
		json OnMain(int64_t id, std::function<json()> fn)
		{
			json out;
			if (_mainThread.Run(std::move(fn), out, kPendingTimeoutMs) == MainThreadQueue::RunResult::NotStarted)
				return MakeError(id, ErrorCode::NotAvailable,
					"the editor main thread did not start the request within " + std::to_string(kPendingTimeoutMs) +
					" ms (busy, loading or shutting down). It was NOT executed - safe to retry.");
			if (out.is_object() && out.contains("__error"))
				return MakeError(id, ErrorCode::Internal, out["__error"].get<std::string>());
			return out;
		}

		static json WriteDenied(int64_t id, const std::string& method)
		{
			return MakeError(id, ErrorCode::Unauthorized,
				"'" + method + "' mutates editor state and requires the write opt-in (set HEXENGINE_EDITOR_BRIDGE_WRITE=1 before launching the editor)");
		}

		json Dispatch(const Request& req)
		{
			const int64_t id = req.id;
			if (req.method == "batch")
				return DispatchBatch(req);

			auto it = _methods.find(req.method);
			if (it == _methods.end())
				return MakeError(id, ErrorCode::UnknownMethod, "No bridge method named " + req.method);
			const MethodInfo& info = it->second;
			if (info.write && !BridgeWriteEnabled())
				return WriteDenied(id, req.method);

			const json params = req.params;
			if (!info.mainThread)
				return Invoke(info, id, params);
			return OnMain(id, [&info, id, params]() { return Invoke(info, id, params); });
		}

		// `batch`: run many methods in ONE main-thread task (a single editor
		// frame, one coalesced PVS rebuild). Params:
		//   ops: [{method, params, as?}]   (as = label for {"$ref":"label.path"})
		//   stopOnError: bool (default true) - remaining ops are reported skipped
		// Not transactional: ops that succeeded before a failure stay applied.
		json DispatchBatch(const Request& req)
		{
			const int64_t id = req.id;
			const json& p = req.params;
			if (!p.contains("ops") || !p["ops"].is_array() || p["ops"].empty())
				return MakeError(id, ErrorCode::InvalidParams, "batch requires a non-empty 'ops' array of {method, params, as?}");
			if (p["ops"].size() > kMaxBatchOps)
				return MakeError(id, ErrorCode::InvalidParams, "batch exceeds " + std::to_string(kMaxBatchOps) + " ops");
			const bool stopOnError = p.contains("stopOnError") && p["stopOnError"].is_boolean() ? p["stopOnError"].get<bool>() : true;

			// Validate every op up front so a bad op fails the whole batch
			// before anything runs.
			struct Op { const MethodInfo* info; std::string method; json params; std::string label; };
			std::vector<Op> ops;
			ops.reserve(p["ops"].size());
			std::map<std::string, size_t> seenLabels;
			for (size_t i = 0; i < p["ops"].size(); ++i)
			{
				const json& o = p["ops"][i];
				const std::string where = "ops[" + std::to_string(i) + "]";
				if (!o.is_object() || !o.contains("method") || !o["method"].is_string())
					return MakeError(id, ErrorCode::InvalidParams, where + " must be an object with a 'method' string");
				const std::string method = o["method"].get<std::string>();
				auto it = _methods.find(method);
				if (it == _methods.end())
					return MakeError(id, ErrorCode::UnknownMethod, where + ": no bridge method named " + method);
				if (!it->second.batchable)
					return MakeError(id, ErrorCode::InvalidParams, where + ": '" + method + "' cannot run inside a batch (call it on its own)");
				if (it->second.write && !BridgeWriteEnabled())
					return WriteDenied(id, method);
				if (o.contains("params") && !o["params"].is_object())
					return MakeError(id, ErrorCode::InvalidParams, where + ".params must be an object");
				std::string label;
				if (o.contains("as"))
				{
					if (!o["as"].is_string() || o["as"].get<std::string>().empty())
						return MakeError(id, ErrorCode::InvalidParams, where + ".as must be a non-empty string");
					label = o["as"].get<std::string>();
					if (std::all_of(label.begin(), label.end(), [](char c) { return c >= '0' && c <= '9'; }) ||
						label.find('.') != std::string::npos)
						return MakeError(id, ErrorCode::InvalidParams, where + ".as must not be numeric or contain '.'");
					if (!seenLabels.emplace(label, i).second)
						return MakeError(id, ErrorCode::InvalidParams, where + ".as '" + label + "' is already used");
				}
				ops.push_back({ &it->second, method, o.contains("params") ? o["params"] : json::object(), label });
			}

			return OnMain(id, [id, ops = std::move(ops), stopOnError]() mutable -> json {
				const auto start = std::chrono::steady_clock::now();
				std::vector<json> results(ops.size());
				BatchLabels labels;
				json out = json::array();
				size_t executed = 0, failed = 0;
				bool stopped = false;
				for (size_t i = 0; i < ops.size(); ++i)
				{
					Op& op = ops[i];
					json entry = json{ {"index", i}, {"method", op.method} };
					if (!op.label.empty()) entry["as"] = op.label;
					if (stopped)
					{
						entry["ok"] = false;
						entry["skipped"] = true;
						out.push_back(std::move(entry));
						continue;
					}

					std::string refErr;
					json r;
					if (!ResolveBatchRefs(op.params, results, i, labels, refErr))
						r = MakeError((int64_t)i, ErrorCode::InvalidParams, refErr);
					else
					{
						r = Invoke(*op.info, (int64_t)i, op.params);
						++executed;
					}

					if (!op.label.empty())
						labels[op.label] = i;
					if (r.value("ok", false))
					{
						results[i] = r["result"];
						entry["ok"] = true;
						entry["result"] = r["result"];
					}
					else
					{
						++failed;
						entry["ok"] = false;
						entry["error"] = r.contains("error") ? r["error"] : json("unknown error");
						if (stopOnError) stopped = true;
					}
					out.push_back(std::move(entry));
				}
				const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
				return MakeResult(id, json{
					{"count", ops.size()},
					{"executed", executed},
					{"failed", failed},
					{"stoppedEarly", stopped},
					{"mainThreadMs", ms},
					{"results", out},
				});
			});
		}

		void Reg(const char* name, bool write, bool mainThread, bool batchable, Handler fn)
		{
			MethodInfo info;
			info.write = write;
			info.mainThread = mainThread;
			info.batchable = batchable;
			info.fn = std::move(fn);
			_methods[name] = std::move(info);
		}

		static json NoScene(int64_t id)
		{
			return MakeError(id, ErrorCode::NotAvailable, "no scene is currently open");
		}

		// Shared by add_component and create_entity's inline `components`.
		// Returns a result object, or an error envelope in `err` (and null).
		static json AddComponentByName(int64_t id, Entity* entity, const json& spec, json& err)
		{
			if (!spec.is_object() || !spec.contains("component") || !spec["component"].is_string())
			{
				err = MakeError(id, ErrorCode::InvalidParams, "component spec requires a 'component' (registered class name) string");
				return nullptr;
			}
			const std::string componentName = spec["component"].get<std::string>();
			auto* cls = g_pEnv->_classRegistry->Find(componentName);
			if (cls == nullptr)
			{
				err = MakeError(id, ErrorCode::NotAvailable, "component class '" + componentName + "' is not registered (see list_component_types)");
				return nullptr;
			}

			// Same sequence the prefab loader / inspector use for by-name
			// construction. AddComponent rejects a duplicate type by DELETING the
			// new instance and returning the existing one - only ever use the
			// returned pointer.
			BaseComponent* created = cls->newInstanceFn(entity);
			if (created == nullptr)
			{
				err = MakeError(id, ErrorCode::Internal, "failed to instantiate component '" + componentName + "'");
				return nullptr;
			}
			BaseComponent* component = entity->AddComponent(created);
			const bool existed = (component != created);
			if (component == nullptr)
			{
				err = MakeError(id, ErrorCode::Internal, "Entity::AddComponent failed for '" + componentName + "'");
				return nullptr;
			}

			json result = json{ {"component", componentName}, {"alreadyExisted", existed} };
			if (spec.contains("fields") && !spec["fields"].is_null())
			{
				json after;
				std::string msg;
				if (!ApplyComponentFields(component, spec["fields"], after, msg))
				{
					err = MakeError(id, ErrorCode::InvalidParams, msg + " (the component WAS added; fields were not applied)");
					return nullptr;
				}
				result["fields"] = after;
			}
			return result;
		}

		void RegisterMethods()
		{
			_methods.clear();
			constexpr bool R = false, W = true;    // write?
			constexpr bool Main = true, Pipe = false;
			constexpr bool Batch = true, Solo = false;

			// ================= READ =================

			Reg("get_editor_status", R, Main, Batch, [this](int64_t id, const json&) -> json {
				std::string sceneName;
				if (auto scene = PrimaryUserScene())
					sceneName = WideToUtf8(scene->GetName());
				IEditorContext* ctx = g_pEnv ? g_pEnv->_editorContext : nullptr;
				const bool playing = g_pEnv && g_pEnv->IsGameRunning();
				std::string projectName = ctx ? ctx->GetProjectName() : std::string();
				return MakeResult(id, json{
					{"editorRunning", true},
					{"sceneName", sceneName},
					{"mode", playing ? "play" : "edit"},
					{"projectName", projectName.empty() ? json(nullptr) : json(projectName)},
					{"protocolVersion", kProtocolVersion},
					{"writeEnabled", BridgeWriteEnabled()},
					{"frame", _frameCount.load()},
				});
			});

			Reg("get_open_scene", R, Main, Batch, [](int64_t id, const json&) -> json {
				auto scene = PrimaryUserScene();
				if (!scene) return NoScene(id);
				std::vector<EntityId> ids;
				scene->GetLiveEntityIds(ids);
				return MakeResult(id, json{
					{"sceneName", WideToUtf8(scene->GetName())},
					{"entityCount", ids.size()},
				});
			});

			Reg("list_entities", R, Main, Batch, [](int64_t id, const json& p) -> json {
				const size_t offset = (size_t)p.value("offset", 0);
				size_t limit = (size_t)p.value("limit", 200);
				if (limit > 1000) limit = 1000;
				const std::string filter = p.value("filter", std::string());
				const std::string component = p.value("component", std::string());
				auto scene = PrimaryUserScene();
				if (!scene) return NoScene(id);
				std::vector<EntityId> ids;
				scene->GetLiveEntityIds(ids);
				json list = json::array();
				size_t matched = 0;
				for (const EntityId& eid : ids)
				{
					Entity* e = scene->TryGetEntity(eid);
					if (!e || e->IsPendingDeletion()) continue;
					const std::string& name = e->GetName();
					if (!filter.empty() && name.find(filter) == std::string::npos) continue;
					if (!component.empty() && FindComponent(e, component) == nullptr) continue;
					if (matched++ < offset) continue;
					if (list.size() >= limit) continue; // keep counting matches
					list.push_back(json{ {"id", EntityIdToJson(eid)}, {"name", name}, {"components", ComponentNames(e)} });
				}
				return MakeResult(id, json{ {"total", ids.size()}, {"matched", matched}, {"offset", offset}, {"returned", list.size()}, {"entities", list} });
			});

			Reg("inspect_entity", R, Main, Batch, [](int64_t id, const json& p) -> json {
				auto scene = PrimaryUserScene();
				if (!scene) return NoScene(id);
				json err;
				Entity* e = RequireEntity(id, scene.get(), p, "entity", true, err);
				if (!e) return err;
				return MakeResult(id, EntityDetailJson(e));
			});

			Reg("list_components", R, Main, Batch, [](int64_t id, const json&) -> json {
				auto scene = PrimaryUserScene();
				if (!scene) return NoScene(id);
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

			Reg("list_component_types", R, Main, Batch, [](int64_t id, const json&) -> json {
				std::vector<std::string> names;
				for (const auto& [hash, cls] : g_pEnv->_classRegistry->GetAllClasses())
					names.push_back(cls.name);
				std::sort(names.begin(), names.end());
				return MakeResult(id, json{ {"source", "class-registry"}, {"count", names.size()}, {"componentTypes", names},
					{"note", "Every name here can be passed to add_component. Field names come from inspect_component on an instance."} });
			});

			Reg("validate_current_scene", R, Main, Batch, [](int64_t id, const json&) -> json {
				auto scene = PrimaryUserScene();
				if (!scene) return NoScene(id);
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
					{"note", "Validation is minimal (entity presence/naming). Deeper reference/asset validation is a follow-up."},
				});
			});

			Reg("get_open_project", R, Main, Batch, [](int64_t id, const json&) -> json {
				IEditorContext* ctx = g_pEnv ? g_pEnv->_editorContext : nullptr;
				if (!ctx)
					return MakeError(id, ErrorCode::NotAvailable, "editor context not available (bridge is not running inside the editor)");
				const std::string folder = ctx->GetProjectFolderPath();
				if (folder.empty())
					return MakeResult(id, json{ {"projectOpen", false} });
				return MakeResult(id, json{
					{"projectOpen", true},
					{"name", ctx->GetProjectName()},
					{"folderPath", folder},
					{"filePath", ctx->GetProjectFilePath()},
				});
			});

			Reg("get_selected_entity", R, Main, Batch, [](int64_t id, const json&) -> json {
				IEditorContext* ctx = g_pEnv ? g_pEnv->_editorContext : nullptr;
				if (!ctx)
					return MakeError(id, ErrorCode::NotAvailable, "editor context not available (bridge is not running inside the editor)");
				Entity* e = ctx->GetSelectedEntity();
				if (!e)
					return MakeResult(id, json{ {"hasSelection", false}, {"entity", nullptr} });
				return MakeResult(id, json{ {"hasSelection", true}, {"entity", EntityDetailJson(e)} });
			});

			Reg("inspect_component", R, Main, Batch, [](int64_t id, const json& p) -> json {
				const std::string compName = RequireString(p, "component");
				auto scene = PrimaryUserScene();
				if (!scene) return NoScene(id);
				json err;
				Entity* e = RequireEntity(id, scene.get(), p, "entity", true, err);
				if (!e) return err;
				BaseComponent* target = FindComponent(e, compName);
				if (!target)
					return MakeError(id, ErrorCode::NotAvailable, "entity '" + e->GetName() + "' has no component named '" + compName + "' (it has: " + ComponentNames(e).dump() + ")");
				return MakeResult(id, json{
					{"entity", EntityBrief(e)},
					{"component", compName},
					{"fields", SerializeComponent(target)},
					{"note", "Fields are the component's own Serialize() output; components with an empty/no-op Serialize return {}."},
				});
			});

			Reg("list_loaded_resources", R, Pipe, Batch, [](int64_t id, const json& p) -> json {
				// EnumerateLoadedResources is internally locked and exposes no
				// pointers, so it's safe from any thread.
				size_t max = (size_t)p.value("limit", 500);
				if (max > 2000) max = 2000;
				json arr = json::array();
				for (const auto& r : g_pEnv->GetResourceSystem().EnumerateLoadedResources(max))
					arr.push_back(json{ {"id", r.id}, {"fsPath", WideToUtf8(r.fsPath)}, {"absPath", r.absPath}, {"refCount", r.useCount} });
				return MakeResult(id, json{ {"count", arr.size()}, {"resources", arr} });
			});

			Reg("find_missing_references", R, Pipe, Batch, [](int64_t id, const json&) -> json {
				return MakeError(id, ErrorCode::NotImplemented, "reference validation is a follow-up (TODO)");
			});

			Reg("get_recent_engine_logs", R, Pipe, Batch, [this](int64_t id, const json& p) -> json {
				// Thread-safe ring buffer. `sinceSeq` returns only lines newer
				// than a previous call's `lastSeq` (an incremental log cursor);
				// `minLevel` filters (debug|info|warn|crit).
				size_t max = (size_t)p.value("max", 200);
				if (max > 1000) max = 1000;
				const uint64_t sinceSeq = p.value("sinceSeq", (uint64_t)0);
				const std::string minLevelName = p.value("minLevel", std::string("debug"));
				int minLevel = 0;
				if (minLevelName == "info") minLevel = 1;
				else if (minLevelName == "warn") minLevel = 2;
				else if (minLevelName == "crit") minLevel = 3;
				auto rank = [](LogLevel l) {
					switch (l) { case LogLevel::Debug: return 0; case LogLevel::Info: return 1; case LogLevel::Warn: return 2; case LogLevel::Crit: return 3; default: return 1; }
				};
				json logs = json::array();
				uint64_t lastSeq = 0;
				bool truncated = false;
				{
					std::lock_guard<std::mutex> lk(_logMutex);
					lastSeq = _logSeq;
					std::vector<const LogEntry*> picked;
					for (const LogEntry& e : _logRing)
						if (e.seq > sinceSeq && rank(e.level) >= minLevel)
							picked.push_back(&e);
					const size_t start = picked.size() > max ? picked.size() - max : 0;
					truncated = start > 0 || (sinceSeq > 0 && !_logRing.empty() && _logRing.front().seq > sinceSeq + 1);
					for (size_t i = start; i < picked.size(); ++i)
						logs.push_back(json{ {"seq", picked[i]->seq}, {"text", picked[i]->text} });
				}
				return MakeResult(id, json{ {"count", logs.size()}, {"lastSeq", lastSeq}, {"truncated", truncated}, {"logs", logs} });
			});

			Reg("list_projects", R, Pipe, Batch, [](int64_t id, const json&) -> json {
				const fs::path listPath = FileSystem::GetUserDataRoot() / L"Projects.json";
				std::ifstream in(listPath, std::ios::binary);
				if (!in.is_open())
					return MakeResult(id, json{ {"count", 0}, {"projects", json::array()},
						{"note", "no Projects.json found (" + listPath.string() + ")"} });
				std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
				in.close();
				json projects = json::array();
				try
				{
					for (const auto& entry : json::parse(text).value("projects", json::array()))
					{
						if (!entry.is_string()) continue;
						const std::string path = entry.get<std::string>();
						projects.push_back(json{
							{"path", path},
							{"name", fs::path(path).stem().string()},
							{"exists", fs::exists(fs::path(path))},
						});
					}
				}
				catch (const std::exception& ex)
				{
					return MakeError(id, ErrorCode::Internal, std::string("Projects.json parse failed: ") + ex.what());
				}
				return MakeResult(id, json{ {"count", projects.size()}, {"projects", projects} });
			});

			// ================= WRITE =================

			Reg("exec_console", W, Main, Batch, [](int64_t id, const json& p) -> json {
				const std::string cmd = RequireString(p, "command");
				if (!g_pEnv || !g_pEnv->_commandManager)
					return MakeError(id, ErrorCode::NotAvailable, "command manager not available");
				g_pEnv->_commandManager->ProcessCommandInput(cmd);
				return MakeResult(id, json{ {"executed", cmd} });
			});

			Reg("open_project", W, Main, Solo, [](int64_t id, const json& p) -> json {
				std::string path = p.value("path", std::string());
				const std::string name = p.value("name", std::string());
				if (path.empty() && name.empty())
					return MakeError(id, ErrorCode::InvalidParams, "open_project requires a 'path' (project .json path) or a 'name' (stem from list_projects)");
				if (path.empty())
				{
					const fs::path listPath = FileSystem::GetUserDataRoot() / L"Projects.json";
					std::ifstream in(listPath, std::ios::binary);
					if (in.is_open())
					{
						std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
						in.close();
						try
						{
							for (const auto& entry : json::parse(text).value("projects", json::array()))
							{
								if (!entry.is_string()) continue;
								const std::string candidate = entry.get<std::string>();
								if (_stricmp(fs::path(candidate).stem().string().c_str(), name.c_str()) == 0)
								{
									path = candidate;
									break;
								}
							}
						}
						catch (...) {}
					}
					if (path.empty())
						return MakeError(id, ErrorCode::NotAvailable, "no recent project named '" + name + "' (see list_projects)");
				}
				IEditorContext* ctx = g_pEnv ? g_pEnv->_editorContext : nullptr;
				if (!ctx)
					return MakeError(id, ErrorCode::NotAvailable, "editor context not available (bridge is not running inside the editor)");
				std::string err;
				if (!ctx->OpenProject(path, err))
					return MakeError(id, ErrorCode::NotAvailable, err);
				// Load continues on a worker thread; poll get_editor_status until sceneName is set.
				return MakeResult(id, json{ {"opening", true}, {"path", path} });
			});

			Reg("create_entity", W, Main, Batch, [this](int64_t id, const json& p) -> json {
				auto scene = PrimaryUserScene();
				if (!scene) return NoScene(id);
				const std::string name = RequireString(p, "name");
				math::Vector3 pos(0.0f, 0.0f, 0.0f);
				ReadVec3(p, "position", pos);
				math::Quaternion rot(0.0f, 0.0f, 0.0f, 1.0f);
				ReadRotation(p, rot);
				math::Vector3 scale(1.0f, 1.0f, 1.0f);
				ReadVec3(p, "scale", scale);

				Entity* parent = nullptr;
				if (p.contains("parent") && !p["parent"].is_null())
				{
					json err;
					parent = RequireEntity(id, scene.get(), p, "parent", false, err);
					if (!parent) return err; // validated BEFORE creating anything
				}
				if (p.contains("components") && !p["components"].is_array())
					return MakeError(id, ErrorCode::InvalidParams, "'components' must be an array of {component, fields?}");

				// Explicit args: the bridge doesn't link SimpleMath statics.
				Entity* e = scene->CreateEntity(name, pos, rot, scale);
				if (e == nullptr)
					return MakeError(id, ErrorCode::Internal, "Scene::CreateEntity returned null");
				if (p.value("persistent", true) == false)
					e->SetFlag(EntityFlags::DoNotSave);
				if (parent)
					e->SetParent(parent);

				json added = json::array();
				if (p.contains("components"))
				{
					for (const json& spec : p["components"])
					{
						json err;
						json r = AddComponentByName(id, e, spec, err);
						if (r.is_null())
						{
							// Report which entity exists so the caller can fix or delete it.
							err["error"]["message"] = err["error"]["message"].get<std::string>() +
								" (entity '" + e->GetName() + "' WAS created with the components before this one)";
							err["error"]["entity"] = EntityBrief(e);
							MarkPvsDirty();
							return err;
						}
						added.push_back(r);
					}
				}
				MarkPvsDirty();
				json result = EntityBrief(e);
				if (!added.empty()) result["components"] = added;
				return MakeResult(id, result);
			});

			Reg("add_component", W, Main, Batch, [this](int64_t id, const json& p) -> json {
				auto scene = PrimaryUserScene();
				if (!scene) return NoScene(id);
				json err;
				Entity* entity = RequireEntity(id, scene.get(), p, "entity", true, err);
				if (!entity) return err;
				json r = AddComponentByName(id, entity, p, err);
				if (r.is_null()) return err;
				r["entity"] = EntityBrief(entity);
				MarkPvsDirty();
				return MakeResult(id, r);
			});

			Reg("remove_component", W, Main, Batch, [this](int64_t id, const json& p) -> json {
				const std::string compName = RequireString(p, "component");
				auto scene = PrimaryUserScene();
				if (!scene) return NoScene(id);
				json err;
				Entity* entity = RequireEntity(id, scene.get(), p, "entity", true, err);
				if (!entity) return err;
				BaseComponent* c = FindComponent(entity, compName);
				if (!c)
					return MakeError(id, ErrorCode::NotAvailable, "entity '" + entity->GetName() + "' has no component named '" + compName + "'");
				if (c->GetComponentId() == Transform::_GetComponentId())
					return MakeError(id, ErrorCode::InvalidParams, "the Transform component cannot be removed");
				// The inspector holds widgets bound to the selected entity's
				// components - drop the selection before destroying one.
				const bool selectionCleared = ReleaseSelectionOf(entity);
				entity->RemoveComponent(c);
				MarkPvsDirty();
				return MakeResult(id, json{ {"entity", EntityBrief(entity)}, {"removed", compName}, {"selectionCleared", selectionCleared} });
			});

			Reg("rename_entity", W, Main, Batch, [](int64_t id, const json& p) -> json {
				const std::string newName = RequireString(p, "newName");
				auto scene = PrimaryUserScene();
				if (!scene) return NoScene(id);
				json err;
				Entity* e = RequireEntity(id, scene.get(), p, "entity", true, err);
				if (!e) return err;
				const std::string before = e->GetName();
				std::string finalName;
				if (!scene->RenameEntity(e, newName, &finalName))
					return MakeError(id, ErrorCode::Internal, "Scene::RenameEntity refused the rename");
				return MakeResult(id, json{ {"id", EntityIdToJson(e->GetId())}, {"before", before}, {"name", finalName},
					{"note", finalName != newName ? "name was de-duplicated by the scene" : ""} });
			});

			Reg("set_parent", W, Main, Batch, [this](int64_t id, const json& p) -> json {
				auto scene = PrimaryUserScene();
				if (!scene) return NoScene(id);
				json err;
				Entity* e = RequireEntity(id, scene.get(), p, "entity", true, err);
				if (!e) return err;
				Entity* parent = nullptr;
				if (p.contains("parent") && !p["parent"].is_null())
				{
					parent = RequireEntity(id, scene.get(), p, "parent", false, err);
					if (!parent) return err;
					for (Entity* a = parent; a != nullptr; a = a->GetParent())
						if (a == e)
							return MakeError(id, ErrorCode::InvalidParams, "cannot parent an entity to itself or one of its descendants");
				}
				const bool preserveWorld = p.value("preserveWorldPosition", true);
				e->SetParent(parent, preserveWorld);
				MarkPvsDirty();
				return MakeResult(id, json{ {"entity", EntityBrief(e)}, {"parent", parent ? EntityBrief(parent) : json(nullptr)} });
			});

			Reg("set_entity_flags", W, Main, Batch, [this](int64_t id, const json& p) -> json {
				auto scene = PrimaryUserScene();
				if (!scene) return NoScene(id);
				json err;
				Entity* e = RequireEntity(id, scene.get(), p, "entity", true, err);
				if (!e) return err;
				auto lookup = [](const std::string& n, EntityFlags& out) {
					for (const auto& f : kEditableFlags)
						if (n == f.name) { out = f.flag; return true; }
					return false;
				};
				std::vector<std::pair<EntityFlags, bool>> changes;
				for (const char* key : { "set", "clear" })
				{
					if (!p.contains(key)) continue;
					if (!p[key].is_array())
						return MakeError(id, ErrorCode::InvalidParams, std::string("'") + key + "' must be an array of flag names");
					for (const json& n : p[key])
					{
						EntityFlags f;
						if (!n.is_string() || !lookup(n.get<std::string>(), f))
							return MakeError(id, ErrorCode::InvalidParams, "unknown flag " + n.dump() + " (editable: DoNotSave, DoNotRender, DoNotBlockNavMesh, ExcludeFromHLOD, DoNotPickInEditor)");
						changes.push_back({ f, std::string(key) == "set" });
					}
				}
				for (auto& [f, set] : changes)
				{
					if (set) e->SetFlag(f);
					else e->ClearFlags(f);
				}
				if (!changes.empty()) MarkPvsDirty();
				return MakeResult(id, json{ {"entity", EntityBrief(e)}, {"flags", EditableFlagsOf(e)} });
			});

			Reg("clone_entity", W, Main, Batch, [this](int64_t id, const json& p) -> json {
				auto scene = PrimaryUserScene();
				if (!scene) return NoScene(id);
				const std::string newName = RequireString(p, "newName");
				json err;
				Entity* src = RequireEntity(id, scene.get(), p, "entity", true, err);
				if (!src) return err;

				// Kit meshes are often baked in world space with the entity at
				// the origin, so 'position' acts as a world-space OFFSET there.
				math::Vector3 pos(0.0f, 0.0f, 0.0f);
				ReadVec3(p, "position", pos);
				math::Quaternion rot(0.0f, 0.0f, 0.0f, 1.0f);
				ReadRotation(p, rot);
				math::Vector3 scale(1.0f, 1.0f, 1.0f);
				ReadVec3(p, "scale", scale);

				Entity* clone = scene->CloneEntity(src, newName, pos, rot, scale, true);
				if (clone == nullptr)
					return MakeError(id, ErrorCode::Internal, "Scene::CloneEntity returned null");
				if (!p.value("persistent", false))
					clone->SetFlag(EntityFlags::DoNotSave);
				MarkPvsDirty();
				json result = EntityBrief(clone);
				result["source"] = EntityBrief(src);
				result["clone"] = clone->GetName(); // legacy field
				return MakeResult(id, result);
			});

			Reg("select_entity", W, Main, Batch, [](int64_t id, const json& p) -> json {
				IEditorContext* ctx = g_pEnv ? g_pEnv->_editorContext : nullptr;
				if (!ctx)
					return MakeError(id, ErrorCode::NotAvailable, "editor context not available (bridge is not running inside the editor)");
				if (p.value("clear", false))
				{
					if (!ctx->SetSelectedEntity(nullptr))
						return MakeError(id, ErrorCode::Internal, "editor refused the selection change");
					return MakeResult(id, json{ {"selected", nullptr} });
				}
				auto scene = PrimaryUserScene();
				if (!scene) return NoScene(id);
				json err;
				Entity* e = RequireEntity(id, scene.get(), p, "entity", true, err);
				if (!e) return err;
				if (!ctx->SetSelectedEntity(e))
					return MakeError(id, ErrorCode::Internal, "editor refused the selection change");
				return MakeResult(id, json{ {"selected", e->GetName()}, {"id", EntityIdToJson(e->GetId())} });
			});

			Reg("set_entity_transform", W, Main, Batch, [this](int64_t id, const json& p) -> json {
				auto scene = PrimaryUserScene();
				if (!scene) return NoScene(id);
				json err;
				Entity* e = RequireEntity(id, scene.get(), p, "entity", true, err);
				if (!e) return err;

				json applied = json::object();
				math::Vector3 v;
				if (ReadVec3(p, "position", v))
				{
					e->SetPosition(v);
					applied["position"] = p["position"];
				}

				// CAMERAS OWN THEIR ORIENTATION. Camera::UpdateRotation rebuilds
				// the Transform rotation AND _lookDir from the camera's own
				// _cameraAngles, and only when those angles change; the view
				// matrix is CreateLookAt(pos, pos + _lookDir, up) with `up` taken
				// from the Transform. Writing the Transform quaternion directly
				// leaves a camera with a STALE look direction and a NEW up vector
				// (rolled horizons, pitches that don't look where told). For a
				// camera, drive the angles (or SetLookDirection for a quaternion)
				// and let the camera write its own Transform.
				Camera* camera = e->GetComponentDerived<Camera>();

				float q[4];
				float eul[3];
				if (ReadFloats(p, "rotation", 4, q))
				{
					math::Quaternion quat(q[0], q[1], q[2], q[3]);
					quat.Normalize();
					if (camera != nullptr)
					{
						// Literal axes: SimpleMath's static Vector3::Forward / ::Up
						// data members are not exported to plugins (LNK2001).
						camera->SetLookDirection(
							math::Vector3::Transform(math::Vector3(0.0f, 0.0f, -1.0f), quat),
							math::Vector3::Transform(math::Vector3(0.0f, 1.0f, 0.0f), quat));
						applied["viaCamera"] = true;
					}
					else
					{
						e->SetRotation(quat);
					}
					applied["rotation"] = p["rotation"];
				}
				else if (ReadFloats(p, "eulerDegrees", 3, eul))
				{
					if (camera != nullptr)
					{
						// Degrees, the camera's own convention: lookDir =
						// (-cos p sin y, sin p, -cos p cos y) - pitch + looks UP,
						// yaw 0 faces -Z, yaw + turns toward -X. Pitch is clamped
						// to the camera's limits (+/-89 by default).
						camera->SetPitch(eul[0]);
						camera->SetYaw(eul[1]);
						camera->SetRoll(eul[2]);
						applied["viaCamera"] = true;
						applied["convention"] = "degrees; pitch + = up; yaw 0 = -Z, yaw + turns toward -X";
					}
					else
					{
						const float toRad = 3.14159265358979f / 180.0f;
						e->SetRotation(math::Quaternion::CreateFromYawPitchRoll(eul[1] * toRad, eul[0] * toRad, eul[2] * toRad));
					}
					applied["eulerDegrees"] = p["eulerDegrees"];
				}
				if (ReadVec3(p, "scale", v))
				{
					e->SetScale(v);
					applied["scale"] = p["scale"];
				}
				if (applied.empty())
					return MakeError(id, ErrorCode::InvalidParams, "provide at least one of: position [x,y,z], rotation [x,y,z,w], eulerDegrees [pitch,yaw,roll], scale [x,y,z]");
				MarkPvsDirty();
				return MakeResult(id, json{ {"entity", e->GetName()}, {"id", EntityIdToJson(e->GetId())}, {"applied", applied} });
			});

			Reg("set_component_field", W, Main, Batch, [this](int64_t id, const json& p) -> json {
				const std::string compName = RequireString(p, "component");
				if (!p.contains("fields") || !p["fields"].is_object())
					return MakeError(id, ErrorCode::InvalidParams, "set_component_field requires 'fields' (object of field:value; names from inspect_component)");
				auto scene = PrimaryUserScene();
				if (!scene) return NoScene(id);
				json err;
				Entity* e = RequireEntity(id, scene.get(), p, "entity", true, err);
				if (!e) return err;
				BaseComponent* target = FindComponent(e, compName);
				if (target == nullptr)
					return MakeError(id, ErrorCode::NotAvailable, "entity '" + e->GetName() + "' has no component named '" + compName + "' (it has: " + ComponentNames(e).dump() + ")");
				json after;
				std::string msg;
				if (!ApplyComponentFields(target, p["fields"], after, msg))
					return MakeError(id, ErrorCode::InvalidParams, msg);
				MarkPvsDirty();
				return MakeResult(id, json{
					{"entity", e->GetName()},
					{"id", EntityIdToJson(e->GetId())},
					{"component", compName},
					{"fields", after},
				});
			});

			Reg("delete_entity", W, Main, Batch, [this](int64_t id, const json& p) -> json {
				auto scene = PrimaryUserScene();
				if (!scene) return NoScene(id);
				json err;
				Entity* e = RequireEntity(id, scene.get(), p, "entity", true, err);
				if (!e) return err;
				const json brief = EntityBrief(e);
				const bool selectionCleared = ReleaseSelectionOf(e);
				scene->DestroyEntity(e);
				MarkPvsDirty();
				return MakeResult(id, json{ {"deleted", brief["name"]}, {"id", brief["id"]}, {"selectionCleared", selectionCleared},
					{"note", "removal may defer one tick if scene iteration is in progress"} });
			});

			Reg("create_mesh_entity", W, Main, Batch, [this](int64_t id, const json& p) -> json {
				auto scene = PrimaryUserScene();
				if (!scene) return NoScene(id);
				const std::string name = RequireString(p, "name");

				// --- geometry parsing (nested [[x,y,z],..] or flat [x,y,z,..]) ---
				auto parseVec3s = [](const json& arr, std::vector<math::Vector3>& out) -> bool
				{
					if (!arr.is_array()) return false;
					if (!arr.empty() && arr[0].is_array())
					{
						out.reserve(arr.size());
						for (const auto& v : arr)
						{
							if (!v.is_array() || v.size() != 3) return false;
							out.emplace_back(v[0].get<float>(), v[1].get<float>(), v[2].get<float>());
						}
						return true;
					}
					if (arr.size() % 3 != 0) return false;
					out.reserve(arr.size() / 3);
					for (size_t i = 0; i + 2 < arr.size(); i += 3)
						out.emplace_back(arr[i].get<float>(), arr[i + 1].get<float>(), arr[i + 2].get<float>());
					return true;
				};

				std::vector<math::Vector3> positions;
				if (!p.contains("vertices") || !parseVec3s(p["vertices"], positions) || positions.empty())
					return MakeError(id, ErrorCode::InvalidParams, "create_mesh_entity requires 'vertices' as [[x,y,z],...] or a flat [x,y,z,...] array");
				if (positions.size() > 262144)
					return MakeError(id, ErrorCode::InvalidParams, "too many vertices (max 262144)");

				std::vector<MeshIndexFormat> indices;
				if (!p.contains("indices") || !p["indices"].is_array() || p["indices"].empty() || p["indices"].size() % 3 != 0)
					return MakeError(id, ErrorCode::InvalidParams, "create_mesh_entity requires 'indices' as a flat triangle-list array (multiple of 3)");
				if (p["indices"].size() > 1048576)
					return MakeError(id, ErrorCode::InvalidParams, "too many indices (max 1048576)");
				indices.reserve(p["indices"].size());
				for (const auto& iv : p["indices"])
				{
					const uint64_t v = iv.get<uint64_t>();
					if (v >= positions.size())
						return MakeError(id, ErrorCode::InvalidParams, "index " + std::to_string(v) + " out of range (vertex count " + std::to_string(positions.size()) + ")");
					indices.push_back((MeshIndexFormat)v);
				}

				std::vector<math::Vector3> normals;
				if (p.contains("normals"))
				{
					if (!parseVec3s(p["normals"], normals) || normals.size() != positions.size())
						return MakeError(id, ErrorCode::InvalidParams, "'normals' must match the vertex count");
				}
				else
				{
					// Area-weighted face-normal accumulation - the cross product
					// magnitude is twice the triangle area, so summing
					// unnormalised face normals weights by area.
					normals.assign(positions.size(), math::Vector3(0.0f, 0.0f, 0.0f));
					for (size_t i = 0; i + 2 < indices.size(); i += 3)
					{
						const math::Vector3& a = positions[indices[i]];
						const math::Vector3& b = positions[indices[i + 1]];
						const math::Vector3& c = positions[indices[i + 2]];
						const math::Vector3 fn = (b - a).Cross(c - a);
						normals[indices[i]]     += fn;
						normals[indices[i + 1]] += fn;
						normals[indices[i + 2]] += fn;
					}
					for (auto& n : normals)
					{
						const float len = n.Length();
						n = (len > 0.00001f) ? (n / len) : math::Vector3(0.0f, 1.0f, 0.0f);
					}
				}

				std::vector<math::Vector2> uvs;
				if (p.contains("uvs"))
				{
					const json& arr = p["uvs"];
					if (!arr.is_array())
						return MakeError(id, ErrorCode::InvalidParams, "'uvs' must be [[u,v],...] or flat [u,v,...]");
					if (!arr.empty() && arr[0].is_array())
					{
						for (const auto& v : arr)
						{
							if (!v.is_array() || v.size() != 2)
								return MakeError(id, ErrorCode::InvalidParams, "'uvs' entries must be [u,v]");
							uvs.emplace_back(v[0].get<float>(), v[1].get<float>());
						}
					}
					else
					{
						if (arr.size() % 2 != 0)
							return MakeError(id, ErrorCode::InvalidParams, "flat 'uvs' must have an even element count");
						for (size_t i = 0; i + 1 < arr.size(); i += 2)
							uvs.emplace_back(arr[i].get<float>(), arr[i + 1].get<float>());
					}
					if (uvs.size() != positions.size())
						return MakeError(id, ErrorCode::InvalidParams, "'uvs' must match the vertex count");
				}

				Entity* parent = nullptr;
				if (p.contains("parent") && !p["parent"].is_null())
				{
					json err;
					parent = RequireEntity(id, scene.get(), p, "parent", false, err);
					if (!parent) return err;
				}

				// --- assemble MeshVertex array (tangent = any axis orthogonal to
				// the normal; Default.shader only needs it non-degenerate when no
				// normal map is bound) ---
				std::vector<MeshVertex> verts(positions.size());
				math::Vector3 mn = positions[0];
				math::Vector3 mx = positions[0];
				for (size_t i = 0; i < positions.size(); ++i)
				{
					const math::Vector3& n = normals[i];
					math::Vector3 axis = (fabsf(n.y) < 0.99f) ? math::Vector3(0.0f, 1.0f, 0.0f) : math::Vector3(1.0f, 0.0f, 0.0f);
					math::Vector3 tangent = axis.Cross(n);
					const float tl = tangent.Length();
					tangent = (tl > 0.00001f) ? (tangent / tl) : math::Vector3(1.0f, 0.0f, 0.0f);
					verts[i] = MeshVertex::Create(positions[i], n, tangent,
						uvs.empty() ? 0.0f : uvs[i].x, uvs.empty() ? 0.0f : uvs[i].y);
					verts[i]._bitangent = n.Cross(tangent);
					mn = math::Vector3::Min(mn, positions[i]);
					mx = math::Vector3::Max(mx, positions[i]);
				}

				// --- material: existing .hmat, or a Default.hmat copy; property
				// overrides always apply to a private copy so a cached shared
				// material is never mutated ---
				std::shared_ptr<Material> material = std::make_shared<Material>();
				material->SetLoader(g_pEnv->GetResourceSystem().FindResourceLoaderForExtension(".hmat"));
				const json matParams = p.value("material", json::object());
				std::shared_ptr<Material> base;
				if (matParams.contains("path") && matParams["path"].is_string())
				{
					base = Material::Create(matParams["path"].get<std::string>());
					if (base == nullptr)
						return MakeError(id, ErrorCode::NotAvailable, "material '" + matParams["path"].get<std::string>() + "' could not be loaded");
				}
				else
				{
					base = Material::Create("EngineData.Materials/Default.hmat");
				}
				if (base != nullptr)
					material->CopyFrom(base);
				auto readColour = [&](const char* key, math::Vector4& out)
				{
					if (matParams.contains(key) && matParams[key].is_array() && matParams[key].size() >= 3)
					{
						out = math::Vector4(
							matParams[key][0].get<float>(), matParams[key][1].get<float>(), matParams[key][2].get<float>(),
							matParams[key].size() >= 4 ? matParams[key][3].get<float>() : 1.0f);
					}
				};
				readColour("diffuseColour", material->_properties.diffuseColour);
				readColour("emissiveColour", material->_properties.emissiveColour);
				if (matParams.contains("metallic"))   material->_properties.metallicFactor  = matParams["metallic"].get<float>();
				if (matParams.contains("roughness"))  material->_properties.roughnessFactor = matParams["roughness"].get<float>();
				if (matParams.contains("smoothness")) material->_properties.smoothness      = matParams["smoothness"].get<float>();
				if (matParams.contains("affectsGI"))  material->SetAffectsGI(matParams["affectsGI"].get<bool>());

				// --- mesh + entity ---
				auto mesh = std::make_shared<Mesh>(nullptr, name);
				if (!mesh->CreateDynamicBuffers((uint32_t)verts.size(), (uint32_t)indices.size()) ||
					!mesh->UpdateDynamicGeometry(verts, indices))
					return MakeError(id, ErrorCode::Internal, "mesh buffer creation/upload failed");

				dx::BoundingBox aabb;
				dx::BoundingBox::CreateFromPoints(aabb,
					DirectX::XMVectorSet(mn.x, mn.y, mn.z, 1.0f),
					DirectX::XMVectorSet(mx.x, mx.y, mx.z, 1.0f));
				dx::BoundingOrientedBox obb;
				dx::BoundingOrientedBox::CreateFromBoundingBox(obb, aabb);
				mesh->SetAABB(aabb);
				mesh->SetOBB(obb);

				math::Vector3 pos(0.0f, 0.0f, 0.0f);
				ReadVec3(p, "position", pos);
				math::Quaternion rot(0.0f, 0.0f, 0.0f, 1.0f);
				ReadRotation(p, rot);
				math::Vector3 scale(1.0f, 1.0f, 1.0f);
				ReadVec3(p, "scale", scale);

				Entity* e = scene->CreateEntity(name, pos, rot, scale);
				if (e == nullptr)
					return MakeError(id, ErrorCode::Internal, "Scene::CreateEntity returned null");
				// The mesh has no backing asset file, so a scene save would
				// serialise an empty mesh reference. Default the entity to
				// DoNotSave; pass persistent=true to opt into saving anyway.
				if (!p.value("persistent", false))
					e->SetFlag(EntityFlags::DoNotSave);
				if (parent)
					e->SetParent(parent);

				auto* meshComponent = e->AddComponent<StaticMeshComponent>();
				if (meshComponent == nullptr)
					return MakeError(id, ErrorCode::Internal, "failed to add StaticMeshComponent");
				meshComponent->SetMesh(mesh);
				meshComponent->SetMaterial(material);
				MarkPvsDirty();

				json result = EntityBrief(e);
				result["vertexCount"] = verts.size();
				result["indexCount"] = indices.size();
				result["triangleCount"] = indices.size() / 3;
				result["aabbMin"] = json::array({ mn.x, mn.y, mn.z });
				result["aabbMax"] = json::array({ mx.x, mx.y, mx.z });
				result["note"] = "mesh is in-memory only (no .hmesh asset); entity defaults to DoNotSave unless persistent=true";
				return MakeResult(id, result);
			});

			// Pipe-thread handler: waits `waitFrames` editor frames (default 2) so
			// mutations made just before - including the coalesced PVS rebuild -
			// are actually rendered, THEN marshals the capture. Not batchable (a
			// batch runs inside one frame, so it could never see its own edits).
			Reg("capture_frame", W, Pipe, Solo, [this](int64_t id, const json& p) -> json {
				const std::string path = RequireString(p, "path");
				int waitFrames = p.value("waitFrames", 2);
				waitFrames = std::clamp(waitFrames, 0, 600);
				const uint64_t target = _frameCount.load() + (uint64_t)waitFrames;
				const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kPendingTimeoutMs);
				while (_frameCount.load() < target)
				{
					if (std::chrono::steady_clock::now() >= deadline)
						return MakeError(id, ErrorCode::NotAvailable, "editor did not render " + std::to_string(waitFrames) + " frames in time; nothing was captured");
					std::this_thread::sleep_for(std::chrono::milliseconds(2));
				}
				return OnMain(id, [this, id, path]() -> json {
					// The main scene camera's render target - a resolved, non-MSAA
					// texture SaveToFile can grab (the swapchain back buffer is
					// MSAA and fails DirectX::CaptureTexture with E_INVALIDARG).
					// Always the USER scene (never the icon-preview utility scene).
					auto scene = PrimaryUserScene();
					Camera* cam = scene ? scene->GetMainCamera() : nullptr;
					ITexture2D* tex = cam ? cam->GetRenderTarget() : nullptr;
					if (!tex)
						return MakeError(id, ErrorCode::NotAvailable, "no main-camera render target");
					try { tex->SaveToFile(fs::path(path)); }
					catch (const std::exception& e) { return MakeError(id, ErrorCode::Internal, std::string("SaveToFile threw: ") + e.what()); }
					return MakeResult(id, json{ {"path", path}, {"frame", _frameCount.load()} });
				});
			});
		}

	private:
		struct LogEntry { uint64_t seq; std::string text; LogLevel level; };

		BridgeServer    _server;
		MainThreadQueue _mainThread;
		fs::path        _sessionFile;
		std::string     _token;
		bool            _started = false;
		bool            _pvsDirty = false; // main thread only
		std::atomic<uint64_t> _frameCount{ 0 };

		std::unordered_map<std::string, MethodInfo> _methods;

		static constexpr size_t kMaxLogRing = 2000;
		std::mutex              _logMutex;
		std::deque<LogEntry>    _logRing;
		uint64_t                _logSeq = 0;
	};

	static EditorBridgePlugin* g_pEditorBridgePlugin = nullptr;
	CREATE_PLUGIN(g_pEditorBridgePlugin, EditorBridgePlugin);
}
