
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
#include "../../HexEngine.Core/Input/HVar.hpp"
#include "../../HexEngine.Core/Input/HCommand.hpp"
#include "../../HexEngine.Core/Environment/ComponentFieldMeta.hpp"
#include "../../HexEngine.Core/Entity/Component/Camera.hpp"
#include "../../HexEngine.Core/Entity/Component/StaticMeshComponent.hpp"
#include "../../HexEngine.Core/Graphics/ITexture2D.hpp"
#include "../../HexEngine.Core/Graphics/Material.hpp"
#include "../../HexEngine.Core/Scene/Mesh.hpp"
#include "../../HexEngine.Core/FileSystem/PrefabLoader.hpp"

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
#include <unordered_set>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>

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

		// ---- Field typing: shape checks + enum names ----

		// Enum fields accept their value NAMES ("Dynamic"), flag fields a name or
		// an array of names (OR-ed). Numbers pass through. Uses the Core
		// ComponentFieldMeta registry; unknown names are an error.
		bool TranslateEnumValue(const std::string& component, const std::string& field, json& value, std::string& msg)
		{
			const ComponentFieldInfo* info = ComponentFieldMeta::Find(component, field);
			if (info == nullptr || info->enumValues.empty())
				return true;
			auto lookup = [&](const std::string& name, int64_t& out) {
				for (const auto& [n, v] : info->enumValues)
					if (_stricmp(n.c_str(), name.c_str()) == 0) { out = v; return true; }
				return false;
			};
			auto names = [&]() {
				std::string s;
				for (const auto& [n, v] : info->enumValues) s += (s.empty() ? "" : ", ") + n;
				return s;
			};
			if (value.is_string())
			{
				int64_t v;
				if (!lookup(value.get<std::string>(), v))
				{
					msg = "'" + field + "': unknown value '" + value.get<std::string>() + "' (valid: " + names() + ")";
					return false;
				}
				value = v;
			}
			else if (value.is_array() && info->isFlags)
			{
				int64_t bits = 0;
				for (const auto& n : value)
				{
					int64_t v;
					if (!n.is_string() || !lookup(n.get<std::string>(), v))
					{
						msg = "'" + field + "': unknown flag " + n.dump() + " (valid: " + names() + ")";
						return false;
					}
					bits |= v;
				}
				value = bits;
			}
			return true;
		}

		// Per-field metadata for the fields of `component` that have any.
		json FieldInfoJson(const std::string& component, const json* fields)
		{
			json out = json::object();
			for (const auto& [field, info] : ComponentFieldMeta::ForComponent(component))
			{
				if (fields && !fields->contains(field)) continue;
				json j = json::object();
				if (!info.description.empty()) j["description"] = info.description;
				if (!info.units.empty()) j["units"] = info.units;
				if (!info.enumValues.empty())
				{
					json e = json::object();
					for (const auto& [n, v] : info.enumValues) e[n] = v;
					j[info.isFlags ? "flags" : "enum"] = e;
				}
				if (info.hasRange) j["range"] = json::array({ info.minValue, info.maxValue });
				out[field] = j;
			}
			return out;
		}

		// Read-modify-write through the component's own Serialize/Deserialize:
		// components' Deserialize expects the full field set (missing keys can
		// reset to defaults), so capture current state, overlay the patch, and
		// feed the merged object back. Object-valued fields merge recursively
		// (JSON merge-patch); scalars/arrays replace. Unknown keys, wrong shapes
		// and unknown enum names are all rejected BEFORE anything is applied, so
		// a bad patch never half-applies.
		bool ApplyComponentFields(BaseComponent* c, const json& fields, json& after, std::string& msg)
		{
			if (!fields.is_object())
			{
				msg = "'fields' must be an object of field:value";
				return false;
			}
			const std::string compName = c->GetComponentName() ? c->GetComponentName() : "";
			MemoryJsonFile mem;
			json current = json::object();
			c->Serialize(current, &mem);
			json patch = fields;
			for (auto& [key, value] : patch.items())
			{
				if (!current.contains(key))
				{
					msg = "component '" + compName + "' has no serialised field '" + key + "' (see inspect_component / describe_component_type)";
					return false;
				}
				if (!TranslateEnumValue(compName, key, value, msg))
					return false;
				if (!CheckFieldShape(current[key], value, key, msg))
					return false;
			}
			for (auto& [key, value] : patch.items())
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

		// ---- Console variables (HVar) ----

		const char* HVarTypeName(HVar::Type t)
		{
			switch (t)
			{
			case HVar::Type::Bool:    return "bool";
			case HVar::Type::Int8:    return "int8";
			case HVar::Type::UInt8:   return "uint8";
			case HVar::Type::Int16:   return "int16";
			case HVar::Type::UInt16:  return "uint16";
			case HVar::Type::Int32:   return "int32";
			case HVar::Type::UInt32:  return "uint32";
			case HVar::Type::Int64:   return "int64";
			case HVar::Type::UInt64:  return "uint64";
			case HVar::Type::Float32: return "float";
			case HVar::Type::Float64: return "double";
			case HVar::Type::Vector3: return "vec3";
			default:                  return "unknown";
			}
		}

		json HVarValueJson(HVar::Type t, const HVar::Val& v)
		{
			switch (t)
			{
			case HVar::Type::Bool:    return v.b;
			case HVar::Type::Int8:    return v.i8;
			case HVar::Type::UInt8:   return v.ui8;
			case HVar::Type::Int16:   return v.i16;
			case HVar::Type::UInt16:  return v.ui16;
			case HVar::Type::Int32:   return v.i32;
			case HVar::Type::UInt32:  return v.ui32;
			case HVar::Type::Int64:   return v.i64;
			case HVar::Type::UInt64:  return v.ui64;
			case HVar::Type::Float32: return v.f32;
			case HVar::Type::Float64: return v.f64;
			case HVar::Type::Vector3: return json::array({ v.v3.x, v.v3.y, v.v3.z });
			default:                  return nullptr;
			}
		}

		json HVarJson(HVar* v, bool full)
		{
			const HVar::Type t = v->GetType();
			json j = json{ {"name", v->_name}, {"type", HVarTypeName(t)}, {"value", HVarValueJson(t, v->_val)} };
			if (full)
			{
				j["description"] = v->_description;
				if (t != HVar::Type::Bool)
				{
					j["min"] = HVarValueJson(t, v->_min);
					j["max"] = HVarValueJson(t, v->_max);
				}
			}
			return j;
		}

		// Typed assignment from JSON (no string parsing, so bad input can't
		// throw out of std::stoi the way the console path can), then the var's
		// own Clamp(). Main thread only.
		bool SetHVarFromJson(HVar* v, const json& value, std::string& msg, bool clamp = true)
		{
			auto setInt = [&](auto& field) -> bool {
				using T = std::decay_t<decltype(field)>;
				if (!value.is_number() || value.is_boolean())
				{
					msg = "'" + v->_name + "' is " + HVarTypeName(v->GetType()) + "; expected an integer";
					return false;
				}
				const double d = value.get<double>();
				if (std::floor(d) != d)
				{
					msg = "'" + v->_name + "' is " + HVarTypeName(v->GetType()) + "; " + value.dump() + " is not an integer";
					return false;
				}
				if (d < (double)std::numeric_limits<T>::lowest() || d > (double)std::numeric_limits<T>::max())
				{
					msg = "'" + v->_name + "': " + value.dump() + " is outside the " + HVarTypeName(v->GetType()) + " range";
					return false;
				}
				field = value.is_number_unsigned() ? (T)value.get<uint64_t>() : (T)value.get<int64_t>();
				if (!value.is_number_integer()) field = (T)d;
				return true;
			};
			switch (v->GetType())
			{
			case HVar::Type::Bool:
				if (value.is_boolean()) v->_val.b = value.get<bool>();
				else if (value.is_number_integer() && (value.get<int64_t>() == 0 || value.get<int64_t>() == 1)) v->_val.b = value.get<int64_t>() == 1;
				else { msg = "'" + v->_name + "' is bool; expected true/false"; return false; }
				break;
			case HVar::Type::Int8:   if (!setInt(v->_val.i8)) return false; break;
			case HVar::Type::UInt8:  if (!setInt(v->_val.ui8)) return false; break;
			case HVar::Type::Int16:  if (!setInt(v->_val.i16)) return false; break;
			case HVar::Type::UInt16: if (!setInt(v->_val.ui16)) return false; break;
			case HVar::Type::Int32:  if (!setInt(v->_val.i32)) return false; break;
			case HVar::Type::UInt32: if (!setInt(v->_val.ui32)) return false; break;
			case HVar::Type::Int64:  if (!setInt(v->_val.i64)) return false; break;
			case HVar::Type::UInt64: if (!setInt(v->_val.ui64)) return false; break;
			case HVar::Type::Float32:
				if (!value.is_number() || value.is_boolean()) { msg = "'" + v->_name + "' is float; expected a number"; return false; }
				v->_val.f32 = value.get<float>();
				break;
			case HVar::Type::Float64:
				if (!value.is_number() || value.is_boolean()) { msg = "'" + v->_name + "' is double; expected a number"; return false; }
				v->_val.f64 = value.get<double>();
				break;
			case HVar::Type::Vector3:
				if (!IsNumericVector(value) || value.size() != 3) { msg = "'" + v->_name + "' is vec3; expected [x, y, z]"; return false; }
				v->_val.v3 = math::Vector3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
				break;
			default:
				msg = "'" + v->_name + "' has an unsupported type";
				return false;
			}
			if (clamp)
				v->Clamp();
			return true;
		}

		IEditorContext* Ctx()
		{
			return g_pEnv ? g_pEnv->_editorContext : nullptr;
		}

		// Project-relative asset paths. Accepts an absolute path, "Data/...",
		// or a path relative to the project's Data folder ("Prefabs/Tree.hprefab").
		// Results must stay inside the open project (no writing elsewhere).
		bool ResolveProjectPath(const std::string& input, fs::path& out, std::string& error)
		{
			IEditorContext* ctx = Ctx();
			const std::string folder = ctx ? ctx->GetProjectFolderPath() : std::string();
			if (folder.empty())
			{
				error = "no project is open";
				return false;
			}
			const fs::path root = fs::weakly_canonical(fs::path(folder));
			fs::path p = fs::path(input);
			if (!p.is_absolute())
			{
				const auto first = p.begin() != p.end() ? p.begin()->wstring() : std::wstring();
				p = (_wcsicmp(first.c_str(), L"Data") == 0) ? root / p : root / L"Data" / p;
			}
			p = fs::weakly_canonical(p);
			const std::wstring r = root.wstring(), c = p.wstring();
			if (c.size() < r.size() || _wcsnicmp(c.c_str(), r.c_str(), r.size()) != 0)
			{
				error = "path '" + input + "' resolves outside the open project (" + root.string() + ")";
				return false;
			}
			out = p;
			return true;
		}

		// Project-relative form for responses ("Data/Prefabs/Tree.hprefab").
		std::string ProjectRelative(const fs::path& p)
		{
			IEditorContext* ctx = Ctx();
			const std::string folder = ctx ? ctx->GetProjectFolderPath() : std::string();
			if (folder.empty()) return p.string();
			std::error_code ec;
			const fs::path rel = fs::relative(p, fs::path(folder), ec);
			return ec || rel.empty() ? p.string() : rel.generic_string();
		}

		// Spawned prefab entities whose parent is not part of the same spawn.
		std::vector<Entity*> SpawnRoots(const std::vector<Entity*>& spawned)
		{
			std::unordered_set<Entity*> set(spawned.begin(), spawned.end());
			std::vector<Entity*> roots;
			for (Entity* e : spawned)
				if (e != nullptr && (e->GetParent() == nullptr || set.count(e->GetParent()) == 0))
					roots.push_back(e);
			return roots;
		}

		std::wstring Utf8ToWide(const std::string& s)
		{
			if (s.empty()) return std::wstring();
			int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
			std::wstring w(n, L'\0');
			MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
			return w;
		}

		// ---- Procedural geometry (create_primitive / create_mesh_entity) ----

		struct Geometry
		{
			std::vector<math::Vector3> positions;
			std::vector<math::Vector3> normals;   // empty = derive smooth normals
			std::vector<math::Vector2> uvs;       // empty = no UVs
			std::vector<MeshIndexFormat> indices;

			MeshIndexFormat Add(const math::Vector3& p, const math::Vector3& n, float u, float v)
			{
				positions.push_back(p);
				normals.push_back(n);
				uvs.emplace_back(u, v);
				return (MeshIndexFormat)(positions.size() - 1);
			}
			void Tri(MeshIndexFormat a, MeshIndexFormat b, MeshIndexFormat c)
			{
				indices.push_back(a); indices.push_back(b); indices.push_back(c);
			}
			// Quad a-b-c-d around its perimeter.
			void Quad(MeshIndexFormat a, MeshIndexFormat b, MeshIndexFormat c, MeshIndexFormat d)
			{
				Tri(a, b, c); Tri(a, c, d);
			}
		};

		constexpr float kPi = 3.14159265358979f;

		// Engine convention (measured on imported .hmesh assets): a triangle's
		// front face is the side (b-a)x(c-a) points to, and vertex normals point
		// out of the front. Re-wind any triangle that disagrees with its vertex
		// normals, so generators only have to get positions and normals right.
		void OrientTriangles(Geometry& g)
		{
			if (g.normals.size() != g.positions.size()) return;
			for (size_t i = 0; i + 2 < g.indices.size(); i += 3)
			{
				const auto a = g.indices[i], b = g.indices[i + 1], c = g.indices[i + 2];
				const math::Vector3 fn = (g.positions[b] - g.positions[a]).Cross(g.positions[c] - g.positions[a]);
				const math::Vector3 vn = g.normals[a] + g.normals[b] + g.normals[c];
				if (fn.Dot(vn) < 0.0f)
					std::swap(g.indices[i + 1], g.indices[i + 2]);
			}
		}

		float PositiveParam(const json& p, const char* key, float def)
		{
			if (!p.contains(key) || p[key].is_null()) return def;
			if (!p[key].is_number()) throw ParamError(std::string("'") + key + "' must be a number");
			const float v = p[key].get<float>();
			if (!(v > 0.0f) || !std::isfinite(v)) throw ParamError(std::string("'") + key + "' must be > 0");
			return v;
		}

		int CountParam(const json& p, const char* key, int def, int lo, int hi)
		{
			if (!p.contains(key) || p[key].is_null()) return def;
			if (!p[key].is_number_integer()) throw ParamError(std::string("'") + key + "' must be an integer");
			return std::clamp(p[key].get<int>(), lo, hi);
		}

		// Ring of (segments+1) vertices (seam duplicated for UVs) around Y.
		// Angle runs so that, seen from outside, u increases left to right.
		math::Vector3 RingDir(int s, int segments)
		{
			const float t = 2.0f * kPi * (float)s / (float)segments;
			return math::Vector3(cosf(t), 0.0f, -sinf(t));
		}

		// Shapes are centred on the origin (plane: at y = 0, facing +Y).
		// Returns the shape's parameters as used, for the response.
		json GeneratePrimitive(const std::string& shape, const json& p, Geometry& g)
		{
			json used = json::object();
			if (shape == "box" || shape == "cube")
			{
				math::Vector3 size(1.0f, 1.0f, 1.0f);
				if (p.contains("size") && p["size"].is_number())
					size = math::Vector3(p["size"].get<float>());
				else
					ReadVec3(p, "size", size);
				if (!(size.x > 0 && size.y > 0 && size.z > 0)) throw ParamError("'size' components must be > 0");
				const math::Vector3 h = size * 0.5f;
				// (normal, u axis, v axis) with u x v = normal.
				const math::Vector3 faces[6][3] = {
					{ { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } },
					{ { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },
					{ { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },
					{ { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
					{ { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },
					{ { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 } },
				};
				for (const auto& f : faces)
				{
					MeshIndexFormat q[4];
					const float su[4] = { -1, 1, 1, -1 }, sv[4] = { -1, -1, 1, 1 };
					for (int k = 0; k < 4; ++k)
					{
						const math::Vector3 unit = f[0] + f[1] * su[k] + f[2] * sv[k];
						q[k] = g.Add(unit * h, f[0], (su[k] + 1.0f) * 0.5f, 1.0f - (sv[k] + 1.0f) * 0.5f);
					}
					g.Quad(q[0], q[1], q[2], q[3]);
				}
				used["size"] = json::array({ size.x, size.y, size.z });
			}
			else if (shape == "plane" || shape == "quad")
			{
				float sz[2] = { 1.0f, 1.0f };
				if (p.contains("size") && p["size"].is_number())
					sz[0] = sz[1] = p["size"].get<float>();
				else
					ReadFloats(p, "size", 2, sz);
				if (!(sz[0] > 0 && sz[1] > 0)) throw ParamError("'size' must be > 0");
				const int n = CountParam(p, "subdivisions", 1, 1, 512);
				for (int j = 0; j <= n; ++j)
					for (int i = 0; i <= n; ++i)
					{
						const float u = (float)i / n, t = (float)j / n;
						g.Add(math::Vector3(-sz[0] * 0.5f + sz[0] * u, 0.0f, sz[1] * 0.5f - sz[1] * t), math::Vector3(0, 1, 0), u, 1.0f - t);
					}
				for (int j = 0; j < n; ++j)
					for (int i = 0; i < n; ++i)
					{
						const MeshIndexFormat a = (MeshIndexFormat)(j * (n + 1) + i);
						g.Quad(a, a + 1, a + 1 + (n + 1), a + (n + 1));
					}
				used["size"] = json::array({ sz[0], sz[1] });
				used["subdivisions"] = n;
			}
			else if (shape == "sphere" || shape == "capsule")
			{
				const float r = PositiveParam(p, "radius", 0.5f);
				const int segs = CountParam(p, "segments", 32, 3, 512);
				int rings = CountParam(p, "rings", 16, 2, 512);
				float cyl = 0.0f;
				if (shape == "capsule")
				{
					const float height = PositiveParam(p, "height", 2.0f);
					if (height < 2.0f * r) throw ParamError("capsule 'height' (total, caps included) must be >= 2 * radius");
					cyl = height - 2.0f * r;
					if (rings % 2) ++rings;
					used["height"] = height;
				}
				// Ring list: polar angle + vertical offset. A capsule duplicates
				// the equator ring (top/bottom offsets) to form its cylinder.
				std::vector<std::pair<float, float>> ringDefs;
				for (int k = 0; k <= rings; ++k)
				{
					const float phi = kPi * (float)k / (float)rings;
					if (cyl > 0.0f && k == rings / 2)
					{
						ringDefs.push_back({ phi, cyl * 0.5f });
						ringDefs.push_back({ phi, -cyl * 0.5f });
					}
					else
						ringDefs.push_back({ phi, cyl > 0.0f ? (k < rings / 2 ? cyl * 0.5f : -cyl * 0.5f) : 0.0f });
				}
				const int rowCount = (int)ringDefs.size();
				for (int k = 0; k < rowCount; ++k)
				{
					const float phi = ringDefs[k].first, yoff = ringDefs[k].second;
					for (int s = 0; s <= segs; ++s)
					{
						const math::Vector3 d = RingDir(s, segs);
						const math::Vector3 n(d.x * sinf(phi), cosf(phi), d.z * sinf(phi));
						g.Add(n * r + math::Vector3(0.0f, yoff, 0.0f), n, (float)s / segs, (float)k / (rowCount - 1));
					}
				}
				for (int k = 0; k + 1 < rowCount; ++k)
					for (int s = 0; s < segs; ++s)
					{
						const MeshIndexFormat a = (MeshIndexFormat)(k * (segs + 1) + s);
						const MeshIndexFormat b = a + (MeshIndexFormat)(segs + 1);
						if (k > 0) g.Tri(a, a + 1, b);                 // skip zero-area pole tris
						if (k + 2 < rowCount) g.Tri(a + 1, b + 1, b);
					}
				used["radius"] = r; used["segments"] = segs; used["rings"] = rings;
			}
			else if (shape == "cylinder" || shape == "cone")
			{
				const bool cone = shape == "cone";
				const float r = PositiveParam(p, "radius", 0.5f);
				const float h = PositiveParam(p, "height", 1.0f);
				const int segs = CountParam(p, "segments", 32, 3, 512);
				const bool caps = p.value("caps", true);
				const float y0 = -h * 0.5f, y1 = h * 0.5f;
				// Side. Cone normals tilt up by the slope; its apex is one vertex
				// per segment (averaged normal) so shading stays smooth.
				const float slope = cone ? r / h : 0.0f;
				for (int s = 0; s <= segs; ++s)
				{
					const math::Vector3 d = RingDir(s, segs);
					math::Vector3 n(d.x, slope, d.z); n.Normalize();
					g.Add(d * r + math::Vector3(0, y0, 0), n, (float)s / segs, 1.0f);
					if (cone)
					{
						const math::Vector3 dm = RingDir(s, segs) * 0.5f + RingDir(s + 1, segs) * 0.5f;
						math::Vector3 nm(dm.x, slope, dm.z); nm.Normalize();
						g.Add(math::Vector3(0, y1, 0), nm, ((float)s + 0.5f) / segs, 0.0f);
					}
					else
						g.Add(d * r + math::Vector3(0, y1, 0), n, (float)s / segs, 0.0f);
				}
				for (int s = 0; s < segs; ++s)
				{
					const MeshIndexFormat a = (MeshIndexFormat)(s * 2);
					if (cone) g.Tri(a, a + 2, a + 1);
					else g.Quad(a, a + 2, a + 3, a + 1);
				}
				auto cap = [&](float y, float ny, float capR)
				{
					const MeshIndexFormat c = g.Add(math::Vector3(0, y, 0), math::Vector3(0, ny, 0), 0.5f, 0.5f);
					for (int s = 0; s <= segs; ++s)
					{
						const math::Vector3 d = RingDir(s, segs);
						g.Add(d * capR + math::Vector3(0, y, 0), math::Vector3(0, ny, 0), 0.5f + d.x * 0.5f, 0.5f - d.z * ny * 0.5f);
					}
					for (int s = 0; s < segs; ++s)
						g.Tri(c, c + 1 + s, c + 2 + s);
				};
				if (caps)
				{
					cap(y0, -1.0f, r);
					if (!cone) cap(y1, 1.0f, r);
				}
				used["radius"] = r; used["height"] = h; used["segments"] = segs; used["caps"] = caps;
			}
			else if (shape == "torus")
			{
				const float R = PositiveParam(p, "radius", 0.5f);
				const float tr = PositiveParam(p, "tubeRadius", 0.15f);
				const int segs = CountParam(p, "segments", 48, 3, 512);
				const int sides = CountParam(p, "sides", 24, 3, 512);
				for (int s = 0; s <= segs; ++s)
				{
					const math::Vector3 d = RingDir(s, segs);
					for (int k = 0; k <= sides; ++k)
					{
						const float phi = 2.0f * kPi * (float)k / sides;
						const math::Vector3 n = d * cosf(phi) + math::Vector3(0, sinf(phi), 0);
						g.Add(d * R + n * tr, n, (float)s / segs, (float)k / sides);
					}
				}
				for (int s = 0; s < segs; ++s)
					for (int k = 0; k < sides; ++k)
					{
						const MeshIndexFormat a = (MeshIndexFormat)(s * (sides + 1) + k);
						const MeshIndexFormat b = a + (MeshIndexFormat)(sides + 1);
						g.Quad(a, b, b + 1, a + 1);
					}
				used["radius"] = R; used["tubeRadius"] = tr; used["segments"] = segs; used["sides"] = sides;
			}
			else
			{
				throw ParamError("unknown shape '" + shape + "' (box, plane, sphere, capsule, cylinder, cone, torus)");
			}

			// Optional UV tiling.
			float uvTile[2];
			if (ReadFloats(p, "uvTiling", 2, uvTile))
			{
				for (auto& uv : g.uvs) { uv.x *= uvTile[0]; uv.y *= uvTile[1]; }
				used["uvTiling"] = json::array({ uvTile[0], uvTile[1] });
			}
			OrientTriangles(g);
			return used;
		}

		// ---- Asset references ----
		//
		// Assets are referenced the way scenes store them: "<FileSystem>.<path
		// under its Data folder>" ("GameData.Materials/Rock.hmat",
		// "EngineData.Materials/Default.hmat"). Tools also accept a project path
		// (absolute, "Data/..." or Data-relative). Writes are only allowed inside
		// the open project's Data folder.
		struct AssetRef
		{
			fs::path absolute;
			std::string ref;     // engine resource path
			bool inProject = false;
		};

		bool ResolveAssetRef(const std::string& input, const char* requiredExt, bool forWrite, AssetRef& out, std::string& error)
		{
			IEditorContext* ctx = Ctx();
			FileSystem* projectFs = ctx ? ctx->GetProjectFileSystem() : nullptr;
			if (input.empty())
			{
				error = "empty asset path";
				return false;
			}

			// "<FileSystem>.<rest>" form?
			const size_t dot = input.find('.');
			const size_t slash = input.find_first_of("/\\:");
			if (dot != std::string::npos && dot > 0 && (slash == std::string::npos || dot < slash))
			{
				FileSystem* fsys = g_pEnv->GetResourceSystem().FindFileSystemByName(Utf8ToWide(input.substr(0, dot)));
				if (fsys != nullptr)
				{
					const fs::path rest = fs::path(Utf8ToWide(input.substr(dot + 1)));
					out.absolute = fs::weakly_canonical(fsys->GetLocalAbsoluteDataPath(rest));
					out.ref = WideToUtf8(fsys->GetName()) + "." + rest.generic_string();
					out.inProject = (fsys == projectFs);
				}
			}

			if (out.ref.empty())
			{
				if (projectFs == nullptr)
				{
					error = "no project is open";
					return false;
				}
				fs::path abs;
				if (!ResolveProjectPath(input, abs, error))
					return false;
				const fs::path dataDir = fs::weakly_canonical(projectFs->GetDataDirectory());
				const std::wstring d = dataDir.wstring(), a = abs.wstring();
				if (a.size() <= d.size() || _wcsnicmp(a.c_str(), d.c_str(), d.size()) != 0)
				{
					error = "'" + input + "' is not inside the project's Data folder (" + dataDir.string() + ")";
					return false;
				}
				out.absolute = abs;
				out.ref = WideToUtf8(projectFs->GetName()) + "." + fs::relative(abs, dataDir).generic_string();
				out.inProject = true;
			}

			if (requiredExt != nullptr && _wcsicmp(out.absolute.extension().c_str(), Utf8ToWide(requiredExt).c_str()) != 0)
			{
				if (!forWrite || out.absolute.has_extension())
				{
					error = "'" + input + "' must be a " + requiredExt + " file";
					return false;
				}
				out.absolute.replace_extension(requiredExt);
				out.ref += requiredExt;
			}
			if (forWrite && !out.inProject)
			{
				error = "'" + input + "' is outside the open project; only project assets can be written";
				return false;
			}
			return true;
		}

		// Loose file, or inside a mounted asset package (EngineAssets.pkg).
		bool AssetExists(const AssetRef& a)
		{
			std::error_code ec;
			return fs::exists(a.absolute, ec) || g_pEnv->GetResourceSystem().DoesResourceExistAsAsset(a.absolute);
		}

		json AssetRefJson(const AssetRef& a)
		{
			return json{ {"ref", a.ref}, {"path", ProjectRelative(a.absolute)} };
		}

		// ---- Material properties ----
		//
		// The bridge's material vocabulary (flat keys; same names on create,
		// edit and read). Graph materials compute their surface in the graph, so
		// for them most of these do nothing visible.
		const char* const kMaterialKeys[] = {
			"diffuseColour", "emissiveColour", "metallic", "roughness", "smoothness", "transparent", "isWater",
			"affectsGI", "emissiveAffectsGI", "receivesSnow", "cullMode", "rainDripIntensity", "materialModel",
			"modelParams", "windSwayParams", "cullDistance", "textures",
		};

		struct TextureSlotName { const char* name; MaterialTexture slot; };
		const TextureSlotName kTextureSlots[] = {
			{ "albedo", MaterialTexture::Albedo }, { "normal", MaterialTexture::Normal },
			{ "roughness", MaterialTexture::Roughness }, { "metallic", MaterialTexture::Metallic },
			{ "height", MaterialTexture::Height }, { "emission", MaterialTexture::Emission },
			{ "opacity", MaterialTexture::Opacity }, { "ambientOcclusion", MaterialTexture::AmbientOcclusion },
		};

		const char* CullModeName(CullingMode m)
		{
			switch (m)
			{
			case CullingMode::NoCulling: return "none";
			case CullingMode::FrontFace: return "front";
			default: return "back";
			}
		}

		math::Vector4 ReadColourValue(const json& v, const char* key)
		{
			if (!IsNumericVector(v) || v.size() < 3 || v.size() > 4)
				throw ParamError(std::string("'") + key + "' must be [r, g, b] or [r, g, b, a]");
			return math::Vector4(v[0].get<float>(), v[1].get<float>(), v[2].get<float>(), v.size() == 4 ? v[3].get<float>() : 1.0f);
		}

		math::Vector4 ReadVec4Value(const json& v, const char* key)
		{
			if (!IsNumericVector(v) || v.size() != 4)
				throw ParamError(std::string("'") + key + "' must be an array of 4 numbers");
			return math::Vector4(v[0].get<float>(), v[1].get<float>(), v[2].get<float>(), v[3].get<float>());
		}

		// Validates everything first (throws ParamError, nothing applied), then
		// applies. `params` may carry other, non-material keys listed in
		// `ignoreKeys`.
		void ApplyMaterialParams(Material& m, const json& params, std::initializer_list<const char*> ignoreKeys)
		{
			if (!params.is_object())
				throw ParamError("material properties must be an object");

			auto isIgnored = [&](const std::string& k)
			{
				for (const char* i : ignoreKeys) if (k == i) return true;
				return false;
			};
			auto num = [&](const json& v, const char* key) -> float
			{
				if (!v.is_number()) throw ParamError(std::string("'") + key + "' must be a number");
				return v.get<float>();
			};
			auto boolean = [&](const json& v, const char* key) -> bool
			{
				if (!v.is_boolean()) throw ParamError(std::string("'") + key + "' must be true or false");
				return v.get<bool>();
			};

			// Pass 1: validate, and resolve textures.
			std::vector<std::pair<MaterialTexture, std::shared_ptr<ITexture2D>>> textures;
			for (auto it = params.begin(); it != params.end(); ++it)
			{
				const std::string& k = it.key();
				if (isIgnored(k)) continue;
				bool known = false;
				for (const char* mk : kMaterialKeys) if (k == mk) known = true;
				if (!known)
				{
					std::string list;
					for (const char* mk : kMaterialKeys) list += (list.empty() ? "" : ", ") + std::string(mk);
					throw ParamError("unknown material property '" + k + "' (valid: " + list + ")");
				}
				if (k == "textures")
				{
					if (!it->is_object()) throw ParamError("'textures' must be an object {slot: asset path or null}");
					for (auto t = it->begin(); t != it->end(); ++t)
					{
						const TextureSlotName* slot = nullptr;
						for (const auto& s : kTextureSlots) if (_stricmp(t.key().c_str(), s.name) == 0) slot = &s;
						if (!slot) throw ParamError("unknown texture slot '" + t.key() + "' (albedo, normal, roughness, metallic, height, emission, opacity, ambientOcclusion)");
						if (t->is_null()) { textures.push_back({ slot->slot, nullptr }); continue; }
						if (!t->is_string()) throw ParamError("texture '" + t.key() + "' must be an asset path or null");
						AssetRef ref;
						std::string err;
						if (!ResolveAssetRef(t->get<std::string>(), nullptr, false, ref, err)) throw ParamError("texture '" + t.key() + "': " + err);
						if (!AssetExists(ref)) throw ParamError("texture '" + t.key() + "': no file at '" + ref.ref + "'");
						auto tex = ITexture2D::Create(fs::path(Utf8ToWide(ref.ref)));
						if (!tex) throw ParamError("texture '" + t.key() + "': '" + ref.ref + "' could not be loaded");
						textures.push_back({ slot->slot, tex });
					}
				}
				else if (k == "diffuseColour" || k == "emissiveColour") ReadColourValue(*it, k.c_str());
				else if (k == "modelParams" || k == "windSwayParams") ReadVec4Value(*it, k.c_str());
				else if (k == "transparent" || k == "isWater" || k == "affectsGI" || k == "emissiveAffectsGI" || k == "receivesSnow") boolean(*it, k.c_str());
				else if (k == "cullMode")
				{
					const std::string v = it->is_string() ? it->get<std::string>() : std::string();
					if (v != "back" && v != "front" && v != "none") throw ParamError("'cullMode' must be \"back\", \"front\" or \"none\" (two-sided)");
				}
				else if (k == "materialModel") { if (!it->is_number_integer()) throw ParamError("'materialModel' must be an integer"); }
				else num(*it, k.c_str());
			}

			// Pass 2: apply.
			for (auto it = params.begin(); it != params.end(); ++it)
			{
				const std::string& k = it.key();
				if (isIgnored(k) || k == "textures") continue;
				auto& pr = m._properties;
				if (k == "diffuseColour") pr.diffuseColour = ReadColourValue(*it, "diffuseColour");
				else if (k == "emissiveColour") pr.emissiveColour = ReadColourValue(*it, "emissiveColour");
				else if (k == "metallic") pr.metallicFactor = it->get<float>();
				else if (k == "roughness") pr.roughnessFactor = it->get<float>();
				else if (k == "smoothness") pr.smoothness = it->get<float>();
				else if (k == "rainDripIntensity") pr.rainDripIntensity = it->get<float>();
				else if (k == "materialModel") pr.materialModel = it->get<int>();
				else if (k == "modelParams") pr.modelParams = ReadVec4Value(*it, "modelParams");
				else if (k == "windSwayParams") pr.windSwayParams = ReadVec4Value(*it, "windSwayParams");
				else if (k == "isWater") pr.isWater = it->get<bool>() ? 1 : 0;
				else if (k == "transparent")
				{
					pr.hasTransparency = it->get<bool>() ? 1 : 0;
					m.SetBlendState(it->get<bool>() ? BlendState::Transparency : BlendState::Opaque);
				}
				else if (k == "affectsGI") m.SetAffectsGI(it->get<bool>());
				else if (k == "emissiveAffectsGI") m.SetEmissiveAffectsGI(it->get<bool>());
				else if (k == "receivesSnow") m.SetReceivesSnow(it->get<bool>());
				else if (k == "cullDistance") m.SetCullDistance(it->get<float>());
				else if (k == "cullMode")
				{
					const std::string v = it->get<std::string>();
					m.SetCullMode(v == "none" ? CullingMode::NoCulling : v == "front" ? CullingMode::FrontFace : CullingMode::BackFace);
				}
			}
			for (auto& t : textures)
				m.SetTexture(t.first, t.second);
			m.InvalidateGiGraphTintCache();
		}

		json MaterialJson(const Material& mc)
		{
			const Material& m = mc;
			const auto& pr = m._properties;
			auto v4 = [](const math::Vector4& v) { return json::array({ v.x, v.y, v.z, v.w }); };
			json textures = json::object();
			for (const auto& s : kTextureSlots)
			{
				const auto& tex = m.GetTexture(s.slot);
				textures[s.name] = tex ? json(tex->GetFileSystemPath().generic_string()) : json(nullptr);
			}
			json out = json{
				{"diffuseColour", v4(pr.diffuseColour)},
				{"emissiveColour", v4(pr.emissiveColour)},
				{"metallic", pr.metallicFactor},
				{"roughness", pr.roughnessFactor},
				{"smoothness", pr.smoothness},
				{"transparent", pr.hasTransparency != 0},
				{"isWater", pr.isWater != 0},
				{"affectsGI", m.GetAffectsGI()},
				{"emissiveAffectsGI", m.GetEmissiveAffectsGI()},
				{"receivesSnow", m.GetReceivesSnow()},
				{"cullMode", CullModeName(m.GetCullMode())},
				{"rainDripIntensity", pr.rainDripIntensity},
				{"materialModel", pr.materialModel},
				{"modelParams", v4(pr.modelParams)},
				{"windSwayParams", v4(pr.windSwayParams)},
				{"cullDistance", m.GetCullDistance()},
				{"textures", textures},
			};
			return out;
		}

		// Write `src` as a .hmat at `ref` and return the engine's shared copy
		// of that asset. A copy already loaded (overwrite) is refreshed in place
		// so every user sees the new values.
		std::shared_ptr<Material> WriteMaterialAsset(const Material& src, const AssetRef& ref, std::string& error)
		{
			IEditorContext* ctx = Ctx();
			FileSystem* projectFs = ctx ? ctx->GetProjectFileSystem() : nullptr;
			if (!projectFs) { error = "no project is open"; return nullptr; }
			std::error_code ec;
			fs::create_directories(ref.absolute.parent_path(), ec); // MaterialLoader pops a dialog if the open fails

			auto file = std::make_shared<Material>();
			file->CopyFrom(src);
			file->SetPaths(ref.absolute, projectFs);
			file->SetLoader(g_pEnv->GetResourceSystem().FindResourceLoaderForExtension(".hmat"));
			file->Save();
			if (!fs::exists(ref.absolute, ec))
			{
				error = "failed to write '" + ref.absolute.string() + "'";
				return nullptr;
			}
			auto shared = Material::Create(fs::path(Utf8ToWide(ref.ref)));
			if (!shared)
			{
				error = "wrote '" + ref.ref + "' but the engine could not load it back";
				return nullptr;
			}
			if (shared.get() != &src)
				shared->CopyFrom(src);
			return shared;
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
			bool    undoable = false;   // its edits are recorded as one undo step
			Handler fn;
		};

		// Everything recorded inside one bridge request (single call or whole
		// batch) becomes ONE editor undo step. Exception-safe.
		struct UndoGroupScope
		{
			IEditorContext* ctx = nullptr;
			UndoGroupScope(bool enable, const std::string& label)
			{
				if (enable && (ctx = Ctx()) != nullptr)
					ctx->BeginUndoGroup(label);
			}
			~UndoGroupScope()
			{
				if (ctx != nullptr)
					ctx->EndUndoGroup();
			}
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
			const std::string label = "Bridge: " + req.method;
			return OnMain(id, [&info, id, params, label]() {
				UndoGroupScope undo(info.undoable, label);
				return Invoke(info, id, params);
			});
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

			const bool anyUndoable = std::any_of(ops.begin(), ops.end(), [](const Op& o) { return o.info->undoable; });
			return OnMain(id, [id, ops = std::move(ops), stopOnError, anyUndoable]() mutable -> json {
				UndoGroupScope undo(anyUndoable, "Bridge: batch (" + std::to_string(ops.size()) + " ops)");
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
					{"prefabStageOpen", ctx ? ctx->IsPrefabEditStageOpen() : false},
					{"undoCount", ctx ? ctx->GetUndoCount() : 0},
					{"redoCount", ctx ? ctx->GetRedoCount() : 0},
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
				const json fields = SerializeComponent(target);
				json result = json{
					{"entity", EntityBrief(e)},
					{"component", compName},
					{"fields", fields},
					{"note", "Fields are the component's own Serialize() output; components with an empty/no-op Serialize return {}."},
				};
				// Enum names / ranges / units for fields that have metadata.
				if (json info = FieldInfoJson(compName, &fields); !info.empty())
					result["fieldInfo"] = info;
				return MakeResult(id, result);
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

			// ---- Console variables / commands (the "environment" knobs: render,
			//      GI, fog, post, weather... - cvars are saved per scene) ----

			Reg("list_cvars", R, Main, Batch, [](int64_t id, const json& p) -> json {
				const std::string filter = p.value("filter", std::string());
				const size_t offset = (size_t)std::max<int64_t>(0, p.value("offset", (int64_t)0));
				size_t limit = (size_t)std::max<int64_t>(1, p.value("limit", (int64_t)200));
				if (limit > 2000) limit = 2000;
				const bool full = p.value("details", true);
				std::vector<HVar*> vars;
				CommandManager::LockVars();
				for (HVar* v = g_hvars; v != nullptr; v = v->_next)
				{
					if (!filter.empty())
					{
						// case-insensitive substring over name + description
						auto lower = [](std::string s) { std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); }); return s; };
						const std::string f = lower(filter);
						if (lower(v->_name).find(f) == std::string::npos && lower(v->_description).find(f) == std::string::npos)
							continue;
					}
					vars.push_back(v);
				}
				std::sort(vars.begin(), vars.end(), [](HVar* a, HVar* b) { return a->_name < b->_name; });
				json list = json::array();
				for (size_t i = offset; i < vars.size() && list.size() < limit; ++i)
					list.push_back(HVarJson(vars[i], full));
				CommandManager::UnlockVars();
				return MakeResult(id, json{ {"matched", vars.size()}, {"offset", offset}, {"returned", list.size()}, {"cvars", list},
					{"note", "Values are typed. Set with set_cvars {values: {name: value}}; vars clamp to [min, max]. Cvars are saved with the scene."} });
			});

			Reg("get_cvars", R, Main, Batch, [](int64_t id, const json& p) -> json {
				if (!p.contains("names") || !p["names"].is_array() || p["names"].empty())
					throw ParamError("get_cvars requires 'names': [\"r_name\", ...]");
				json out = json::object();
				json missing = json::array();
				CommandManager::LockVars();
				for (const auto& n : p["names"])
				{
					if (!n.is_string()) { CommandManager::UnlockVars(); throw ParamError("'names' must contain strings"); }
					HVar* v = g_pEnv->_commandManager->FindHVar(n.get<std::string>());
					if (v) out[v->_name] = HVarJson(v, true);
					else missing.push_back(n);
				}
				CommandManager::UnlockVars();
				return MakeResult(id, json{ {"cvars", out}, {"missing", missing} });
			});

			Reg("list_console_commands", R, Main, Batch, [](int64_t id, const json& p) -> json {
				const std::string filter = p.value("filter", std::string());
				std::vector<std::string> names;
				CommandManager::LockCommands();
				for (HCommand* c = g_commands; c != nullptr; c = c->_next)
					if (filter.empty() || c->_name.find(filter) != std::string::npos)
						names.push_back(c->_name);
				CommandManager::UnlockCommands();
				std::sort(names.begin(), names.end());
				return MakeResult(id, json{ {"count", names.size()}, {"commands", names},
					{"note", "Run with exec_console. Commands take space-separated args; they have no machine-readable signature."} });
			});

			// ---- Component catalogue ----

			Reg("describe_component_type", R, Main, Batch, [this](int64_t id, const json& p) -> json {
				const std::string name = RequireString(p, "component");
				auto* cls = g_pEnv->_classRegistry->Find(name);
				if (cls == nullptr)
					return MakeError(id, ErrorCode::NotAvailable, "component class '" + name + "' is not registered (see list_component_types)");

				json defaults;
				std::string source;
				if (auto it = _componentDefaults.find(name); it != _componentDefaults.end())
				{
					defaults = it->second;
					source = "defaults";
				}
				else if (kNoScratchInstance.count(name) != 0)
				{
					// Construction has global side effects (e.g. key binds that
					// are never removed): read a live instance instead.
					if (auto scene = PrimaryUserScene())
					{
						std::vector<EntityId> ids;
						scene->GetLiveEntityIds(ids);
						for (const EntityId& eid : ids)
						{
							Entity* e = scene->TryGetEntity(eid);
							if (!e) continue;
							if (BaseComponent* c = FindComponent(e, name))
							{
								defaults = SerializeComponent(c);
								source = "scene-instance:" + e->GetName();
								break;
							}
						}
					}
					if (source.empty())
						source = "unavailable (constructing this component has global side effects; add it to an entity and use inspect_component)";
				}
				else
				{
					// Build a throwaway instance in a private, UNREGISTERED scene
					// (the same isolation PrefabLoader uses for prefab assets) so the
					// user scene never sees it - no new main camera, sun, outliner
					// churn or GI invalidation. Released immediately; cached.
					auto scratch = g_pEnv->GetSceneManager().CreateEmptyScene(false, nullptr, false);
					if (scratch == nullptr)
						return MakeError(id, ErrorCode::Internal, "could not create a scratch scene");
					Entity* e = scratch->CreateEntity("__bridge_describe", math::Vector3(0.0f, 0.0f, 0.0f), math::Quaternion(0.0f, 0.0f, 0.0f, 1.0f), math::Vector3(1.0f, 1.0f, 1.0f));
					if (e == nullptr)
						return MakeError(id, ErrorCode::Internal, "could not create a scratch entity");
					e->SetFlag(EntityFlags::DoNotSave);
					BaseComponent* created = cls->newInstanceFn(e);
					BaseComponent* component = created ? e->AddComponent(created) : nullptr;
					if (component == nullptr)
						return MakeError(id, ErrorCode::Internal, "failed to instantiate '" + name + "'");
					defaults = SerializeComponent(component);
					scratch.reset(); // tears the entity + component down
					_componentDefaults[name] = defaults;
					source = "defaults";
				}

				json fields = json::array();
				const json info = FieldInfoJson(name, nullptr);
				if (defaults.is_object())
				{
					for (auto& [key, value] : defaults.items())
					{
						json f = json{ {"name", key}, {"type", JsonFieldType(value)}, {"default", value} };
						if (info.contains(key))
						{
							for (auto& [ik, iv] : info[key].items())
								f[ik] = iv;
							if (info[key].contains("enum")) f["type"] = "enum";
							if (info[key].contains("flags")) f["type"] = "flags";
						}
						fields.push_back(f);
					}
				}
				return MakeResult(id, json{
					{"component", name},
					{"valuesFrom", source},
					{"fieldCount", fields.size()},
					{"fields", fields},
					{"note", fields.empty()
						? "This component serialises no fields: it has nothing to configure through the bridge."
						: "Pass these names to add_component / create_entity components[].fields / set_component_field. enum/flags fields also accept value names (flags: an array of names)."},
				});
			});

			// ---- Prefabs (read) ----

			Reg("list_prefabs", R, Main, Batch, [](int64_t id, const json& p) -> json {
				fs::path base;
				std::string err;
				if (!ResolveProjectPath(p.value("subdir", std::string("")), base, err))
					return MakeError(id, ErrorCode::NotAvailable, err);
				json list = json::array();
				std::error_code ec;
				for (fs::recursive_directory_iterator it(base, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
				{
					if (!it->is_regular_file(ec) || _wcsicmp(it->path().extension().c_str(), L".hprefab") != 0)
						continue;
					list.push_back(json{ {"path", ProjectRelative(it->path())}, {"name", it->path().stem().string()} });
					if (list.size() >= 2000) break;
				}
				return MakeResult(id, json{ {"count", list.size()}, {"prefabs", list},
					{"note", "Pass 'path' to instantiate_prefab / open_prefab_stage. Paths are relative to the project folder."} });
			});

			Reg("prefab_info", R, Main, Batch, [](int64_t id, const json& p) -> json {
				auto scene = PrimaryUserScene();
				if (!scene) return NoScene(id);
				json err;
				Entity* e = RequireEntity(id, scene.get(), p, "entity", true, err);
				if (!e) return err;
				IEditorContext* ctx = Ctx();
				if (!ctx)
					return MakeError(id, ErrorCode::NotAvailable, "editor context not available");
				json info = ctx->DescribePrefabInstance(e);
				if (info.contains("sourcePath") && info["sourcePath"].is_string())
					info["sourcePath"] = ProjectRelative(fs::path(info["sourcePath"].get<std::string>()));
				info["entity"] = EntityBrief(e);
				return MakeResult(id, info);
			});

			// ================= WRITE =================

			Reg("set_cvars", W, Main, Batch, [](int64_t id, const json& p) -> json {
				if (!p.contains("values") || !p["values"].is_object() || p["values"].empty())
					throw ParamError("set_cvars requires 'values': {\"r_name\": value, ...}");
				// Validate every name first so a typo applies nothing.
				CommandManager::LockVars();
				std::vector<std::pair<HVar*, json>> todo;
				for (auto& [name, value] : p["values"].items())
				{
					HVar* v = g_pEnv->_commandManager->FindHVar(name);
					if (v == nullptr)
					{
						CommandManager::UnlockVars();
						return MakeError(id, ErrorCode::NotAvailable, "no cvar named '" + name + "' (see list_cvars); nothing was changed");
					}
					todo.push_back({ v, value });
				}
				// Then type-check by applying to a copy of each value.
				for (auto& [v, value] : todo)
				{
					HVar::Val saved;
					std::memcpy(&saved, &v->_val, sizeof(HVar::Val));
					std::string msg;
					const bool ok = SetHVarFromJson(v, value, msg);
					std::memcpy(&v->_val, &saved, sizeof(HVar::Val));
					if (!ok)
					{
						CommandManager::UnlockVars();
						return MakeError(id, ErrorCode::InvalidParams, msg + "; nothing was changed");
					}
				}
				json results = json::object();
				for (auto& [v, value] : todo)
				{
					const json before = HVarValueJson(v->GetType(), v->_val);
					std::string msg;
					SetHVarFromJson(v, value, msg, false);
					const json requested = HVarValueJson(v->GetType(), v->_val);
					v->Clamp();
					const json after = HVarValueJson(v->GetType(), v->_val);
					json r = json{ {"before", before}, {"value", after} };
					if (after != requested)
					{
						r["clamped"] = true;
						r["requested"] = requested;
					}
					results[v->_name] = r;
					LOG_INFO("EditorBridge: cvar %s %s -> %s", v->_name.c_str(), before.dump().c_str(), after.dump().c_str());
				}
				CommandManager::UnlockVars();
				return MakeResult(id, json{ {"cvars", results} });
			});

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
				if (IEditorContext* ctx = Ctx()) ctx->RecordEntityCreated(e);
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
				IEditorContext* ctx = Ctx();
				if (ctx) ctx->BeginComponentEdit(entity);
				json r = AddComponentByName(id, entity, p, err);
				// A property edit on an existing component records here; a new
				// component changes the layout, so this is a no-op and the add
				// is recorded instead.
				if (ctx) ctx->EndComponentEdit(entity);
				if (r.is_null()) return err;
				if (ctx && !r.value("alreadyExisted", false))
					if (BaseComponent* added = FindComponent(entity, r["component"].get<std::string>()))
						ctx->RecordComponentAdded(added);
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
				if (IEditorContext* ctx = Ctx()) ctx->RecordComponentDeleted(c);
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
				if (IEditorContext* ctx = Ctx()) ctx->RecordEntityRename(e, before, finalName);
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
				Entity* oldParent = e->GetParent();
				e->SetParent(parent, preserveWorld);
				if (IEditorContext* ctx = Ctx()) ctx->RecordEntityParentChange(e, oldParent, parent);
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
				const bool hiddenBefore = e->HasFlag(EntityFlags::DoNotRender);
				for (auto& [f, set] : changes)
				{
					if (set) e->SetFlag(f);
					else e->ClearFlags(f);
				}
				if (IEditorContext* ctx = Ctx(); ctx && hiddenBefore != e->HasFlag(EntityFlags::DoNotRender))
					ctx->RecordEntityVisibilityChange(e, hiddenBefore, e->HasFlag(EntityFlags::DoNotRender));
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
				if (IEditorContext* ctx = Ctx()) ctx->RecordEntityCreated(clone);
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
				const math::Vector3 positionBefore = e->GetPosition();
				const math::Quaternion rotationBefore = e->GetRotation();
				const math::Vector3 scaleBefore = e->GetScale();
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
				// Cameras own their orientation (see above), so their moves aren't
				// replayed through the Transform-based undo.
				if (IEditorContext* ctx = Ctx(); ctx && camera == nullptr)
					ctx->RecordEntityTransformChange(e, positionBefore, rotationBefore, scaleBefore);
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
				IEditorContext* ctx = Ctx();
				if (ctx) ctx->BeginComponentEdit(e);
				const bool applied = ApplyComponentFields(target, p["fields"], after, msg);
				if (ctx) ctx->EndComponentEdit(e); // records nothing when unchanged
				if (!applied)
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
				if (IEditorContext* ctx = Ctx()) ctx->RecordEntityDeleted(e);
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

				std::vector<math::Vector3> normals; // computed by BuildMeshEntity when omitted
				if (p.contains("normals"))
				{
					if (!parseVec3s(p["normals"], normals) || normals.size() != positions.size())
						return MakeError(id, ErrorCode::InvalidParams, "'normals' must match the vertex count");
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

				Geometry g;
				g.positions = std::move(positions);
				g.normals = std::move(normals);
				g.uvs = std::move(uvs);
				g.indices = std::move(indices);
				return BuildMeshEntity(id, scene, p, g, json::object());
			});

			// ---- Primitives, mesh assets, materials (C2) ----

			Reg("create_primitive", W, Main, Batch, [this](int64_t id, const json& p) -> json {
				auto scene = PrimaryUserScene();
				if (!scene) return NoScene(id);
				std::string shape = RequireString(p, "shape");
				std::transform(shape.begin(), shape.end(), shape.begin(), [](unsigned char c) { return (char)std::tolower(c); });
				Geometry g;
				const json used = GeneratePrimitive(shape, p, g);
				return BuildMeshEntity(id, scene, p, g, json{ {"shape", shape}, {"shapeParams", used} });
			});

			Reg("save_mesh_asset", W, Main, Batch, [this](int64_t id, const json& p) -> json {
				auto scene = PrimaryUserScene();
				if (!scene) return NoScene(id);
				json err;
				Entity* e = RequireEntity(id, scene.get(), p, "entity", true, err);
				if (!e) return err;
				json out = EntityBrief(e);
				std::string msg;
				if (!SaveMeshAsset(e, p, out, msg))
					return MakeError(id, ErrorCode::InvalidParams, msg);
				out["note"] = "the entity now references the asset and saves with the scene; reuse the mesh elsewhere via its ref";
				return MakeResult(id, out);
			});

			Reg("list_assets", R, Main, Batch, [](int64_t id, const json& p) -> json {
				struct TypeExt { const char* type; std::vector<const wchar_t*> exts; };
				static const TypeExt kTypes[] = {
					{ "mesh", { L".hmesh" } }, { "material", { L".hmat" } }, { "prefab", { L".hprefab" } },
					{ "scene", { L".hscene" } }, { "texture", { L".png", L".dds", L".tga", L".jpg", L".jpeg", L".bmp", L".hdr" } },
				};
				const std::string type = p.value("type", std::string("all"));
				std::vector<const TypeExt*> wanted;
				for (const auto& t : kTypes) if (type == "all" || type == t.type) wanted.push_back(&t);
				if (wanted.empty()) throw ParamError("'type' must be mesh, material, prefab, scene, texture or all");
				std::string filter = p.value("filter", std::string());
				std::transform(filter.begin(), filter.end(), filter.begin(), [](unsigned char c) { return (char)std::tolower(c); });
				const size_t limit = (size_t)std::clamp(p.value("limit", 500), 1, 5000);
				const std::string subdir = p.value("subdir", std::string());

				IEditorContext* ctx = Ctx();
				FileSystem* projectFs = ctx ? ctx->GetProjectFileSystem() : nullptr;
				std::vector<FileSystem*> roots;
				if (projectFs) roots.push_back(projectFs);
				if (p.value("includeEngine", false))
					for (FileSystem* f : g_pEnv->GetResourceSystem().GetFileSystems())
						if (f && f != projectFs && !f->IsAsset()) roots.push_back(f);
				if (roots.empty()) return MakeError(id, ErrorCode::NotAvailable, "no project is open");

				json list = json::array();
				size_t matched = 0;
				std::error_code ec;
				for (FileSystem* f : roots)
				{
					const fs::path dataDir = f->GetDataDirectory();
					const fs::path base = subdir.empty() ? dataDir : dataDir / fs::path(Utf8ToWide(subdir));
					for (fs::recursive_directory_iterator it(base, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
					{
						std::error_code fec;
						if (!it->is_regular_file(fec)) continue;
						const std::wstring ext = it->path().extension().wstring();
						const char* kind = nullptr;
						for (const TypeExt* t : wanted)
							for (const wchar_t* x : t->exts)
								if (_wcsicmp(ext.c_str(), x) == 0) kind = t->type;
						if (!kind) continue;
						const std::string rel = fs::relative(it->path(), dataDir, fec).generic_string();
						if (!filter.empty())
						{
							std::string low = rel;
							std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return (char)std::tolower(c); });
							if (low.find(filter) == std::string::npos) continue;
						}
						++matched;
						if (list.size() < limit)
							list.push_back(json{ {"ref", WideToUtf8(f->GetName()) + "." + rel}, {"type", kind}, {"name", it->path().stem().string()} });
					}
					ec.clear();
				}
				return MakeResult(id, json{ {"matched", matched}, {"returned", list.size()}, {"truncated", matched > list.size()}, {"assets", list},
					{"note", "Pass a ref (or a Data-relative path) to any tool taking an asset path."} });
			});

			// Material: by asset path, or the material an entity's mesh uses.
			auto resolveMaterial = [this](int64_t id, const json& p, std::shared_ptr<Material>& mat, AssetRef& ref, StaticMeshComponent** smcOut, json& err) -> bool
			{
				if (p.contains("entity") && !p["entity"].is_null())
				{
					auto scene = PrimaryUserScene();
					if (!scene) { err = NoScene(id); return false; }
					Entity* e = RequireEntity(id, scene.get(), p, "entity", false, err);
					if (!e) return false;
					auto* smc = RequireMeshComponent(id, e, err);
					if (!smc) return false;
					if (smcOut) *smcOut = smc;
					mat = smc->GetMaterial();
					if (!mat) { err = MakeError(id, ErrorCode::NotAvailable, "'" + e->GetName() + "' has no material"); return false; }
					const std::string fsPath = mat->GetFileSystemPath().generic_string();
					std::string msg;
					if (!fsPath.empty() && !ResolveAssetRef(fsPath, nullptr, false, ref, msg)) { err = MakeError(id, ErrorCode::NotAvailable, msg); return false; }
					return true;
				}
				std::string msg;
				if (!ResolveAssetRef(RequireString(p, "path"), ".hmat", false, ref, msg)) { err = MakeError(id, ErrorCode::InvalidParams, msg); return false; }
				std::error_code ec;
				if (!AssetExists(ref)) { err = MakeError(id, ErrorCode::NotAvailable, "no material at '" + ref.ref + "' (see list_assets type=material)"); return false; }
				mat = Material::Create(fs::path(Utf8ToWide(ref.ref)));
				if (!mat) { err = MakeError(id, ErrorCode::Internal, "'" + ref.ref + "' could not be loaded"); return false; }
				return true;
			};

			Reg("get_material", R, Main, Batch, [resolveMaterial](int64_t id, const json& p) -> json {
				std::shared_ptr<Material> mat;
				AssetRef ref;
				json err;
				if (!resolveMaterial(id, p, mat, ref, nullptr, err)) return err;
				json out = json{
					{"ref", ref.ref.empty() ? json(nullptr) : json(ref.ref)},
					{"asset", !ref.ref.empty()},
					{"projectAsset", ref.inProject},
					{"isGraph", mat->_hasGraph || mat->_hasGraphInstance},
					{"properties", MaterialJson(*mat)},
				};
				if (mat->_hasGraph || mat->_hasGraphInstance)
					out["note"] = "graph material: its surface comes from the material graph, so most properties have little visible effect";
				return MakeResult(id, out);
			});

			Reg("create_material", W, Main, Batch, [](int64_t id, const json& p) -> json {
				AssetRef out;
				std::string err;
				if (!ResolveAssetRef(RequireString(p, "path"), ".hmat", true, out, err))
					return MakeError(id, ErrorCode::InvalidParams, err);
				std::error_code ec;
				if (fs::exists(out.absolute, ec) && !p.value("overwrite", false))
					return MakeError(id, ErrorCode::InvalidParams, "'" + out.ref + "' already exists (pass overwrite: true, or change it with edit_material)");
				AssetRef baseRef;
				if (!ResolveAssetRef(p.value("base", std::string("EngineData.Materials/Default.hmat")), ".hmat", false, baseRef, err))
					return MakeError(id, ErrorCode::InvalidParams, "base: " + err);
				auto base = Material::Create(fs::path(Utf8ToWide(baseRef.ref)));
				if (!base) return MakeError(id, ErrorCode::NotAvailable, "base material '" + baseRef.ref + "' could not be loaded");
				auto src = std::make_shared<Material>();
				src->CopyFrom(base);
				ApplyMaterialParams(*src, p, { "path", "base", "overwrite" });
				auto shared = WriteMaterialAsset(*src, out, err);
				if (!shared) return MakeError(id, ErrorCode::Internal, err);
				json result = AssetRefJson(out);
				result["base"] = baseRef.ref;
				result["properties"] = MaterialJson(*shared);
				return MakeResult(id, result);
			});

			Reg("edit_material", W, Main, Batch, [this, resolveMaterial](int64_t id, const json& p) -> json {
				std::shared_ptr<Material> mat;
				AssetRef ref;
				json err;
				if (!resolveMaterial(id, p, mat, ref, nullptr, err)) return err;
				if (!ref.ref.empty() && !ref.inProject)
					return MakeError(id, ErrorCode::InvalidParams, "'" + ref.ref + "' is an engine asset shared by everything that uses it; make a project copy with create_material {base: \"" + ref.ref + "\"} and assign_material that instead");
				ApplyMaterialParams(*mat, p, { "path", "entity", "save" });
				MarkPvsDirty();
				bool saved = false;
				if (!ref.ref.empty() && p.value("save", true))
				{
					std::error_code ec;
					const auto before = fs::exists(ref.absolute, ec) ? fs::last_write_time(ref.absolute, ec) : fs::file_time_type{};
					if (!mat->GetLoader()) mat->SetLoader(g_pEnv->GetResourceSystem().FindResourceLoaderForExtension(".hmat"));
					mat->Save();
					saved = fs::exists(ref.absolute, ec) && fs::last_write_time(ref.absolute, ec) != before;
					if (!saved) return MakeError(id, ErrorCode::Internal, "edited in memory, but writing '" + ref.ref + "' failed (see engine logs)");
				}
				json out = json{ {"ref", ref.ref.empty() ? json(nullptr) : json(ref.ref)}, {"saved", saved}, {"properties", MaterialJson(*mat)} };
				out["note"] = ref.ref.empty()
					? "private (unsaved) material: changes are in memory; save_mesh_asset writes it as a .hmat"
					: "asset edit: every entity using this material changes. Not part of undo.";
				return MakeResult(id, out);
			});

			Reg("assign_material", W, Main, Batch, [this](int64_t id, const json& p) -> json {
				auto scene = PrimaryUserScene();
				if (!scene) return NoScene(id);
				json err;
				Entity* e = RequireEntity(id, scene.get(), p, "entity", true, err);
				if (!e) return err;
				auto* smc = RequireMeshComponent(id, e, err);
				if (!smc) return err;
				AssetRef ref;
				std::string msg;
				if (!ResolveAssetRef(RequireString(p, "material"), ".hmat", false, ref, msg))
					return MakeError(id, ErrorCode::InvalidParams, msg);
				if (!AssetExists(ref))
					return MakeError(id, ErrorCode::NotAvailable, "no material at '" + ref.ref + "' (see list_assets type=material)");
				auto mat = Material::Create(fs::path(Utf8ToWide(ref.ref)));
				if (!mat) return MakeError(id, ErrorCode::Internal, "'" + ref.ref + "' could not be loaded");
				const auto previous = smc->GetMaterial();
				// Same record path as set_component_field: undo + prefab override.
				IEditorContext* ctx = Ctx();
				if (ctx) ctx->BeginComponentEdit(e);
				smc->SetMaterial(mat);
				if (ctx) ctx->EndComponentEdit(e);
				MarkPvsDirty();
				return MakeResult(id, json{ {"entity", EntityBrief(e)}, {"material", ref.ref},
					{"previous", previous && !previous->GetFileSystemPath().empty() ? json(previous->GetFileSystemPath().generic_string()) : json(nullptr)} });
			});

			// ---- Undo / redo / save ----

			Reg("undo", W, Main, Solo, [](int64_t id, const json& p) -> json {
				IEditorContext* ctx = Ctx();
				if (!ctx) return MakeError(id, ErrorCode::NotAvailable, "editor context not available");
				int steps = std::clamp(p.value("steps", 1), 1, 100);
				int done = 0;
				while (done < steps && ctx->Undo()) ++done;
				return MakeResult(id, json{ {"undone", done}, {"undoCount", ctx->GetUndoCount()}, {"redoCount", ctx->GetRedoCount()},
					{"note", "Each bridge call or batch is ONE undo step, shared with the editor's own history."} });
			});

			Reg("redo", W, Main, Solo, [](int64_t id, const json& p) -> json {
				IEditorContext* ctx = Ctx();
				if (!ctx) return MakeError(id, ErrorCode::NotAvailable, "editor context not available");
				int steps = std::clamp(p.value("steps", 1), 1, 100);
				int done = 0;
				while (done < steps && ctx->Redo()) ++done;
				return MakeResult(id, json{ {"redone", done}, {"undoCount", ctx->GetUndoCount()}, {"redoCount", ctx->GetRedoCount()} });
			});

			Reg("save_scene", W, Main, Solo, [](int64_t id, const json&) -> json {
				IEditorContext* ctx = Ctx();
				if (!ctx) return MakeError(id, ErrorCode::NotAvailable, "editor context not available");
				std::string err;
				if (!ctx->SaveAll(err))
					return MakeError(id, ErrorCode::NotAvailable, err);
				return MakeResult(id, json{ {"saved", true},
					{"note", "Saved the project file, every open scene (cvars included) and the prefab stage if open. Entities flagged DoNotSave (e.g. in-memory procedural meshes) are skipped."} });
			});

			// ---- Prefabs (write) ----

			Reg("instantiate_prefab", W, Main, Batch, [this](int64_t id, const json& p) -> json {
				auto scene = PrimaryUserScene();
				if (!scene) return NoScene(id);
				fs::path path;
				std::string perr;
				if (!ResolveProjectPath(RequireString(p, "path"), path, perr))
					return MakeError(id, ErrorCode::InvalidParams, perr);
				std::error_code ec;
				if (!fs::exists(path, ec))
					return MakeError(id, ErrorCode::NotAvailable, "no prefab at '" + ProjectRelative(path) + "' (see list_prefabs)");

				Entity* parent = nullptr;
				if (p.contains("parent") && !p["parent"].is_null())
				{
					json err;
					parent = RequireEntity(id, scene.get(), p, "parent", false, err);
					if (!parent) return err;
				}
				math::Vector3 position;
				const bool hasPosition = ReadVec3(p, "position", position);
				math::Quaternion rotation;
				const bool hasRotation = ReadRotation(p, rotation);
				math::Vector3 scale;
				const bool hasScale = ReadVec3(p, "scale", scale);

				auto spawned = g_pEnv->_prefabLoader->LoadPrefab(scene, path);
				const auto roots = SpawnRoots(spawned);
				if (roots.empty())
					return MakeError(id, ErrorCode::Internal, "the prefab spawned no entities (see engine logs)");

				// Same placement rule as the editor's drag & drop: move every root
				// by the offset that puts the first root at `position`.
				const math::Vector3 delta = hasPosition ? position - roots.front()->GetPosition() : math::Vector3(0.0f, 0.0f, 0.0f);
				json rootList = json::array();
				for (Entity* r : roots)
				{
					if (parent) r->SetParent(parent, false);
					if (hasPosition) r->SetPosition(r->GetPosition() + delta);
					if (hasRotation) r->SetRotation(rotation);
					if (hasScale) r->SetScale(scale);
				}
				if (p.contains("name") && p["name"].is_string() && roots.size() == 1)
				{
					std::string finalName;
					scene->RenameEntity(roots.front(), p["name"].get<std::string>(), &finalName);
				}
				if (p.value("persistent", true) == false)
					for (Entity* e : spawned) e->SetFlag(EntityFlags::DoNotSave);
				for (Entity* r : roots)
				{
					if (IEditorContext* ctx = Ctx()) ctx->RecordEntityCreated(r);
					rootList.push_back(EntityBrief(r));
				}
				MarkPvsDirty();
				json result = EntityBrief(roots.front());
				result["roots"] = rootList;
				result["entityCount"] = spawned.size();
				result["prefab"] = ProjectRelative(path);
				return MakeResult(id, result);
			});

			Reg("create_prefab", W, Main, Solo, [this](int64_t id, const json& p) -> json {
				auto scene = PrimaryUserScene();
				if (!scene) return NoScene(id);
				IEditorContext* ctx = Ctx();
				if (!ctx) return MakeError(id, ErrorCode::NotAvailable, "editor context not available");
				json err;
				Entity* e = RequireEntity(id, scene.get(), p, "entity", true, err);
				if (!e) return err;
				std::string requested = p.value("path", std::string());
				if (requested.empty())
					requested = "Prefabs/" + e->GetName() + ".hprefab";
				fs::path path;
				std::string perr;
				if (!ResolveProjectPath(requested, path, perr))
					return MakeError(id, ErrorCode::InvalidParams, perr);
				if (path.extension() != ".hprefab")
					path.replace_extension(".hprefab");
				std::error_code ec;
				if (fs::exists(path, ec) && !p.value("overwrite", false))
					return MakeError(id, ErrorCode::InvalidParams, "'" + ProjectRelative(path) + "' already exists (pass overwrite: true, or edit it with open_prefab_stage)");
				const bool replace = p.value("replaceWithInstance", true);
				std::string cerr;
				Entity* result = ctx->CreatePrefabFromEntity(e, path, replace, cerr);
				if (result == nullptr)
					return MakeError(id, ErrorCode::Internal, cerr);
				MarkPvsDirty();
				json out = json{ {"prefab", ProjectRelative(path)}, {"entity", EntityBrief(result)}, {"replacedWithInstance", replace && cerr.empty()} };
				if (!cerr.empty()) out["warning"] = cerr;
				return MakeResult(id, out);
			});

			auto prefabInstanceOp = [](const char* what) {
				return [what](int64_t id, const json& p) -> json {
					auto scene = PrimaryUserScene();
					if (!scene) return NoScene(id);
					IEditorContext* ctx = Ctx();
					if (!ctx) return MakeError(id, ErrorCode::NotAvailable, "editor context not available");
					json err;
					Entity* e = RequireEntity(id, scene.get(), p, "entity", true, err);
					if (!e) return err;
					std::string msg;
					const std::string op = what;
					if (op == "apply")
					{
						if (!ctx->ApplyPrefabInstance(e, msg)) return MakeError(id, ErrorCode::InvalidParams, msg);
						return MakeResult(id, json{ {"applied", true}, {"entity", EntityBrief(e)},
							{"note", "The prefab asset was rewritten; other instances update per the editor's instance-sync policy."} });
					}
					if (op == "revert")
					{
						ReleaseSelectionOf(e);
						Entity* r = ctx->RevertPrefabInstanceToAsset(e, msg);
						if (!r) return MakeError(id, ErrorCode::InvalidParams, msg);
						return MakeResult(id, json{ {"reverted", true}, {"entity", EntityBrief(r)} });
					}
					if (!ctx->UpdatePrefabInstance(e, msg)) return MakeError(id, ErrorCode::InvalidParams, msg);
					return MakeResult(id, json{ {"updated", true}, {"entity", EntityBrief(e)} });
				};
			};
			Reg("apply_prefab_instance", W, Main, Solo, prefabInstanceOp("apply"));
			Reg("revert_prefab_instance", W, Main, Solo, prefabInstanceOp("revert"));
			Reg("update_prefab_instance", W, Main, Solo, prefabInstanceOp("update"));

			Reg("open_prefab_stage", W, Main, Solo, [this](int64_t id, const json& p) -> json {
				IEditorContext* ctx = Ctx();
				if (!ctx) return MakeError(id, ErrorCode::NotAvailable, "editor context not available");
				fs::path path;
				std::string err;
				if (!ResolveProjectPath(RequireString(p, "path"), path, err))
					return MakeError(id, ErrorCode::InvalidParams, err);
				if (!ctx->OpenPrefabEditStage(path, err))
					return MakeError(id, ErrorCode::NotAvailable, err);
				MarkPvsDirty();
				auto scene = PrimaryUserScene();
				return MakeResult(id, json{ {"opened", ProjectRelative(path)}, {"stageScene", scene ? WideToUtf8(scene->GetName()) : std::string()},
					{"note", "Every entity/component tool now edits the PREFAB. Finish with save_prefab_stage and/or close_prefab_stage."} });
			});

			Reg("save_prefab_stage", W, Main, Solo, [](int64_t id, const json&) -> json {
				IEditorContext* ctx = Ctx();
				if (!ctx) return MakeError(id, ErrorCode::NotAvailable, "editor context not available");
				std::string err;
				if (!ctx->SavePrefabEditStage(err))
					return MakeError(id, ErrorCode::NotAvailable, err);
				return MakeResult(id, json{ {"saved", true} });
			});

			Reg("close_prefab_stage", W, Main, Solo, [this](int64_t id, const json& p) -> json {
				IEditorContext* ctx = Ctx();
				if (!ctx) return MakeError(id, ErrorCode::NotAvailable, "editor context not available");
				if (!p.contains("save") || !p["save"].is_boolean())
					throw ParamError("close_prefab_stage requires 'save': true (keep changes) or false (discard)");
				std::string err;
				if (!ctx->ClosePrefabEditStage(p["save"].get<bool>(), err))
					return MakeError(id, ErrorCode::NotAvailable, err);
				MarkPvsDirty();
				auto scene = PrimaryUserScene();
				return MakeResult(id, json{ {"closed", true}, {"saved", p["save"].get<bool>()}, {"activeScene", scene ? WideToUtf8(scene->GetName()) : std::string()} });
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

			// Methods whose edits go through the editor's undo history (each call
			// or batch = one step). The mesh builders only record an entity once
			// it is backed by a .hmesh (saveAs): an in-memory mesh couldn't be
			// rebuilt by redo.
			for (const char* name : { "create_entity", "add_component", "remove_component", "rename_entity", "set_parent",
				"set_entity_flags", "clone_entity", "set_entity_transform", "set_component_field", "delete_entity",
				"instantiate_prefab", "create_prefab", "create_mesh_entity", "create_primitive", "assign_material" })
				_methods[name].undoable = true;
		}

	private:
		// ---- C2: mesh entities, mesh + material assets (main thread only) ----

		// The material a new mesh entity gets. `material` may be omitted (the
		// engine Default), an asset path, or an object {path?, saveAs?,
		// overwrite?, ...properties}. Properties make a private copy of the base;
		// saveAs writes that copy as a project .hmat and uses the shared asset.
		std::shared_ptr<Material> ResolveMeshMaterial(const json& p, json& info)
		{
			const json mp = p.contains("material") ? p["material"] : json();
			std::string basePath = "EngineData.Materials/Default.hmat";
			json props = json::object();
			std::string saveAs;
			bool overwrite = false;
			if (mp.is_string())
				basePath = mp.get<std::string>();
			else if (mp.is_object())
			{
				for (auto it = mp.begin(); it != mp.end(); ++it)
				{
					if (it.key() == "path") { if (!it->is_string()) throw ParamError("'material.path' must be a string"); basePath = it->get<std::string>(); }
					else if (it.key() == "saveAs") { if (!it->is_string()) throw ParamError("'material.saveAs' must be a string"); saveAs = it->get<std::string>(); }
					else if (it.key() == "overwrite") overwrite = it->get<bool>();
					else props[it.key()] = *it;
				}
			}
			else if (!mp.is_null())
				throw ParamError("'material' must be an asset path or an object");

			AssetRef baseRef;
			std::string err;
			if (!ResolveAssetRef(basePath, ".hmat", false, baseRef, err)) throw ParamError("material: " + err);
			std::error_code ec;
			if (!AssetExists(baseRef)) throw ParamError("material: no .hmat at '" + baseRef.ref + "' (see list_assets type=material)");
			auto base = Material::Create(fs::path(Utf8ToWide(baseRef.ref)));
			if (!base) throw ParamError("material '" + baseRef.ref + "' could not be loaded");

			if (props.empty() && saveAs.empty())
			{
				info = json{ {"ref", baseRef.ref}, {"shared", true} };
				return base;
			}

			auto copy = std::make_shared<Material>();
			copy->SetLoader(g_pEnv->GetResourceSystem().FindResourceLoaderForExtension(".hmat"));
			copy->CopyFrom(base);
			ApplyMaterialParams(*copy, props, {});
			if (saveAs.empty())
			{
				info = json{ {"ref", nullptr}, {"base", baseRef.ref}, {"shared", false},
					{"note", "private material copy (not an asset); save_mesh_asset writes it as a .hmat, or pass material.saveAs"} };
				return copy;
			}
			AssetRef out;
			if (!ResolveAssetRef(saveAs, ".hmat", true, out, err)) throw ParamError("material.saveAs: " + err);
			if (fs::exists(out.absolute, ec) && !overwrite)
				throw ParamError("material.saveAs: '" + out.ref + "' already exists (set material.overwrite: true, or reference it with material.path)");
			auto shared = WriteMaterialAsset(*copy, out, err);
			if (!shared) throw std::runtime_error(err);
			info = AssetRefJson(out);
			info["shared"] = true;
			info["written"] = true;
			return shared;
		}

		// Write an entity's bridge-built mesh as a .hmesh (and its private
		// material as a .hmat). Afterwards the entity is an ordinary
		// asset-backed mesh: it saves with the scene and works in prefabs.
		bool SaveMeshAsset(Entity* e, const json& p, json& out, std::string& err)
		{
			IEditorContext* ctx = Ctx();
			FileSystem* projectFs = ctx ? ctx->GetProjectFileSystem() : nullptr;
			if (!projectFs) { err = "no project is open"; return false; }
			auto* smc = e->GetComponent<StaticMeshComponent>();
			auto mesh = smc ? smc->GetMesh() : nullptr;
			if (!mesh) { err = "'" + e->GetName() + "' has no StaticMeshComponent mesh"; return false; }
			if (!mesh->IsDynamicMesh())
			{
				err = "'" + e->GetName() + "' already uses the mesh asset '" + mesh->GetFileSystemPath().generic_string() + "'; only bridge-built meshes (create_primitive / create_mesh_entity) are saved this way";
				return false;
			}
			if (mesh->GetVertices().empty() || mesh->GetIndices().empty()) { err = "the mesh has no geometry"; return false; }

			const bool overwrite = p.value("overwrite", false);
			std::string requested = p.value("path", std::string());
			if (requested.empty()) requested = "Meshes/" + e->GetName() + ".hmesh";
			AssetRef ref;
			if (!ResolveAssetRef(requested, ".hmesh", true, ref, err)) return false;
			std::error_code ec;
			if (fs::exists(ref.absolute, ec) && !overwrite && ref.absolute != fs::path(mesh->GetAbsolutePath()))
			{
				err = "'" + ref.ref + "' already exists (pass overwrite: true)";
				return false;
			}

			// A private material has no file for the mesh/scene to reference:
			// write it next to the mesh (or at materialPath).
			auto material = smc->GetMaterial();
			if (material && material->GetFileSystemPath().empty())
			{
				std::string mreq = p.value("materialPath", std::string());
				if (mreq.empty()) { fs::path mp = ref.absolute; mp.replace_extension(".hmat"); mreq = mp.string(); }
				AssetRef mref;
				if (!ResolveAssetRef(mreq, ".hmat", true, mref, err)) return false;
				if (fs::exists(mref.absolute, ec) && !overwrite)
				{
					err = "'" + mref.ref + "' already exists (pass overwrite: true or a different materialPath)";
					return false;
				}
				auto shared = WriteMaterialAsset(*material, mref, err);
				if (!shared) return false;
				smc->SetMaterial(shared);
				material = shared;
				out["material"] = AssetRefJson(mref);
				out["materialWritten"] = true;
			}
			else if (material)
				out["material"] = json{ {"ref", material->GetFileSystemPath().generic_string()} };

			mesh->SetMaterial(material);
			mesh->SetLoader(g_pEnv->GetResourceSystem().FindResourceLoaderForExtension(".hmesh"));
			mesh->SetPaths(ref.absolute, projectFs);
			fs::create_directories(ref.absolute.parent_path(), ec);
			mesh->Save();
			if (!fs::exists(ref.absolute, ec)) { err = "failed to write '" + ref.absolute.string() + "' (see engine logs)"; return false; }

			const bool wasDoNotSave = e->HasFlag(EntityFlags::DoNotSave);
			e->ClearFlags(EntityFlags::DoNotSave);
			out["mesh"] = AssetRefJson(ref);
			out["clearedDoNotSave"] = wasDoNotSave;
			return true;
		}

		// Shared by create_mesh_entity and create_primitive: `g` holds positions
		// + indices (normals / uvs optional, validated by the caller).
		json BuildMeshEntity(int64_t id, const std::shared_ptr<Scene>& scene, const json& p, Geometry& g, json extra)
		{
			const std::string name = RequireString(p, "name");
			if (p.contains("saveAs") && !p["saveAs"].is_null() && !p["saveAs"].is_string())
				throw ParamError("'saveAs' must be a .hmesh path");

			if (g.normals.size() != g.positions.size())
			{
				// Area-weighted face-normal accumulation (the cross product's
				// length is twice the triangle area).
				g.normals.assign(g.positions.size(), math::Vector3(0.0f, 0.0f, 0.0f));
				for (size_t i = 0; i + 2 < g.indices.size(); i += 3)
				{
					const auto a = g.indices[i], b = g.indices[i + 1], c = g.indices[i + 2];
					const math::Vector3 fn = (g.positions[b] - g.positions[a]).Cross(g.positions[c] - g.positions[a]);
					g.normals[a] += fn; g.normals[b] += fn; g.normals[c] += fn;
				}
				for (auto& n : g.normals)
				{
					const float len = n.Length();
					n = (len > 0.00001f) ? (n / len) : math::Vector3(0.0f, 1.0f, 0.0f);
				}
			}

			Entity* parent = nullptr;
			if (p.contains("parent") && !p["parent"].is_null())
			{
				json err;
				parent = RequireEntity(id, scene.get(), p, "parent", false, err);
				if (!parent) return err;
			}

			json matInfo;
			auto material = ResolveMeshMaterial(p, matInfo);

			// Tangents: from UVs when there are any (so normal maps work),
			// otherwise any axis orthogonal to the normal.
			const bool hasUvs = g.uvs.size() == g.positions.size();
			std::vector<math::Vector3> tan(g.positions.size(), math::Vector3(0, 0, 0)), bit(g.positions.size(), math::Vector3(0, 0, 0));
			if (hasUvs)
			{
				for (size_t i = 0; i + 2 < g.indices.size(); i += 3)
				{
					const auto a = g.indices[i], b = g.indices[i + 1], c = g.indices[i + 2];
					const math::Vector3 e1 = g.positions[b] - g.positions[a], e2 = g.positions[c] - g.positions[a];
					const math::Vector2 d1 = g.uvs[b] - g.uvs[a], d2 = g.uvs[c] - g.uvs[a];
					const float det = d1.x * d2.y - d2.x * d1.y;
					if (fabsf(det) < 1e-12f) continue;
					const float r = 1.0f / det;
					const math::Vector3 t = (e1 * d2.y - e2 * d1.y) * r;
					const math::Vector3 bt = (e2 * d1.x - e1 * d2.x) * r;
					for (auto v : { a, b, c }) { tan[v] += t; bit[v] += bt; }
				}
			}

			std::vector<MeshVertex> verts(g.positions.size());
			math::Vector3 mn = g.positions[0], mx = g.positions[0];
			for (size_t i = 0; i < g.positions.size(); ++i)
			{
				const math::Vector3& n = g.normals[i];
				math::Vector3 t = tan[i] - n * n.Dot(tan[i]); // Gram-Schmidt
				float tl = t.Length();
				if (tl < 1e-6f)
				{
					const math::Vector3 axis = (fabsf(n.y) < 0.99f) ? math::Vector3(0, 1, 0) : math::Vector3(1, 0, 0);
					t = axis.Cross(n);
					tl = t.Length();
				}
				t = (tl > 1e-6f) ? t / tl : math::Vector3(1, 0, 0);
				math::Vector3 b = n.Cross(t);
				if (hasUvs && b.Dot(bit[i]) < 0.0f) b = -b;
				verts[i] = MeshVertex::Create(g.positions[i], n, t, hasUvs ? g.uvs[i].x : 0.0f, hasUvs ? g.uvs[i].y : 0.0f);
				verts[i]._bitangent = b;
				mn = math::Vector3::Min(mn, g.positions[i]);
				mx = math::Vector3::Max(mx, g.positions[i]);
			}

			auto mesh = std::make_shared<Mesh>(nullptr, name);
			if (!mesh->CreateDynamicBuffers((uint32_t)verts.size(), (uint32_t)g.indices.size()) ||
				!mesh->UpdateDynamicGeometry(verts, g.indices))
				return MakeError(id, ErrorCode::Internal, "mesh buffer creation/upload failed");
			dx::BoundingBox aabb;
			dx::BoundingBox::CreateFromPoints(aabb, DirectX::XMVectorSet(mn.x, mn.y, mn.z, 1.0f), DirectX::XMVectorSet(mx.x, mx.y, mx.z, 1.0f));
			dx::BoundingOrientedBox obb;
			dx::BoundingOrientedBox::CreateFromBoundingBox(obb, aabb);
			mesh->SetAABB(aabb);
			mesh->SetOBB(obb);
			mesh->SetMaterial(material);

			math::Vector3 pos(0.0f, 0.0f, 0.0f);
			ReadVec3(p, "position", pos);
			math::Quaternion rot(0.0f, 0.0f, 0.0f, 1.0f);
			ReadRotation(p, rot);
			math::Vector3 scale(1.0f, 1.0f, 1.0f);
			ReadVec3(p, "scale", scale);

			Entity* e = scene->CreateEntity(name, pos, rot, scale);
			if (e == nullptr)
				return MakeError(id, ErrorCode::Internal, "Scene::CreateEntity returned null");
			// Without a mesh asset a scene save would write an empty mesh
			// reference, so in-memory meshes default to DoNotSave.
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
			for (auto it = extra.begin(); it != extra.end(); ++it) result[it.key()] = *it;
			result["vertexCount"] = verts.size();
			result["triangleCount"] = g.indices.size() / 3;
			result["aabbMin"] = json::array({ mn.x, mn.y, mn.z });
			result["aabbMax"] = json::array({ mx.x, mx.y, mx.z });
			result["material"] = matInfo;

			bool assetBacked = false;
			if (p.contains("saveAs") && !p["saveAs"].is_null())
			{
				json saveParams = json{ {"path", p["saveAs"]}, {"overwrite", p.value("overwrite", false)} };
				if (p.contains("materialPath")) saveParams["materialPath"] = p["materialPath"];
				json saved;
				std::string err;
				if (!SaveMeshAsset(e, saveParams, saved, err))
				{
					result["saveError"] = err;
					result["note"] = "entity created, but saving the mesh asset failed; it stays in-memory (DoNotSave). Retry with save_mesh_asset.";
				}
				else
				{
					assetBacked = true;
					result["asset"] = saved;
					if (saved.contains("material"))
						result["material"] = saved["material"]; // the saved copy, not the pre-save private one
				}
			}
			if (assetBacked)
			{
				// Recorded after the save so redo rebuilds it from the asset.
				if (IEditorContext* ctx = Ctx()) ctx->RecordEntityCreated(e);
				result["undoable"] = true;
			}
			else if (!result.contains("note"))
			{
				result["undoable"] = false;
				result["note"] = "in-memory mesh: not saved with the scene and not undoable until save_mesh_asset (or pass saveAs)";
			}
			return MakeResult(id, result);
		}

		StaticMeshComponent* RequireMeshComponent(int64_t id, Entity* e, json& err)
		{
			auto* smc = e->GetComponent<StaticMeshComponent>();
			if (!smc)
				err = MakeError(id, ErrorCode::InvalidParams, "'" + e->GetName() + "' has no StaticMeshComponent");
			return smc;
		}

		struct LogEntry { uint64_t seq; std::string text; LogLevel level; };

		BridgeServer    _server;
		MainThreadQueue _mainThread;
		fs::path        _sessionFile;
		std::string     _token;
		bool            _started = false;
		bool            _pvsDirty = false; // main thread only
		std::atomic<uint64_t> _frameCount{ 0 };

		std::unordered_map<std::string, MethodInfo> _methods;

		// describe_component_type: cached default field values per type (main
		// thread only), and types whose construction has global side effects so
		// they are never built as throwaway instances.
		std::unordered_map<std::string, json> _componentDefaults;
		inline static const std::unordered_set<std::string> kNoScratchInstance = { "RTSCameraController" };

		static constexpr size_t kMaxLogRing = 2000;
		std::mutex              _logMutex;
		std::deque<LogEntry>    _logRing;
		uint64_t                _logSeq = 0;
	};

	static EditorBridgePlugin* g_pEditorBridgePlugin = nullptr;
	CREATE_PLUGIN(g_pEditorBridgePlugin, EditorBridgePlugin);
}
