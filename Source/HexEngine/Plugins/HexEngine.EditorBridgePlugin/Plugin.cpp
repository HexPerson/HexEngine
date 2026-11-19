
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
#include "../../HexEngine.Core/Entity/Component/RigidBody.hpp"
#include "../../HexEngine.Core/Graphics/ITexture2D.hpp"
#include "../../HexEngine.Core/Graphics/Material.hpp"
#include "../../HexEngine.Core/Scene/Mesh.hpp"
#include "../../HexEngine.Core/FileSystem/PrefabLoader.hpp"
#include "../../HexEngine.Core/Physics/IPhysicsSystem.hpp"
#include "../../HexEngine.Core/Physics/PhysUtils.hpp"
#include "../../HexEngine.Core/Graphics/IGraphicsDevice.hpp"
#include "../../HexEngine.Core/Scene/IModelSystem.hpp"
#include "../../HexEngine.Core/Environment/TimeManager.hpp"
#include "../../HexEngine.Core/Input/InputSystem.hpp"
#include "../../HexEngine.Core/Entity/Component/ScriptComponent.hpp"
#include "../../HexEngine.Core/Scripting/IScriptEngine.hpp"
#include "../../HexEngine.Core/Scene/INavMeshProvider.hpp"

#include <deque>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincodec.h>
#include <d3d11.h>
#include <objbase.h>
#include <dxgiformat.h>
#include <DirectXPackedVector.h>
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

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
#include <regex>
#include <condition_variable>
#include <set>
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

		// ---- Spatial queries (D1) ----

		struct RayResult
		{
			bool hit = false;
			float distance = FLT_MAX;
			math::Vector3 position;
			math::Vector3 normal;      // the surface's front-face normal (world)
			bool backFace = false;     // the ray hit the back of the triangle
			Entity* entity = nullptr;
			const char* source = "";
		};

		// The bridge's ray options ARE the engine's (same names, see PhysUtils.hpp).
		using RayOptions = RayCastOptions;

		void AddHierarchy(Entity* e, std::unordered_set<Entity*>& out)
		{
			if (!e || !out.insert(e).second) return;
			for (Entity* c : e->GetChildren()) AddHierarchy(c, out);
		}

		// Nearest accepted hit: the engine raycast (exact triangles via a cached
		// per-mesh BVH + the physics scene for mesh-less colliders), on the
		// edited scene - never GetCurrentScene, which can be the icon scene.
		RayResult CastRay(Scene* scene, const math::Vector3& origin, const math::Vector3& dir, const RayOptions& opt)
		{
			RayResult out;
			if (dir.LengthSquared() < 1e-12f) return out;
			RayHit h;
			if (!PhysUtils::RayCast(scene, origin, dir, opt, &h)) return out;
			out.hit = true;
			out.distance = h.distance;
			out.position = h.position;
			out.normal = h.normal;
			out.backFace = h.backFace;
			out.entity = h.entity;
			out.source = RayHitSourceName(h.source);
			return out;
		}

		json Vec3Json(const math::Vector3& v) { return json::array({ v.x, v.y, v.z }); }

		json RayResultJson(const RayResult& r)
		{
			if (!r.hit) return json{ {"hit", false} };
			json j = json{ {"hit", true}, {"position", Vec3Json(r.position)}, {"normal", Vec3Json(r.normal)},
				{"distance", r.distance}, {"source", r.source}, {"entity", r.entity ? EntityBrief(r.entity) : json(nullptr)} };
			if (r.backFace) j["backFace"] = true;
			return j;
		}

		// World AABB of an entity and its whole hierarchy (prefab roots are
		// often empty transforms whose meshes live on children).
		bool HierarchyAABB(Entity* e, dx::BoundingBox& out)
		{
			bool any = false;
			std::unordered_set<Entity*> all;
			AddHierarchy(e, all);
			for (Entity* x : all)
			{
				if (x->IsPendingDeletion()) continue;
				const dx::BoundingBox& b = x->GetWorldAABB();
				const bool hasExtent = b.Extents.x > 1e-5f || b.Extents.y > 1e-5f || b.Extents.z > 1e-5f;
				if (!hasExtent && x != e) continue;
				if (!any) { out = b; any = true; }
				else dx::BoundingBox::CreateMerged(out, out, b);
			}
			return any;
		}

		// Common ray options from params ("ignore", "only", "includeHidden",
		// "physics", "meshes", "minNormalY", "maxDistance").
		RayOptions ReadRayOptions(int64_t id, Scene* scene, const json& p)
		{
			RayOptions opt;
			opt.rigidBodyColliders = false; // placement wants the visible surface, not a collider
			if (p.contains("maxDistance")) opt.maxDistance = PositiveParam(p, "maxDistance", 10000.0f);
			opt.meshes = p.value("meshes", true);
			opt.physics = p.value("physics", true);
			opt.includeHidden = p.value("includeHidden", false);
			if (p.contains("minNormalY") && !p["minNormalY"].is_null())
			{
				if (!p["minNormalY"].is_number()) throw ParamError("'minNormalY' must be a number (-1..1)");
				opt.minNormalY = p["minNormalY"].get<float>();
			}
			auto readSet = [&](const char* key, std::unordered_set<Entity*>& set)
			{
				if (!p.contains(key) || p[key].is_null()) return;
				if (!p[key].is_array()) throw ParamError(std::string("'") + key + "' must be an array of entity references");
				for (const auto& ref : p[key])
				{
					json wrap = json{ {"entity", ref} }, err;
					Entity* e = RequireEntity(id, scene, wrap, "entity", false, err);
					if (!e) throw ParamError(std::string("'") + key + "': " + (err.contains("error") ? err["error"].value("message", std::string("unknown entity")) : std::string("unknown entity")));
					AddHierarchy(e, set); // an entity's children count as part of it
				}
			};
			readSet("ignore", opt.ignore);
			readSet("only", opt.only);
			return opt;
		}

		// ---- Image readback / encode (capture_view) ----

		float SrgbEncode(float c)
		{
			c = std::clamp(c, 0.0f, 1.0f);
			return c <= 0.0031308f ? c * 12.92f : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
		}

		// One texel of a render target to 0..1 display RGB. False = format
		// not supported.
		bool DecodeTexel(uint32_t fmt, const uint8_t* px, float rgb[3])
		{
			switch (fmt)
			{
			case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: case DXGI_FORMAT_R8G8B8A8_TYPELESS:
				rgb[0] = px[0] / 255.0f; rgb[1] = px[1] / 255.0f; rgb[2] = px[2] / 255.0f; return true;
			case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: case DXGI_FORMAT_B8G8R8A8_TYPELESS:
			case DXGI_FORMAT_B8G8R8X8_UNORM: case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: case DXGI_FORMAT_B8G8R8X8_TYPELESS:
				rgb[0] = px[2] / 255.0f; rgb[1] = px[1] / 255.0f; rgb[2] = px[0] / 255.0f; return true;
			case DXGI_FORMAT_R10G10B10A2_UNORM: case DXGI_FORMAT_R10G10B10A2_TYPELESS:
			{
				uint32_t v; memcpy(&v, px, 4);
				rgb[0] = (v & 1023u) / 1023.0f; rgb[1] = ((v >> 10) & 1023u) / 1023.0f; rgb[2] = ((v >> 20) & 1023u) / 1023.0f; return true;
			}
			case DXGI_FORMAT_R16G16B16A16_FLOAT:
			{
				uint16_t h[3]; memcpy(h, px, 6);
				for (int i = 0; i < 3; ++i) rgb[i] = SrgbEncode(DirectX::PackedVector::XMConvertHalfToFloat(h[i]));
				return true;
			}
			case DXGI_FORMAT_R11G11B10_FLOAT:
			{
				DirectX::PackedVector::XMFLOAT3PK pk; memcpy(&pk, px, 4);
				DirectX::XMFLOAT3 f; DirectX::XMStoreFloat3(&f, DirectX::PackedVector::XMLoadFloat3PK(&pk));
				rgb[0] = SrgbEncode(f.x); rgb[1] = SrgbEncode(f.y); rgb[2] = SrgbEncode(f.z); return true;
			}
			case DXGI_FORMAT_R32G32B32A32_FLOAT:
			{
				float f[3]; memcpy(f, px, 12);
				for (int i = 0; i < 3; ++i) rgb[i] = SrgbEncode(f[i]);
				return true;
			}
			default: return false;
			}
		}

		uint32_t TexelBytes(uint32_t fmt)
		{
			switch (fmt)
			{
			case DXGI_FORMAT_R16G16B16A16_FLOAT: return 8;
			case DXGI_FORMAT_R32G32B32A32_FLOAT: return 16;
			default: return 4;
			}
		}

		// Box-filtered downscale to BGR8 (what WIC's JPEG/PNG encoders take).
		// Each output pixel averages up to 4x4 samples spread over its block.
		bool DownscaleToBgr(const std::vector<uint8_t>& src, uint32_t fmt, int w, int h, int ow, int oh, std::vector<uint8_t>& out, std::string& err)
		{
			const uint32_t bpp = TexelBytes(fmt);
			if (src.size() < (size_t)w * h * bpp) { err = "render target readback was short"; return false; }
			float probe[3];
			if (!DecodeTexel(fmt, src.data(), probe)) { err = "unsupported render target format " + std::to_string(fmt); return false; }
			out.resize((size_t)ow * oh * 3);
			for (int y = 0; y < oh; ++y)
			{
				const int y0 = (int)((int64_t)y * h / oh), y1 = std::max(y0 + 1, (int)((int64_t)(y + 1) * h / oh));
				const int sy = std::max(1, (y1 - y0) / 4);
				for (int x = 0; x < ow; ++x)
				{
					const int x0 = (int)((int64_t)x * w / ow), x1 = std::max(x0 + 1, (int)((int64_t)(x + 1) * w / ow));
					const int sx = std::max(1, (x1 - x0) / 4);
					float acc[3] = { 0, 0, 0 };
					int n = 0;
					for (int yy = y0; yy < y1; yy += sy)
						for (int xx = x0; xx < x1; xx += sx)
						{
							float c[3];
							DecodeTexel(fmt, src.data() + ((size_t)yy * w + xx) * bpp, c);
							acc[0] += c[0]; acc[1] += c[1]; acc[2] += c[2]; ++n;
						}
					uint8_t* o = out.data() + ((size_t)y * ow + x) * 3;
					for (int i = 0; i < 3; ++i)
						o[2 - i] = (uint8_t)std::clamp((int)(acc[i] / n * 255.0f + 0.5f), 0, 255); // BGR
				}
			}
			return true;
		}

		bool EncodeWic(const std::vector<uint8_t>& bgr, int w, int h, bool jpeg, float quality, std::vector<uint8_t>& out, std::string& err)
		{
			const HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
			const bool coOwned = SUCCEEDED(coHr);
			bool ok = false;
			IWICImagingFactory* factory = nullptr;
			IStream* stream = nullptr;
			IWICBitmapEncoder* encoder = nullptr;
			IWICBitmapFrameEncode* frame = nullptr;
			IPropertyBag2* props = nullptr;
			do
			{
				if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) { err = "WIC factory unavailable"; break; }
				if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) { err = "stream allocation failed"; break; }
				if (FAILED(factory->CreateEncoder(jpeg ? GUID_ContainerFormatJpeg : GUID_ContainerFormatPng, nullptr, &encoder))) { err = "encoder unavailable"; break; }
				if (FAILED(encoder->Initialize(stream, WICBitmapEncoderNoCache))) { err = "encoder init failed"; break; }
				if (FAILED(encoder->CreateNewFrame(&frame, &props))) { err = "frame creation failed"; break; }
				if (jpeg && props)
				{
					PROPBAG2 opt = {};
					wchar_t name[] = L"ImageQuality";
					opt.pstrName = name;
					VARIANT v;
					VariantInit(&v);
					v.vt = VT_R4;
					v.fltVal = std::clamp(quality, 0.05f, 1.0f);
					props->Write(1, &opt, &v);
				}
				if (FAILED(frame->Initialize(props))) { err = "frame init failed"; break; }
				if (FAILED(frame->SetSize((UINT)w, (UINT)h))) { err = "SetSize failed"; break; }
				WICPixelFormatGUID pf = GUID_WICPixelFormat24bppBGR;
				if (FAILED(frame->SetPixelFormat(&pf)) || pf != GUID_WICPixelFormat24bppBGR) { err = "encoder rejected 24bpp BGR"; break; }
				if (FAILED(frame->WritePixels((UINT)h, (UINT)w * 3, (UINT)bgr.size(), const_cast<BYTE*>(bgr.data())))) { err = "WritePixels failed"; break; }
				if (FAILED(frame->Commit()) || FAILED(encoder->Commit())) { err = "encode commit failed"; break; }
				HGLOBAL mem = nullptr;
				if (FAILED(GetHGlobalFromStream(stream, &mem))) { err = "stream read failed"; break; }
				STATSTG stat = {};
				stream->Stat(&stat, STATFLAG_NONAME);
				const size_t size = (size_t)stat.cbSize.QuadPart;
				const void* data = GlobalLock(mem);
				if (!data) { err = "stream lock failed"; break; }
				out.assign((const uint8_t*)data, (const uint8_t*)data + size);
				GlobalUnlock(mem);
				ok = true;
			} while (false);
			if (props) props->Release();
			if (frame) frame->Release();
			if (encoder) encoder->Release();
			if (stream) stream->Release();
			if (factory) factory->Release();
			if (coOwned) CoUninitialize();
			return ok;
		}

		// D3D11 only (main thread): copy the frame into a mip-chained texture,
		// GenerateMips (GPU box filter), and read back just the mip whose longest
		// side is the smallest still >= maxSize - 1/16..1/64 of a 4K readback.
		// False = use the full-resolution path (other backends, odd formats).
		bool ReadbackDownsampledD3D11(ITexture2D* src, int maxSize, std::vector<uint8_t>& out, uint32_t& fmt, int& w, int& h)
		{
			IGraphicsDevice* gd = g_pEnv ? g_pEnv->_graphicsDevice : nullptr;
			if (!gd || gd->GetBackend() != GraphicsBackend::D3D11 || !src) return false;
			auto* dev = static_cast<ID3D11Device*>(gd->GetNativeDevice());
			auto* ctx = static_cast<ID3D11DeviceContext*>(gd->GetNativeDeviceContext());
			auto* srcTex = static_cast<ID3D11Texture2D*>(src->GetNativePtr());
			if (!dev || !ctx || !srcTex) return false;
			D3D11_TEXTURE2D_DESC sd;
			srcTex->GetDesc(&sd);
			if (sd.SampleDesc.Count != 1 || sd.ArraySize != 1) return false;
			UINT support = 0;
			if (FAILED(dev->CheckFormatSupport(sd.Format, &support)) || !(support & D3D11_FORMAT_SUPPORT_MIP_AUTOGEN)) return false;

			UINT readMip = 0;
			while (readMip < 12 && (int)std::max(sd.Width >> (readMip + 1), sd.Height >> (readMip + 1)) >= maxSize) ++readMip;
			if (readMip == 0) return false; // already small: the plain path is as cheap

			D3D11_TEXTURE2D_DESC md = {};
			md.Width = sd.Width; md.Height = sd.Height; md.MipLevels = readMip + 1; md.ArraySize = 1;
			md.Format = sd.Format; md.SampleDesc.Count = 1; md.Usage = D3D11_USAGE_DEFAULT;
			md.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
			md.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
			ID3D11Texture2D* mipTex = nullptr;
			ID3D11ShaderResourceView* srv = nullptr;
			ID3D11Texture2D* staging = nullptr;
			bool ok = false;
			do
			{
				if (FAILED(dev->CreateTexture2D(&md, nullptr, &mipTex))) break;
				if (FAILED(dev->CreateShaderResourceView(mipTex, nullptr, &srv))) break;
				D3D11_TEXTURE2D_DESC stg = {};
				stg.Width = std::max(1u, sd.Width >> readMip); stg.Height = std::max(1u, sd.Height >> readMip);
				stg.MipLevels = 1; stg.ArraySize = 1; stg.Format = sd.Format; stg.SampleDesc.Count = 1;
				stg.Usage = D3D11_USAGE_STAGING; stg.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				if (FAILED(dev->CreateTexture2D(&stg, nullptr, &staging))) break;

				gd->Lock(); // other threads may use the immediate context
				ctx->CopySubresourceRegion(mipTex, 0, 0, 0, 0, srcTex, 0, nullptr);
				ctx->GenerateMips(srv);
				ctx->CopySubresourceRegion(staging, 0, 0, 0, 0, mipTex, readMip, nullptr);
				D3D11_MAPPED_SUBRESOURCE map = {};
				const HRESULT hr = ctx->Map(staging, 0, D3D11_MAP_READ, 0, &map);
				if (SUCCEEDED(hr))
				{
					const uint32_t bpp = TexelBytes((uint32_t)sd.Format);
					const size_t row = (size_t)stg.Width * bpp;
					out.resize(row * stg.Height);
					for (UINT y = 0; y < stg.Height; ++y)
						memcpy(out.data() + row * y, (const uint8_t*)map.pData + (size_t)map.RowPitch * y, row);
					ctx->Unmap(staging, 0);
				}
				gd->Unlock();
				if (FAILED(hr)) break;
				fmt = (uint32_t)sd.Format; w = (int)stg.Width; h = (int)stg.Height;
				ok = true;
			} while (false);
			if (staging) staging->Release();
			if (srv) srv->Release();
			if (mipTex) mipTex->Release();
			return ok;
		}

		std::string Base64(const std::vector<uint8_t>& data)
		{
			static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
			std::string s;
			s.reserve((data.size() + 2) / 3 * 4);
			size_t i = 0;
			for (; i + 2 < data.size(); i += 3)
			{
				const uint32_t v = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
				s += k[(v >> 18) & 63]; s += k[(v >> 12) & 63]; s += k[(v >> 6) & 63]; s += k[v & 63];
			}
			if (i < data.size())
			{
				uint32_t v = data[i] << 16;
				if (i + 1 < data.size()) v |= data[i + 1] << 8;
				s += k[(v >> 18) & 63]; s += k[(v >> 12) & 63];
				s += (i + 1 < data.size()) ? k[(v >> 6) & 63] : '=';
				s += '=';
			}
			return s;
		}

		// Camera look direction <-> the camera's own angles (degrees):
		// lookDir = (-cos p sin y, sin p, -cos p cos y).
		void AnglesFromDirection(math::Vector3 d, float& pitch, float& yaw)
		{
			d.Normalize();
			pitch = asinf(std::clamp(d.y, -1.0f, 1.0f)) * 180.0f / kPi;
			yaw = atan2f(-d.x, -d.z) * 180.0f / kPi;
		}

		math::Vector3 DirectionFromAngles(float pitch, float yaw)
		{
			const float p = pitch * kPi / 180.0f, y = yaw * kPi / 180.0f;
			return math::Vector3(-cosf(p) * sinf(y), sinf(p), -cosf(p) * cosf(y));
		}

		// ---- Input injection (D2) ----

		int KeyFromJson(const json& k)
		{
			if (k.is_number_integer())
			{
				const int vk = k.get<int>();
				if (vk <= 0 || vk > 0xFE) throw ParamError("virtual-key codes are 1..254");
				return vk;
			}
			if (!k.is_string() || k.get<std::string>().empty()) throw ParamError("'key' must be a key name (\"W\", \"Space\", \"Shift\", \"F1\", ...) or a virtual-key code");
			std::string s = k.get<std::string>();
			if (s.size() == 1)
			{
				const char c = (char)std::toupper((unsigned char)s[0]);
				if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return c;
			}
			std::string l = s;
			std::transform(l.begin(), l.end(), l.begin(), [](unsigned char c) { return (char)std::tolower(c); });
			static const std::pair<const char*, int> kNames[] = {
				{ "space", VK_SPACE }, { "enter", VK_RETURN }, { "return", VK_RETURN }, { "escape", VK_ESCAPE }, { "esc", VK_ESCAPE },
				{ "tab", VK_TAB }, { "backspace", VK_BACK }, { "delete", VK_DELETE }, { "insert", VK_INSERT },
				{ "shift", VK_SHIFT }, { "lshift", VK_LSHIFT }, { "rshift", VK_RSHIFT },
				{ "ctrl", VK_CONTROL }, { "control", VK_CONTROL }, { "lctrl", VK_LCONTROL }, { "rctrl", VK_RCONTROL },
				{ "alt", VK_MENU }, { "lalt", VK_LMENU }, { "ralt", VK_RMENU },
				{ "up", VK_UP }, { "down", VK_DOWN }, { "left", VK_LEFT }, { "right", VK_RIGHT },
				{ "home", VK_HOME }, { "end", VK_END }, { "pageup", VK_PRIOR }, { "pagedown", VK_NEXT },
				{ "capslock", VK_CAPITAL }, { "minus", VK_OEM_MINUS }, { "plus", VK_OEM_PLUS }, { "comma", VK_OEM_COMMA }, { "period", VK_OEM_PERIOD },
			};
			for (const auto& [name, vk] : kNames) if (l == name) return vk;
			if (l.size() >= 2 && l[0] == 'f')
			{
				const int n = atoi(l.c_str() + 1);
				if (n >= 1 && n <= 24) return VK_F1 + n - 1;
			}
			if (l.rfind("numpad", 0) == 0 && l.size() == 7 && std::isdigit((unsigned char)l[6])) return VK_NUMPAD0 + (l[6] - '0');
			throw ParamError("unknown key '" + s + "'");
		}

		int MouseButtonFromJson(const json& e)
		{
			const std::string b = e.value("button", std::string("left"));
			if (b == "left") return VK_LBUTTON;
			if (b == "right") return VK_RBUTTON;
			if (b == "middle") return VK_MBUTTON;
			throw ParamError("'button' must be left, right or middle");
		}

		struct InputStep
		{
			enum Kind { KeyDown, KeyUp, MouseMove, MouseMoveTo, MouseDown, MouseUp, Wheel, Char, WaitFrames, WaitMs } kind;
			int code = 0;               // vk / button / char / frames / ms
			float x = 0.0f, y = 0.0f;   // move deltas, positions, wheel delta
			bool hasPos = false;
		};

		// Validates and expands `events` (hold / click / text become primitive
		// steps) before anything is sent.
		std::vector<InputStep> ParseInputEvents(const json& events)
		{
			if (!events.is_array() || events.empty()) throw ParamError("'events' must be a non-empty array");
			if (events.size() > 2000) throw ParamError("at most 2000 events per call");
			std::vector<InputStep> out;
			auto waitOf = [](const json& e, std::vector<InputStep>& o, int defFrames)
			{
				if (e.contains("ms")) { InputStep w{ InputStep::WaitMs }; w.code = std::clamp(e["ms"].get<int>(), 0, 60000); o.push_back(w); }
				else { InputStep w{ InputStep::WaitFrames }; w.code = std::clamp(e.value("frames", defFrames), 0, 3600); o.push_back(w); }
			};
			for (const auto& e : events)
			{
				if (!e.is_object() || !e.contains("type") || !e["type"].is_string()) throw ParamError("each event needs a 'type'");
				const std::string t = e["type"].get<std::string>();
				if (t == "keyDown" || t == "keyUp")
				{
					InputStep s{ t == "keyDown" ? InputStep::KeyDown : InputStep::KeyUp };
					s.code = KeyFromJson(e.value("key", json()));
					out.push_back(s);
				}
				else if (t == "hold" || t == "press")
				{
					// Down, keep it held for frames/ms (default 1 frame), up.
					const int vk = KeyFromJson(e.value("key", json()));
					InputStep d{ InputStep::KeyDown }; d.code = vk; out.push_back(d);
					waitOf(e, out, 1);
					InputStep u{ InputStep::KeyUp }; u.code = vk; out.push_back(u);
				}
				else if (t == "mouseMove")
				{
					InputStep s{ InputStep::MouseMove };
					s.x = e.value("dx", 0.0f); s.y = e.value("dy", 0.0f);
					out.push_back(s);
				}
				else if (t == "mouseMoveTo")
				{
					InputStep s{ InputStep::MouseMoveTo };
					if (!e.contains("x") || !e.contains("y")) throw ParamError("mouseMoveTo needs x and y (window pixels)");
					s.x = e["x"].get<float>(); s.y = e["y"].get<float>();
					out.push_back(s);
				}
				else if (t == "mouseDown" || t == "mouseUp" || t == "click")
				{
					const int b = MouseButtonFromJson(e);
					InputStep d{ InputStep::MouseDown }; d.code = b;
					if (e.contains("x") && e.contains("y")) { d.x = e["x"].get<float>(); d.y = e["y"].get<float>(); d.hasPos = true; }
					if (t != "mouseUp") out.push_back(d);
					if (t == "click") waitOf(e, out, 1);
					if (t != "mouseDown") { InputStep u{ InputStep::MouseUp }; u.code = b; out.push_back(u); }
				}
				else if (t == "wheel")
				{
					InputStep s{ InputStep::Wheel }; s.x = e.value("delta", 1.0f); out.push_back(s);
				}
				else if (t == "text")
				{
					const std::wstring w = Utf8ToWide(e.value("text", std::string()));
					if (w.empty()) throw ParamError("text event needs 'text'");
					for (wchar_t c : w) { InputStep s{ InputStep::Char }; s.code = (int)c; out.push_back(s); }
				}
				else if (t == "wait")
					waitOf(e, out, 1);
				else
					throw ParamError("unknown event type '" + t + "' (keyDown, keyUp, hold, mouseMove, mouseMoveTo, mouseDown, mouseUp, click, wheel, text, wait)");
			}
			return out;
		}

		// ---- Scene validation (D2) ----

		bool Finite3(const math::Vector3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

		struct Issue { const char* severity; const char* code; Entity* entity; std::string message; };

		int SeverityRank(const std::string& s) { return s == "error" ? 2 : s == "warning" ? 1 : 0; }

		// Asset reference health for one engine resource path (empty = none).
		// Returns "" when fine, else a reason.
		std::string CheckAssetPath(const fs::path& resourcePath)
		{
			const std::string ref = resourcePath.generic_string();
			if (ref.empty()) return "unsaved (in-memory, no asset file)";
			AssetRef a;
			std::string err;
			if (!ResolveAssetRef(ref, nullptr, false, a, err)) return "unresolvable reference '" + ref + "': " + err;
			if (!AssetExists(a)) return "file missing: '" + ref + "'";
			return std::string();
		}

		// ---- Game code (E1) ----

		fs::path GameCodeDir()
		{
			IEditorContext* ctx = Ctx();
			const std::string folder = ctx ? ctx->GetProjectFolderPath() : std::string();
			return folder.empty() ? fs::path() : fs::weakly_canonical(fs::path(folder) / "Code");
		}

		bool IsBuildOutputDir(const std::wstring& name)
		{
			static const wchar_t* kSkip[] = { L"x64", L"x86", L"Win32", L".vs", L"Debug", L"Release", L"obj", L"bin", L"HotReload" };
			for (const wchar_t* s : kSkip) if (_wcsicmp(name.c_str(), s) == 0) return true;
			return false;
		}

		enum class SourceKind { None, Compile, Include };
		SourceKind SourceKindOf(const fs::path& p)
		{
			const std::wstring e = p.extension().wstring();
			for (const wchar_t* x : { L".cpp", L".cc", L".cxx", L".c" }) if (_wcsicmp(e.c_str(), x) == 0) return SourceKind::Compile;
			for (const wchar_t* x : { L".h", L".hpp", L".hxx", L".inl" }) if (_wcsicmp(e.c_str(), x) == 0) return SourceKind::Include;
			return SourceKind::None;
		}

		// A path under the project's Code/ folder (absolute, "Code/...", or
		// relative to Code/). Never into build-output folders.
		bool ResolveGameSourcePath(const std::string& input, fs::path& out, std::string& err)
		{
			const fs::path code = GameCodeDir();
			if (code.empty()) { err = "no project is open"; return false; }
			fs::path p = fs::path(Utf8ToWide(input));
			if (!p.is_absolute())
			{
				const auto first = p.begin() != p.end() ? p.begin()->wstring() : std::wstring();
				p = (_wcsicmp(first.c_str(), L"Code") == 0) ? code.parent_path() / p : code / p;
			}
			p = fs::weakly_canonical(p);
			const std::wstring c = code.wstring(), s = p.wstring();
			if (s.size() <= c.size() || _wcsnicmp(s.c_str(), c.c_str(), c.size()) != 0 || (s[c.size()] != L'\\' && s[c.size()] != L'/'))
			{
				err = "'" + input + "' is not inside the project's Code folder (" + code.string() + ")";
				return false;
			}
			for (const auto& part : fs::relative(p, code))
				if (IsBuildOutputDir(part.wstring())) { err = "'" + input + "' is inside a build-output folder"; return false; }
			out = p;
			return true;
		}

		std::string CodeRelative(const fs::path& p)
		{
			std::error_code ec;
			const fs::path rel = fs::relative(p, GameCodeDir(), ec);
			return ec || rel.empty() ? p.string() : rel.generic_string();
		}

		// The .vcxproj that owns `file`: the nearest one walking up from its
		// folder to Code/, else the first under Code/.
		fs::path OwningVcxproj(const fs::path& file)
		{
			const fs::path code = GameCodeDir();
			std::error_code ec;
			for (fs::path dir = file.parent_path(); !dir.empty(); dir = dir.parent_path())
			{
				for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
					if (_wcsicmp(it->path().extension().c_str(), L".vcxproj") == 0) return it->path();
				if (_wcsicmp(dir.wstring().c_str(), code.wstring().c_str()) == 0 || dir == dir.root_path()) break;
			}
			for (fs::recursive_directory_iterator it(code, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
				if (_wcsicmp(it->path().extension().c_str(), L".vcxproj") == 0) return it->path();
			return {};
		}

		// Adds `file` to the project as ClCompile / ClInclude unless it is already
		// listed. Text-level edit next to existing items of the same kind.
		bool AddToVcxproj(const fs::path& vcxproj, const fs::path& file, bool& added, std::string& err)
		{
			added = false;
			const SourceKind kind = SourceKindOf(file);
			if (kind == SourceKind::None) return true;
			std::ifstream in(vcxproj, std::ios::binary);
			if (!in) { err = "cannot read " + vcxproj.string(); return false; }
			std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			in.close();
			const std::string nl = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
			std::string rel = fs::relative(file, vcxproj.parent_path()).string();
			std::replace(rel.begin(), rel.end(), '/', '\\');
			auto lower = [](std::string s) { std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); }); return s; };
			if (lower(text).find(lower("Include=\"" + rel + "\"")) != std::string::npos) return true;

			const std::string tag = kind == SourceKind::Compile ? "ClCompile" : "ClInclude";
			const std::string item = "<" + tag + " Include=\"" + rel + "\" />";
			const size_t at = text.find("<" + tag + " Include=");
			if (at != std::string::npos)
			{
				const size_t lineStart = text.rfind('\n', at);
				const std::string indent = text.substr(lineStart == std::string::npos ? 0 : lineStart + 1, at - (lineStart == std::string::npos ? 0 : lineStart + 1));
				text.insert(lineStart == std::string::npos ? 0 : lineStart + 1, indent + item + nl);
			}
			else
			{
				const size_t end = text.rfind("</Project>");
				if (end == std::string::npos) { err = "unexpected project file format: " + vcxproj.string(); return false; }
				text.insert(end, "  <ItemGroup>" + nl + "    " + item + nl + "  </ItemGroup>" + nl);
			}
			std::ofstream out(vcxproj, std::ios::binary | std::ios::trunc);
			if (!out) { err = "cannot write " + vcxproj.string(); return false; }
			out << text;
			added = true;
			return true;
		}

		// MSBuild/cl/link diagnostics from Build/GameBuild.log.
		struct BuildDiagnostics { json errors = json::array(); json warnings = json::array(); size_t errorCount = 0, warningCount = 0; std::vector<std::string> tail; };

		BuildDiagnostics ParseBuildLog(const fs::path& log)
		{
			BuildDiagnostics d;
			std::ifstream in(log, std::ios::binary);
			std::string line;
			std::set<std::string> seen;
			// file(line[,col]): error|warning CODE: message [project]
			static const std::regex kFileDiag(R"(^\s*(.+?)\((\d+)(?:,(\d+))?\)\s*:\s*(fatal error|error|warning)\s+([A-Za-z]+\d+)\s*:\s*(.*?)(?:\s+\[[^\]]*\])?\s*$)");
			// tool or file: error|warning CODE: message [project]  (link, MSBuild)
			static const std::regex kToolDiag(R"(^\s*(.+?)\s*:\s*(fatal error|error|warning)\s+([A-Za-z]+\d+)\s*:\s*(.*?)(?:\s+\[[^\]]*\])?\s*$)");
			const fs::path code = GameCodeDir();
			while (std::getline(in, line))
			{
				if (!line.empty() && line.back() == '\r') line.pop_back();
				for (char& ch : line) if ((unsigned char)ch >= 0x80) ch = '?';
				d.tail.push_back(line);
				if (d.tail.size() > 40) d.tail.erase(d.tail.begin());
				std::smatch m;
				json diag;
				bool isError = false;
				if (std::regex_match(line, m, kFileDiag))
				{
					std::string file = m[1].str();
					std::error_code ec;
					const fs::path rel = fs::relative(fs::path(file), code, ec);
					if (!ec && !rel.empty() && rel.native()[0] != L'.') file = rel.generic_string();
					isError = m[4].str() != "warning";
					diag = json{ {"file", file}, {"line", std::stoi(m[2].str())}, {"code", m[5].str()}, {"message", m[6].str()} };
					if (m[3].matched) diag["column"] = std::stoi(m[3].str());
				}
				else if (std::regex_match(line, m, kToolDiag))
				{
					isError = m[2].str() != "warning";
					diag = json{ {"source", m[1].str()}, {"code", m[3].str()}, {"message", m[4].str()} };
				}
				else
					continue;
				if (!seen.insert(diag.dump()).second) continue; // MSBuild repeats some lines
				if (isError) { ++d.errorCount; if (d.errors.size() < 50) d.errors.push_back(diag); }
				else { ++d.warningCount; if (d.warnings.size() < 20) d.warnings.push_back(diag); }
			}
			return d;
		}

		// ---- Scene environment ----

		// WeatherPresetId (HexEngine.WeatherPlugin/WeatherTypes.hpp) by value.
		const char* const kWeatherPresets[] = { "Custom", "Clear", "Overcast", "Rain", "HeavyRain", "Storm", "Thunderstorm", "Snow", "Blizzard", "Hot", "Sandstorm" };

		Entity* FindEntityWithComponent(Scene* scene, const char* component)
		{
			std::vector<EntityId> ids;
			scene->GetLiveEntityIds(ids);
			for (const EntityId& eid : ids)
			{
				Entity* e = scene->TryGetEntity(eid);
				if (e && !e->IsPendingDeletion() && FindComponent(e, component)) return e;
			}
			return nullptr;
		}

		json PickFields(const json& all, std::initializer_list<const char*> keys)
		{
			json out = json::object();
			for (const char* k : keys) if (all.contains(k)) out[k] = all[k];
			return out;
		}

		json EnvironmentJson(Scene* scene)
		{
			json env = json::object();
			env["sceneName"] = WideToUtf8(scene->GetName());

			if (Entity* dn = FindEntityWithComponent(scene, "DayNightCycleComponent"))
			{
				const json f = SerializeComponent(FindComponent(dn, "DayNightCycleComponent"));
				json t = PickFields(f, { "_currentTimeHours", "_simulateCycle", "_dayLengthSeconds", "_nightLengthSeconds", "_sunYawDegrees" });
				t["entity"] = dn->GetName();
				env["timeOfDay"] = t;
			}
			else
				env["timeOfDay"] = nullptr;

			if (Entity* we = FindEntityWithComponent(scene, "WeatherControllerComponent"))
			{
				const json f = SerializeComponent(FindComponent(we, "WeatherControllerComponent"));
				const int pid = f.value("_globalPresetId", 0);
				env["weather"] = json{ {"entity", we->GetName()}, {"preset", pid >= 0 && pid < (int)std::size(kWeatherPresets) ? kWeatherPresets[pid] : "?"},
					{"transitionSeconds", f.value("_defaultTransitionSeconds", 0.0f)}, {"randomCycling", f.value("_randomCyclingEnabled", false)} };
			}
			else
				env["weather"] = g_pEnv->_classRegistry->Find("WeatherControllerComponent") ? json(nullptr) : json("weather plugin not loaded");

			if (Entity* sun = FindEntityWithComponent(scene, "DirectionalLight"))
			{
				const math::Vector3 dir = math::Vector3::Transform(math::Vector3(0.0f, 0.0f, -1.0f), sun->GetRotation());
				env["sun"] = json{ {"entity", sun->GetName()}, {"direction", Vec3Json(dir)} };
			}
			else
				env["sun"] = nullptr;

			env["sea"] = json{ {"hasOcean", scene->HasOcean()}, {"seaLevel", scene->GetSeaLevel()} };
			const OceanSettings& o = scene->GetOcean();
			auto v4 = [](const math::Vector4& v) { return json::array({ v.x, v.y, v.z, v.w }); };
			env["ocean"] = json{ {"shallowColour", v4(o.shallowColour)}, {"deepColour", v4(o.deepColour)}, {"fresnelPow", o.fresnelPow},
				{"shoreFadeStrength", o.shoreFadeStrength}, {"fadeFactor", o.fadeFactor}, {"reflectionStrength", o.reflectionStrength},
				{"reflectionNearDistance", o.reflectionNearDistance}, {"reflectionFarDistance", o.reflectionFarDistance} };

			// The look-defining console variables (saved with the scene).
			json cv = json::object();
			static const char* kPrefixes[] = { "env_", "r_fog", "r_cloud", "r_sky", "r_exposure", "r_moon", "r_star", "r_ocean", "r_atmosphere" };
			for (HVar* v = g_hvars; v != nullptr; v = v->_next)
				for (const char* pre : kPrefixes)
					if (v->_name.rfind(pre, 0) == 0) { cv[v->_name] = HVarValueJson(v->_type, v->_val); break; }
			env["cvars"] = cv;
			env["note"] = "set_environment changes these in one call. Water renders on surfaces whose material is based on EngineData.Materials/Water.hmat (create_material base=...); seaLevel only moves the sea plane used by underwater/ocean queries.";
			return env;
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
			if (_build.thread.joinable()) _build.thread.join(); // waits for a running MSBuild
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
					{"simulation", SimulationJson()},
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

			Reg("validate_current_scene", R, Main, Batch, [this](int64_t id, const json& p) -> json {
				auto scene = PrimaryUserScene();
				if (!scene) return NoScene(id);
				return MakeResult(id, ValidateScene(id, scene.get(), p, false));
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

			Reg("find_missing_references", R, Main, Batch, [this](int64_t id, const json& p) -> json {
				auto scene = PrimaryUserScene();
				if (!scene) return NoScene(id);
				return MakeResult(id, ValidateScene(id, scene.get(), p, true));
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
				// Register the clone (and its cloned children) with every PVS right
				// away. A bulk clone/edit session left skinned clones - and their
				// source - missing from the camera PVS until something flushed them
				// (a DoNotRender toggle, i.e. this same flush, was the observed cure).
				scene->FlushPVS(clone, false);
				for (Entity* child : clone->GetChildren())
					if (child != nullptr) scene->FlushPVS(child, false);
				scene->FlushPVS(src, false);
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
				// Re-file the entity under its new material batch now (see clone_entity).
				scene->FlushPVS(e, false);
				MarkPvsDirty();
				return MakeResult(id, json{ {"entity", EntityBrief(e)}, {"material", ref.ref},
					{"previous", previous && !previous->GetFileSystemPath().empty() ? json(previous->GetFileSystemPath().generic_string()) : json(nullptr)} });
			});

			// Recenter a static mesh asset so its bounds centre sits at the model
			// origin (fixes kit meshes authored kilometres from their pivot). The
			// live shared mesh is updated in place (GPU buffers rebuilt) and saved;
			// every entity in the open scene that uses it is moved by R*S*offset
			// (children counter-shifted) so nothing moves in the world, and its
			// bounds, PVS entry and mesh/box collider are refreshed. Prefab files in
			// Data/Prefabs that reference the mesh get the same compensation.
			Reg("recenter_mesh", W, Main, Solo, [this](int64_t id, const json& p) -> json {
				auto scene = PrimaryUserScene();
				IEditorContext* ctx = Ctx();
				FileSystem* projectFs = ctx ? ctx->GetProjectFileSystem() : nullptr;
				AssetRef ref;
				std::string msg;
				if (!ResolveAssetRef(RequireString(p, "mesh"), ".hmesh", false, ref, msg))
					return MakeError(id, ErrorCode::InvalidParams, msg);
				if (!AssetExists(ref))
					return MakeError(id, ErrorCode::NotAvailable, "no mesh at '" + ref.ref + "'");
				const bool dryRun = p.value("dryRun", false);
				const bool compensateScene = p.value("compensateScene", true);
				const bool updatePrefabs = p.value("updatePrefabs", true);

				auto mesh = Mesh::Create(fs::path(Utf8ToWide(ref.ref)));
				if (!mesh) return MakeError(id, ErrorCode::Internal, "'" + ref.ref + "' could not be loaded");
				if (mesh->HasAnimations())
					return MakeError(id, ErrorCode::InvalidParams, "'" + ref.ref + "' is animated - recentering would break its skin/bone binding");
				if (mesh->GetVertices().empty())
					return MakeError(id, ErrorCode::NotAvailable, "'" + ref.ref + "' has no CPU-side vertices to move");

				const math::Vector3 center = mesh->GetAABB().Center;
				json result = json{ {"mesh", ref.ref}, {"offset", json::array({ center.x, center.y, center.z })} };
				if (center.LengthSquared() <= 1e-6f)
				{
					result["note"] = "already centred";
					return MakeResult(id, result);
				}

				// Mesh-key match for prefab JSON: "<fs>.<path>" normalised.
				auto normKey = [](std::string s) {
					std::replace(s.begin(), s.end(), '\\', '/');
					std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
					const size_t dot = s.find('.');
					const size_t slash = s.find('/');
					if (dot != std::string::npos && (slash == std::string::npos || dot < slash)) s = s.substr(dot + 1);
					return s;
				};
				const std::string wantKey = normKey(ref.ref);

				// Live users in the open scene.
				std::vector<StaticMeshComponent*> users;
				if (scene)
				{
					std::vector<StaticMeshComponent*> all;
					scene->GetComponents<StaticMeshComponent>(all);
					for (auto* smc : all)
						if (smc && smc->GetEntity() && !smc->GetEntity()->IsPendingDeletion() && smc->GetMesh() == mesh)
							users.push_back(smc);
				}
				json entities = json::array();
				for (auto* smc : users) entities.push_back(smc->GetEntity()->GetName());
				result["sceneEntities"] = entities;

				if (dryRun)
				{
					result["dryRun"] = true;
					return MakeResult(id, result);
				}

				if (!mesh->TranslateGeometry(-center))
					return MakeError(id, ErrorCode::Internal, "TranslateGeometry failed (dynamic mesh, or GPU buffer recreation failed)");
				mesh->Save();

				// Compensate placed entities: local' = local + R * (S * c); children -= c.
				if (compensateScene && scene)
				{
					for (auto* smc : users)
					{
						Entity* e = smc->GetEntity();
						const math::Vector3 posBefore = e->GetPosition();
						const math::Quaternion rotBefore = e->GetRotation();
						const math::Vector3 scaleBefore = e->GetScale();
						const math::Vector3 shift = math::Vector3::Transform(scaleBefore * center, rotBefore);
						e->SetPosition(posBefore + shift);
						if (ctx) ctx->RecordEntityTransformChange(e, posBefore, rotBefore, scaleBefore);
						for (Entity* child : e->GetChildren())
						{
							if (child == nullptr) continue;
							const math::Vector3 cp = child->GetPosition(), cs = child->GetScale();
							const math::Quaternion cr = child->GetRotation();
							child->SetPosition(cp - center);
							if (ctx) ctx->RecordEntityTransformChange(child, cp, cr, cs);
						}
						e->SetAABB(mesh->GetAABB());
						e->SetOBB(mesh->GetOBB());
						e->RecalculateBoundingVolumes(mesh->GetAABB());
						scene->NotifyStaticMeshChanged(smc, true, false);
						scene->UpdatePvsSpatialEntriesForEntity(e);
						if (auto* rb = e->GetComponent<RigidBody>(); rb != nullptr)
							rb->RebuildColliderFromMesh();
						scene->FlushPVS(e, false);
					}
					MarkPvsDirty();
				}

				// Prefab files that reference the mesh.
				json prefabs = json::array();
				if (updatePrefabs && projectFs)
				{
					std::error_code ec;
					const fs::path prefabDir = projectFs->GetDataDirectory() / L"Prefabs";
					for (fs::recursive_directory_iterator it(prefabDir, ec), end; !ec && it != end; it.increment(ec))
					{
						if (!it->is_regular_file() || _wcsicmp(it->path().extension().c_str(), L".hprefab") != 0) continue;
						std::ifstream in(it->path());
						json doc = json::parse(in, nullptr, false);
						in.close();
						if (doc.is_discarded() || !doc.contains("entities") || !doc["entities"].is_object()) continue;
						auto& ents = doc["entities"];
						const json hierarchy = doc.value("hierarchy", json::object());
						int changed = 0;
						for (auto& [name, ent] : ents.items())
						{
							if (!ent.contains("components") || !ent["components"].is_array()) continue;
							bool uses = false;
							json* tr = nullptr;
							for (auto& c : ent["components"])
							{
								const std::string cn = c.value("name", std::string());
								if (cn == "Transform") tr = &c;
								if (cn == "StaticMeshComponent" && c.contains("mesh") && c["mesh"].is_object())
									for (auto& [k, v] : c["mesh"].items()) if (normKey(k) == wantKey) uses = true;
							}
							if (!uses || tr == nullptr || !tr->contains("_position")) continue;
							auto& pos = (*tr)["_position"];
							const json rj = tr->value("_rotation", json::array({ 0.0, 0.0, 0.0, 1.0 }));
							const json sj = tr->value("_scale", json::array({ 1.0, 1.0, 1.0 }));
							const math::Quaternion r(rj[0].get<float>(), rj[1].get<float>(), rj[2].get<float>(), rj[3].get<float>());
							const math::Vector3 s(sj[0].get<float>(), sj[1].get<float>(), sj[2].get<float>());
							const math::Vector3 shift = math::Vector3::Transform(s * center, r);
							pos[0] = pos[0].get<float>() + shift.x; pos[1] = pos[1].get<float>() + shift.y; pos[2] = pos[2].get<float>() + shift.z;
							for (auto& [child, parent] : hierarchy.items())
							{
								if (!parent.is_string() || parent.get<std::string>() != name || !ents.contains(child)) continue;
								for (auto& cc : ents[child]["components"])
									if (cc.value("name", std::string()) == "Transform" && cc.contains("_position"))
									{
										auto& cpos = cc["_position"];
										cpos[0] = cpos[0].get<float>() - center.x; cpos[1] = cpos[1].get<float>() - center.y; cpos[2] = cpos[2].get<float>() - center.z;
									}
							}
							++changed;
						}
						if (changed > 0)
						{
							std::ofstream out(it->path(), std::ios::trunc);
							out << doc.dump(2);
							prefabs.push_back(json{ {"prefab", ProjectRelative(it->path())}, {"entities", changed} });
						}
					}
				}
				result["prefabsUpdated"] = prefabs;

				// Other scene files still placing the mesh at its OLD pivot (not edited).
				json otherScenes = json::array();
				if (projectFs)
				{
					std::error_code ec;
					const std::string fileName = fs::path(Utf8ToWide(ref.ref)).filename().string();
					const std::wstring openScene = scene ? fs::path(scene->GetName()).wstring() : std::wstring();
					for (fs::recursive_directory_iterator it(projectFs->GetDataDirectory(), ec), end; !ec && it != end; it.increment(ec))
					{
						if (!it->is_regular_file() || _wcsicmp(it->path().extension().c_str(), L".hscene") != 0) continue;
						if (!openScene.empty() && _wcsicmp(it->path().stem().wstring().c_str(), openScene.c_str()) == 0) continue;
						std::ifstream in(it->path());
						const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
						if (text.find(fileName) != std::string::npos) otherScenes.push_back(ProjectRelative(it->path()));
					}
				}
				result["otherScenesReferencing"] = otherScenes;
				if (!otherScenes.empty())
					result["warning"] = "these scenes still place the mesh at its OLD pivot; open each and move its users by the offset";
				result["note"] = "mesh recentred, saved, and the open scene compensated - save the scene to keep the entity moves";
				return MakeResult(id, result);
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

			// ---- Spatial queries + visual feedback (D1) ----

			Reg("raycast", R, Main, Batch, [](int64_t id, const json& p) -> json {
				auto scene = PrimaryUserScene();
				if (!scene) return NoScene(id);
				math::Vector3 origin, dir, target;
				if (!ReadVec3(p, "origin", origin)) throw ParamError("raycast requires 'origin' [x,y,z]");
				if (ReadVec3(p, "target", target))
				{
					dir = target - origin;
					if (!p.contains("maxDistance")) { json q = p; q["maxDistance"] = std::max(dir.Length(), 1e-4f); return MakeResult(id, RayResultJson(CastRay(scene.get(), origin, dir, ReadRayOptions(id, scene.get(), q)))); }
				}
				else if (!ReadVec3(p, "direction", dir))
					throw ParamError("raycast requires 'direction' [x,y,z] or 'target' [x,y,z]");
				if (dir.LengthSquared() < 1e-12f) throw ParamError("'direction' must be non-zero");
				return MakeResult(id, RayResultJson(CastRay(scene.get(), origin, dir, ReadRayOptions(id, scene.get(), p))));
			});

			Reg("ground_height", R, Main, Batch, [](int64_t id, const json& p) -> json {
				auto scene = PrimaryUserScene();
				if (!scene) return NoScene(id);
				std::vector<std::pair<float, float>> pts;
				if (p.contains("points"))
				{
					if (!p["points"].is_array()) throw ParamError("'points' must be [[x, z], ...]");
					for (const auto& pt : p["points"])
					{
						if (!IsNumericVector(pt) || pt.size() != 2) throw ParamError("each point must be [x, z]");
						pts.push_back({ pt[0].get<float>(), pt[1].get<float>() });
					}
				}
				else
				{
					float xz[2];
					if (!ReadFloats(p, "xz", 2, xz)) throw ParamError("ground_height requires 'points' [[x,z],...] or 'xz' [x,z]");
					pts.push_back({ xz[0], xz[1] });
				}
				if (pts.empty() || pts.size() > 20000) throw ParamError("1..20000 points");
				const float fromY = p.value("fromY", 2000.0f);
				json q = p;
				if (!q.contains("maxDistance")) q["maxDistance"] = fromY + 10000.0f;
				const RayOptions opt = ReadRayOptions(id, scene.get(), q);
				json out = json::array();
				size_t hits = 0;
				const auto t0 = std::chrono::steady_clock::now();
				for (const auto& [x, z] : pts)
				{
					const RayResult r = CastRay(scene.get(), math::Vector3(x, fromY, z), math::Vector3(0.0f, -1.0f, 0.0f), opt);
					if (!r.hit) { out.push_back(json{ {"x", x}, {"z", z}, {"y", nullptr} }); continue; }
					++hits;
					out.push_back(json{ {"x", x}, {"z", z}, {"y", r.position.y}, {"normal", Vec3Json(r.normal)},
						{"entity", r.entity ? json(r.entity->GetName()) : json(nullptr)} });
				}
				const double rayMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
				return MakeResult(id, json{ {"count", pts.size()}, {"hits", hits}, {"points", out}, {"msPerRay", rayMs / (double)pts.size()},
					{"note", "Rays go straight down from fromY and stop at the first surface (canopies and roofs included): pass only / ignore / minNormalY to pick the ground."} });
			});

			// Navigation: bake the scene navmesh (what the editor's NavMesh tool's
			// "Build Scene NavMesh" button does) and query paths on it, so agents
			// can verify that waypoints are reachable.
			Reg("build_navmesh", W, Main, Solo, [](int64_t id, const json& p) -> json {
				auto scene = PrimaryUserScene();
				if (!scene) return NoScene(id);
				auto* nav = g_pEnv->_navMeshProvider;
				if (nav == nullptr) return MakeError(id, ErrorCode::Internal, "no navmesh provider is loaded");

				INavMeshProvider::NavMeshCreationParams params = {};
				params.cs = p.value("cellSize", 0.3f);
				params.ch = p.value("cellHeight", 0.3f);
				params.walkableSlopeAngle = p.value("walkableSlopeAngle", 45.0f);
				params.walkableClimb = p.value("walkableClimb", 2);
				params.walkableHeight = p.value("walkableHeight", 7);
				params.walkableRadius = p.value("walkableRadius", 1);
				params.minRegionArea = p.value("minRegionArea", 8);
				params.mergeRegionArea = std::max(params.minRegionArea, p.value("mergeRegionArea", 20));
				params.maxEdgeLen = 24;
				params.maxSimplificationError = 1.3f;
				params.maxVertsPerPoly = 6;
				params.detailSampleDist = 3.0f;
				params.detailSampleMaxError = 1.0f;
				params.heightBias = params.ch * 0.5f;
				params.borderSize = params.walkableRadius + 3;
				params.width = params.height = params.tileSize = 0;
				params.bmin[0] = params.bmin[1] = params.bmin[2] = 0.0f;
				params.bmax[0] = params.bmax[1] = params.bmax[2] = 0.0f;
				// Optional bake region (world AABB). Without one the whole scene's bounds
				// are used - sky sphere and distant scenery included.
				if (float lo[3], hi[3]; ReadFloats(p, "min", 3, lo) && ReadFloats(p, "max", 3, hi))
				{
					for (int i = 0; i < 3; ++i) { params.bmin[i] = lo[i]; params.bmax[i] = hi[i]; }
				}

				const auto t0 = std::chrono::steady_clock::now();
				NavMeshId navMeshId = 0;
				if (!nav->CreateNavMeshForScene(scene.get(), params, &navMeshId))
					return MakeError(id, ErrorCode::Internal, "navmesh build failed (see get_recent_engine_logs)");
				scene->SetNavMeshId(navMeshId);
				const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
				return MakeResult(id, json{ {"navMeshId", navMeshId}, {"ms", ms},
					{"note", "Live immediately for path queries and agents; save_scene writes the navmesh sidecar."} });
			});

			Reg("find_path", R, Main, Batch, [](int64_t id, const json& p) -> json {
				auto scene = PrimaryUserScene();
				if (!scene) return NoScene(id);
				auto* nav = g_pEnv->_navMeshProvider;
				if (nav == nullptr) return MakeError(id, ErrorCode::Internal, "no navmesh provider is loaded");

				auto readEnd = [&](const char* posKey, const char* entKey, math::Vector3& out) -> bool
				{
					float v[3];
					if (ReadFloats(p, posKey, 3, v)) { out = math::Vector3(v[0], v[1], v[2]); return true; }
					if (p.contains(entKey) && p[entKey].is_string())
					{
						auto* e = scene->GetEntityByName(p[entKey].get<std::string>());
						if (e == nullptr) throw ParamError(std::string("no entity named ") + p[entKey].dump());
						out = e->GetWorldTM().Translation();
						return true;
					}
					return false;
				};
				math::Vector3 from, to;
				if (!readEnd("from", "fromEntity", from) || !readEnd("to", "toEntity", to))
					throw ParamError("find_path needs from:[x,y,z] or fromEntity, and to:[x,y,z] or toEntity");

				INavMeshProvider::PathParams params;
				params.from = from;
				params.to = to;
				const float search = p.value("searchDistance", 50.0f);
				params.searchDistance = math::Vector3(search, p.value("searchHeight", 15.0f), search);
				params.stepSize = p.value("stepSize", 5.0f);
				params.meshId = scene->GetNavMeshId();
				INavMeshProvider::PathResult result;
				nav->FindPath(params, result);

				float length = 0.0f;
				json pts = json::array();
				for (size_t i = 0; i < result.path.size(); ++i)
				{
					if (i > 0) length += (result.path[i] - result.path[i - 1]).Length();
					pts.push_back(Vec3Json(result.path[i]));
				}
				const float endGap = result.path.empty() ? -1.0f : (result.path.back() - to).Length();
				const float startGap = result.path.empty() ? -1.0f : (result.path.front() - from).Length();
				const float tolerance = p.value("tolerance", 1.5f);
				const bool returnPoints = p.value("returnPoints", true);
				return MakeResult(id, json{ {"found", !result.path.empty()},
					{"reaches", !result.path.empty() && endGap <= tolerance},
					{"length", length}, {"straightLine", (to - from).Length()},
					{"startGap", startGap}, {"endGap", endGap}, {"pointCount", result.path.size()},
					{"navMeshId", scene->GetNavMeshId()},
					{"points", returnPoints ? pts : json(nullptr)},
					{"note", "endGap = distance from the path's last point to 'to': a large gap means the target is off the navmesh or unreachable (the path stops at the nearest reachable point)."} });
			});

			Reg("snap_to_ground", W, Main, Batch, [this](int64_t id, const json& p) -> json {
				auto scene = PrimaryUserScene();
				if (!scene) return NoScene(id);
				json err;
				Entity* e = RequireEntity(id, scene.get(), p, "entity", true, err);
				if (!e) return err;
				if (e->GetComponentDerived<Camera>()) throw ParamError("cameras can't be snapped");
				RayOptions opt = ReadRayOptions(id, scene.get(), p);
				AddHierarchy(e, opt.ignore); // never land on yourself
				const float offset = p.value("offset", 0.0f);
				const bool align = p.value("alignToNormal", false);

				const math::Vector3 posBefore = e->GetPosition();
				const math::Quaternion rotBefore = e->GetRotation();
				const math::Vector3 scaleBefore = e->GetScale();
				dx::BoundingBox box;
				HierarchyAABB(e, box);
				const float castHeight = p.value("castHeight", 2.0f);
				const math::Vector3 from(box.Center.x, box.Center.y + box.Extents.y + castHeight, box.Center.z);
				const RayResult r = CastRay(scene.get(), from, math::Vector3(0.0f, -1.0f, 0.0f), opt);
				if (!r.hit)
					return MakeError(id, ErrorCode::NotAvailable, "no surface below '" + e->GetName() + "' (cast from y=" + std::to_string(from.y) + "); try a larger castHeight or maxDistance");

				if (align)
				{
					math::Vector3 n = r.normal;
					if (n.y < 0.0f) n = -n;
					n.Normalize();
					const math::Vector3 up(0.0f, 1.0f, 0.0f);
					math::Vector3 axis = up.Cross(n);
					const float s = axis.Length();
					if (s > 1e-5f)
					{
						axis /= s;
						const math::Quaternion tilt = math::Quaternion::CreateFromAxisAngle(axis, asinf(std::min(s, 1.0f)) * (up.Dot(n) < 0.0f ? -1.0f : 1.0f));
						e->SetRotation(rotBefore * tilt);
					}
					HierarchyAABB(e, box); // bounds change with the tilt
				}
				const float bottom = box.Center.y - box.Extents.y;
				const float dy = r.position.y + offset - bottom;
				e->SetPosition(e->GetPosition() + math::Vector3(0.0f, dy, 0.0f));
				if (IEditorContext* ctx = Ctx()) ctx->RecordEntityTransformChange(e, posBefore, rotBefore, scaleBefore);
				MarkPvsDirty();
				return MakeResult(id, json{ {"entity", EntityBrief(e)}, {"position", Vec3Json(e->GetPosition())}, {"moved", dy},
					{"surface", RayResultJson(r)} });
			});

			Reg("query_region", R, Main, Batch, [](int64_t id, const json& p) -> json {
				auto scene = PrimaryUserScene();
				if (!scene) return NoScene(id);
				math::Vector3 center, mn, mx;
				const bool sphere = ReadVec3(p, "center", center);
				float radius = 0.0f;
				if (sphere)
					radius = PositiveParam(p, "radius", 10.0f);
				else if (!(ReadVec3(p, "min", mn) && ReadVec3(p, "max", mx)))
					throw ParamError("query_region requires 'center' + 'radius' (sphere) or 'min' + 'max' (box)");
				dx::BoundingSphere qs(DirectX::XMFLOAT3(center.x, center.y, center.z), radius);
				dx::BoundingBox qb;
				if (!sphere)
				{
					dx::BoundingBox::CreateFromPoints(qb, DirectX::XMVectorSet(mn.x, mn.y, mn.z, 1), DirectX::XMVectorSet(mx.x, mx.y, mx.z, 1));
					center = math::Vector3(qb.Center.x, qb.Center.y, qb.Center.z);
				}
				const std::string component = p.value("component", std::string());
				const std::string filter = p.value("filter", std::string());
				const bool rootsOnly = p.value("rootsOnly", false);
				const size_t limit = (size_t)std::clamp(p.value("limit", 200), 1, 5000);

				std::vector<EntityId> ids;
				scene->GetLiveEntityIds(ids);
				struct Found { float dist; Entity* e; dx::BoundingBox box; };
				std::vector<Found> found;
				for (const EntityId& eid : ids)
				{
					Entity* e = scene->TryGetEntity(eid);
					if (!e || e->IsPendingDeletion()) continue;
					if (rootsOnly && e->GetParent()) continue;
					if (!filter.empty() && e->GetName().find(filter) == std::string::npos) continue;
					if (!component.empty() && FindComponent(e, component) == nullptr) continue;
					const dx::BoundingBox& b = e->GetWorldAABB();
					if (sphere ? !qs.Intersects(b) : !qb.Intersects(b)) continue;
					// Distance from the query centre to the box (0 inside).
					const math::Vector3 c(b.Center.x, b.Center.y, b.Center.z), ext(b.Extents.x, b.Extents.y, b.Extents.z);
					const math::Vector3 dlt = center - c;
					const math::Vector3 outside(std::max(fabsf(dlt.x) - ext.x, 0.0f), std::max(fabsf(dlt.y) - ext.y, 0.0f), std::max(fabsf(dlt.z) - ext.z, 0.0f));
					found.push_back({ outside.Length(), e, b });
				}
				// Nearest first; ties (e.g. several boxes containing the centre)
				// put the smaller, more specific entity first - not the terrain.
				auto volume = [](const dx::BoundingBox& x) { return x.Extents.x * x.Extents.y * x.Extents.z; };
				std::sort(found.begin(), found.end(), [&](const Found& a, const Found& b) {
					if (a.dist != b.dist) return a.dist < b.dist;
					return volume(a.box) < volume(b.box);
				});
				json list = json::array();
				for (size_t i = 0; i < found.size() && i < limit; ++i)
				{
					const auto& f = found[i];
					json j = EntityBrief(f.e);
					j["distance"] = f.dist;
					j["aabbMin"] = json::array({ f.box.Center.x - f.box.Extents.x, f.box.Center.y - f.box.Extents.y, f.box.Center.z - f.box.Extents.z });
					j["aabbMax"] = json::array({ f.box.Center.x + f.box.Extents.x, f.box.Center.y + f.box.Extents.y, f.box.Center.z + f.box.Extents.z });
					list.push_back(j);
				}
				return MakeResult(id, json{ {"matched", found.size()}, {"returned", list.size()}, {"entities", list} });
			});

			// Pipe-thread handler. Optionally moves the editor camera to a
			// viewpoint, waits for the frame (and temporal effects) to settle,
			// reads the camera's render target back on the main thread, puts the
			// camera back, then downscales + encodes OFF the main thread. The
			// image comes back inline (the MCP server turns "__image" into an
			// image content block).
			Reg("capture_view", R, Pipe, Solo, [this](int64_t id, const json& p) -> json {
				std::lock_guard<std::mutex> captureLock(_captureMutex); // one camera hijack at a time
				const int maxSize = std::clamp(p.value("maxSize", 768), 64, 2048);
				const std::string format = p.value("format", std::string("jpeg"));
				if (format != "jpeg" && format != "png") throw ParamError("'format' must be \"jpeg\" or \"png\"");
				const float quality = std::clamp(p.value("quality", 85), 10, 100) / 100.0f;
				const bool restore = p.value("restore", true);
				math::Vector3 reqPos, reqLookAt;
				float reqEul[3];
				const bool hasPos = ReadVec3(p, "position", reqPos);
				const bool hasLookAt = ReadVec3(p, "lookAt", reqLookAt);
				const bool hasEul = ReadFloats(p, "eulerDegrees", 3, reqEul);
				const bool hasFrame = p.contains("frameEntity") && !p["frameEntity"].is_null();
				const bool moves = hasPos || hasLookAt || hasEul || hasFrame;
				const int waitFrames = std::clamp(p.value("waitFrames", moves ? 8 : 2), 0, 600);

				struct Saved { math::Vector3 pos; float pitch = 0, yaw = 0, roll = 0; bool valid = false; };
				auto saved = std::make_shared<Saved>();
				auto applied = std::make_shared<json>();

				// 1. position the camera
				json setup = OnMain(id, [&, saved, applied]() -> json {
					auto scene = PrimaryUserScene();
					Camera* cam = scene ? scene->GetMainCamera() : nullptr;
					if (!cam) return MakeError(id, ErrorCode::NotAvailable, "no main camera");
					Entity* ce = cam->GetEntity();
					saved->pos = ce->GetPosition();
					saved->pitch = cam->GetPitch(); saved->yaw = cam->GetYaw(); saved->roll = cam->GetRoll();
					saved->valid = true;
					if (!moves) return MakeResult(id, json::object());

					math::Vector3 pos = hasPos ? reqPos : saved->pos;
					float pitch = saved->pitch, yaw = saved->yaw, roll = 0.0f;
					if (hasEul) { pitch = reqEul[0]; yaw = reqEul[1]; roll = reqEul[2]; }
					if (hasFrame)
					{
						json err;
						json wrap = json{ {"entity", p["frameEntity"]} };
						Entity* target = RequireEntity(id, scene.get(), wrap, "entity", false, err);
						if (!target) return err;
						dx::BoundingBox box;
						HierarchyAABB(target, box);
						const math::Vector3 c(box.Center.x, box.Center.y, box.Center.z);
						const float radius = std::max(math::Vector3(box.Extents.x, box.Extents.y, box.Extents.z).Length(), 0.25f);
						if (!hasEul) { pitch = -25.0f; yaw = saved->yaw; }
						const float halfFov = std::clamp(cam->GetFov(), 10.0f, 150.0f) * 0.5f * kPi / 180.0f;
						const float dist = radius / sinf(halfFov) * p.value("distanceScale", 1.15f);
						pos = c - DirectionFromAngles(pitch, yaw) * dist;
						(*applied)["framed"] = json{ {"center", Vec3Json(c)}, {"radius", radius}, {"distance", dist} };
					}
					else if (hasLookAt)
					{
						const math::Vector3 d = reqLookAt - pos;
						if (d.LengthSquared() < 1e-8f) throw ParamError("'lookAt' must differ from the camera position");
						AnglesFromDirection(d, pitch, yaw);
					}
					ce->SetPosition(pos);
					cam->SetPitch(pitch);
					cam->SetYaw(yaw);
					cam->SetRoll(roll);
					MarkPvsDirty();
					(*applied)["position"] = Vec3Json(pos);
					(*applied)["eulerDegrees"] = json::array({ pitch, yaw, roll });
					return MakeResult(id, json::object());
				});
				if (setup.contains("error")) return setup;

				// 2. let it render (and temporal effects converge)
				const uint64_t target = _frameCount.load() + (uint64_t)waitFrames;
				const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kPendingTimeoutMs);
				bool timedOut = false;
				while (_frameCount.load() < target)
				{
					if (std::chrono::steady_clock::now() >= deadline) { timedOut = true; break; }
					std::this_thread::sleep_for(std::chrono::milliseconds(2));
				}

				// 3. read back + restore (one main-thread hop)
				auto pixels = std::make_shared<std::vector<uint8_t>>();
				uint32_t fmt = 0;
				int w = 0, h = 0, srcW = 0, srcH = 0;
				bool gpuDownsampled = false;
				const auto grabStart = std::chrono::steady_clock::now();
				json grab = OnMain(id, [&, saved, pixels]() -> json {
					auto scene = PrimaryUserScene();
					Camera* cam = scene ? scene->GetMainCamera() : nullptr;
					std::string problem;
					if (!cam || !cam->GetRenderTarget()) problem = "no main-camera render target";
					else if (!timedOut)
					{
						ITexture2D* tex = cam->GetRenderTarget();
						srcW = tex->GetWidth();
						srcH = tex->GetHeight();
						gpuDownsampled = ReadbackDownsampledD3D11(tex, maxSize, *pixels, fmt, w, h);
						if (!gpuDownsampled)
						{
							fmt = tex->GetFormat();
							w = srcW;
							h = srcH;
							tex->GetPixels(*pixels);
						}
					}
					if (cam && moves && restore && saved->valid)
					{
						cam->GetEntity()->SetPosition(saved->pos);
						cam->SetPitch(saved->pitch);
						cam->SetYaw(saved->yaw);
						cam->SetRoll(saved->roll);
						MarkPvsDirty();
					}
					if (timedOut) problem = "the editor did not render " + std::to_string(waitFrames) + " frames in time";
					if (!problem.empty()) return MakeError(id, ErrorCode::NotAvailable, problem + (moves && restore ? " (camera restored)" : ""));
					return MakeResult(id, json::object());
				});
				if (grab.contains("error")) return grab;
				if (w <= 0 || h <= 0 || pixels->empty())
					return MakeError(id, ErrorCode::Internal, "render target readback returned nothing");

				// 4. downscale + encode on this (pipe) thread
				// Final size from the SOURCE aspect (a GPU mip may round odd sizes).
				const float scale = std::min(1.0f, (float)maxSize / (float)std::max(srcW, srcH));
				const int ow = std::max(1, (int)lroundf(srcW * scale)), oh = std::max(1, (int)lroundf(srcH * scale));
				std::vector<uint8_t> bgr, encoded;
				std::string err;
				if (!DownscaleToBgr(*pixels, fmt, w, h, ow, oh, bgr, err))
					return MakeError(id, ErrorCode::Internal, err);
				pixels.reset();
				if (!EncodeWic(bgr, ow, oh, format == "jpeg", quality, encoded, err))
					return MakeError(id, ErrorCode::Internal, err);
				json result = json{
					{"width", ow}, {"height", oh}, {"sourceSize", json::array({ srcW, srcH })}, {"bytes", encoded.size()},
					{"gpuDownsampled", gpuDownsampled},
					{"grabMs", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - grabStart).count()},
					{"frame", _frameCount.load()}, {"cameraRestored", moves && restore},
					{"__image", json{ {"mimeType", format == "jpeg" ? "image/jpeg" : "image/png"}, {"data", Base64(encoded)} }},
				};
				if (moves) result["camera"] = *applied;
				return MakeResult(id, result);
			});

			// ---- Play mode, simulation control, input, validation (D2) ----

			Reg("play", W, Pipe, Solo, [this](int64_t id, const json& p) -> json {
				const int waitFrames = std::clamp(p.value("waitFrames", 5), 0, 600);
				json started = OnMain(id, [&]() -> json {
					IEditorContext* ctx = Ctx();
					if (!ctx) return MakeError(id, ErrorCode::NotAvailable, "editor context not available");
					if (ctx->IsPlayModeActive()) return MakeResult(id, json{ {"alreadyPlaying", true} });
					// The editor's Run would otherwise BUILD a missing game DLL
					// (MSBuild of the game project); building is the user's call.
					const fs::path dll = fs::path(ctx->GetProjectFolderPath()) / "Build" / "Game.dll";
					std::error_code ec;
					if (!fs::exists(dll, ec))
						return MakeError(id, ErrorCode::NotAvailable, "no built game at '" + dll.string() + "': build the game project first");
					std::string err;
					if (!ctx->StartPlayMode(err)) return MakeError(id, ErrorCode::NotAvailable, err);
					return MakeResult(id, json::object());
				});
				if (started.contains("error")) return started;
				if (!WaitFrames(waitFrames))
					return MakeError(id, ErrorCode::NotAvailable, "play started but the editor stopped rendering frames");
				return OnMain(id, [&]() -> json {
					json r = SimulationJson();
					if (started["result"].value("alreadyPlaying", false)) r["alreadyPlaying"] = true;
					r["note"] = "Gameplay runs now. hex_stop restores the scene to its pre-play state; edits made while playing are discarded with it.";
					return MakeResult(id, r);
				});
			});

			Reg("stop", W, Main, Solo, [this](int64_t id, const json&) -> json {
				IEditorContext* ctx = Ctx();
				if (!ctx) return MakeError(id, ErrorCode::NotAvailable, "editor context not available");
				std::string err;
				if (!ctx->StopPlayMode(err)) return MakeError(id, ErrorCode::NotAvailable, err);
				MarkPvsDirty();
				json r = SimulationJson();
				r["note"] = "Scene restored to its pre-play state.";
				return MakeResult(id, r);
			});

			Reg("set_simulation", W, Main, Batch, [this](int64_t id, const json& p) -> json {
				IEditorContext* ctx = Ctx();
				if (!ctx || !ctx->IsPlayModeActive())
					return MakeError(id, ErrorCode::NotAvailable, "simulation control applies to play mode: start it with hex_play");
				TimeManager* tm = g_pEnv->_timeManager;
				if (p.contains("paused"))
				{
					if (!p["paused"].is_boolean()) throw ParamError("'paused' must be true or false");
					tm->_simulationPaused = p["paused"].get<bool>();
					if (!tm->_simulationPaused) tm->_pendingSimulationSteps = 0;
				}
				if (p.contains("timeScale"))
				{
					if (!p["timeScale"].is_number()) throw ParamError("'timeScale' must be a number");
					const float s = p["timeScale"].get<float>();
					if (!(s >= 0.0f && s <= 100.0f)) throw ParamError("'timeScale' must be 0..100");
					tm->_timeScale = s;
				}
				return MakeResult(id, SimulationJson());
			});

			Reg("step_simulation", W, Pipe, Solo, [this](int64_t id, const json& p) -> json {
				const int steps = std::clamp(p.value("steps", 1), 1, 6000);
				float before = 0.0f;
				json armed = OnMain(id, [&]() -> json {
					IEditorContext* ctx = Ctx();
					if (!ctx || !ctx->IsPlayModeActive())
						return MakeError(id, ErrorCode::NotAvailable, "stepping applies to play mode: start it with hex_play");
					TimeManager* tm = g_pEnv->_timeManager;
					tm->_simulationPaused = true; // stepping implies paused
					before = tm->_simulationTime;
					tm->_pendingSimulationSteps += (uint32_t)steps;
					return MakeResult(id, json::object());
				});
				if (armed.contains("error")) return armed;
				// <= 120 ticks run per frame; +2 so the last batch has rendered.
				if (!WaitFrames((steps + 119) / 120 + 2))
					return MakeError(id, ErrorCode::NotAvailable, "the editor stopped rendering frames while stepping");
				return OnMain(id, [&]() -> json {
					TimeManager* tm = g_pEnv->_timeManager;
					json r = SimulationJson();
					r["requestedSteps"] = steps;
					r["advanced"] = tm->_simulationTime - before;
					r["pendingSteps"] = tm->_pendingSimulationSteps;
					return MakeResult(id, r);
				});
			});

			Reg("send_input", W, Pipe, Solo, [this](int64_t id, const json& p) -> json {
				const std::vector<InputStep> steps = ParseInputEvents(p.value("events", json()));
				const bool allowInEditMode = p.value("allowInEditMode", false);
				const bool release = p.value("release", true);
				std::set<int> heldKeys, heldButtons;
				bool inputEnabled = true;

				auto applyRun = [&](size_t from, size_t to) -> json {
					return OnMain(id, [&, from, to]() -> json {
						IEditorContext* ctx = Ctx();
						if (!allowInEditMode && !(ctx && ctx->IsPlayModeActive()))
							return MakeError(id, ErrorCode::NotAvailable, "send_input drives the running game: start play mode (hex_play) first, or pass allowInEditMode: true - editor shortcuts react to it too");
						InputSystem* in = g_pEnv->_inputSystem;
						// The engine drops ALL input while the editor window is
						// unfocused (WM_KILLFOCUS) - and an agent-driven editor
						// usually is. Open the gate just for these events.
						const bool wasEnabled = in->IsInputEnabled();
						if (!wasEnabled) { in->EnableInput(true); inputEnabled = false; }
						for (size_t i = from; i < to; ++i)
						{
							const InputStep& s = steps[i];
							switch (s.kind)
							{
							case InputStep::KeyDown: in->OnKeyDown(s.code); heldKeys.insert(s.code); break;
							case InputStep::KeyUp: in->OnKeyUp(s.code); heldKeys.erase(s.code); break;
							case InputStep::MouseMove: in->OnMouseMove(s.x, s.y, false); break;
							case InputStep::MouseMoveTo: in->OnMouseMove(s.x, s.y, true); break;
							case InputStep::MouseDown:
							{
								int mx = 0, my = 0;
								in->GetMousePosition(mx, my);
								in->OnMouseDown(s.code, s.hasPos ? (int)s.x : mx, s.hasPos ? (int)s.y : my);
								heldButtons.insert(s.code);
								break;
							}
							case InputStep::MouseUp: in->OnMouseUp(s.code); heldButtons.erase(s.code); break;
							case InputStep::Wheel: in->OnMouseWheel(s.x); break;
							case InputStep::Char: in->OnChar((wchar_t)s.code); break;
							default: break;
							}
						}
						if (!wasEnabled) in->EnableInput(false);
						return MakeResult(id, json::object());
					});
				};

				size_t runStart = 0;
				int framesWaited = 0;
				json failure;
				for (size_t i = 0; i <= steps.size() && failure.is_null(); ++i)
				{
					const bool isWait = i < steps.size() && (steps[i].kind == InputStep::WaitFrames || steps[i].kind == InputStep::WaitMs);
					if (i == steps.size() || isWait)
					{
						if (i > runStart)
						{
							json r = applyRun(runStart, i);
							if (r.contains("error")) failure = r;
						}
						if (isWait && failure.is_null())
						{
							if (steps[i].kind == InputStep::WaitFrames)
							{
								if (!WaitFrames(steps[i].code)) failure = MakeError(id, ErrorCode::NotAvailable, "the editor stopped rendering frames");
								framesWaited += steps[i].code;
							}
							else
								std::this_thread::sleep_for(std::chrono::milliseconds(steps[i].code));
						}
						runStart = i + 1;
					}
				}
				// Never leave keys/buttons stuck down (also after a failure).
				json released = json::array();
				if (release && (!heldKeys.empty() || !heldButtons.empty()))
				{
					OnMain(id, [&]() -> json {
						InputSystem* in = g_pEnv->_inputSystem;
						const bool wasEnabled = in->IsInputEnabled();
						if (!wasEnabled) in->EnableInput(true);
						for (int k : heldKeys) { in->OnKeyUp(k); released.push_back(k); }
						for (int b : heldButtons) { in->OnMouseUp(b); released.push_back(b); }
						if (!wasEnabled) in->EnableInput(false);
						return MakeResult(id, json::object());
					});
				}
				if (!failure.is_null()) return failure;
				json r = json{ {"sent", steps.size()}, {"framesWaited", framesWaited}, {"frame", _frameCount.load()} };
				if (!released.empty()) r["releasedAtEnd"] = released;
				if (!inputEnabled) r["editorUnfocused"] = true; // delivered anyway (see above)
				return MakeResult(id, r);
			});

			// ---- Game code: sources, build, reload (E1) ----

			Reg("list_game_sources", R, Main, Batch, [](int64_t id, const json& p) -> json {
				const fs::path code = GameCodeDir();
				if (code.empty()) return MakeError(id, ErrorCode::NotAvailable, "no project is open");
				json files = json::array(), projects = json::array();
				std::error_code ec;
				for (fs::recursive_directory_iterator it(code, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
				{
					if (it->is_directory(ec)) { if (IsBuildOutputDir(it->path().filename().wstring())) it.disable_recursion_pending(); continue; }
					const fs::path& f = it->path();
					const std::wstring ext = f.extension().wstring();
					if (_wcsicmp(ext.c_str(), L".vcxproj") == 0 || _wcsicmp(ext.c_str(), L".sln") == 0) { projects.push_back(CodeRelative(f)); continue; }
					if (SourceKindOf(f) == SourceKind::None) continue;
					std::error_code se;
					files.push_back(json{ {"path", CodeRelative(f)}, {"bytes", (uint64_t)fs::file_size(f, se)} });
					if (files.size() >= 2000) break;
				}
				(void)p;
				IEditorContext* ctx = Ctx();
				return MakeResult(id, json{ {"codeDir", code.string()}, {"projects", projects}, {"files", files},
					{"gameLoaded", ctx && ctx->IsGameModuleLoaded()}, {"autoReload", ctx && ctx->GetGameAutoReload()},
					{"note", "Paths are relative to the project's Code folder. The game is an IGameExtension DLL built to Build/Game.dll."} });
			});

			Reg("read_game_source", R, Main, Batch, [](int64_t id, const json& p) -> json {
				fs::path path;
				std::string err;
				if (!ResolveGameSourcePath(RequireString(p, "path"), path, err)) return MakeError(id, ErrorCode::InvalidParams, err);
				std::ifstream in(path, std::ios::binary);
				if (!in) return MakeError(id, ErrorCode::NotAvailable, "no file at '" + CodeRelative(path) + "'");
				std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
				if (text.size() > 2 * 1024 * 1024) return MakeError(id, ErrorCode::InvalidParams, "file is over 2 MB");
				size_t lines = std::count(text.begin(), text.end(), '\n') + (text.empty() || text.back() == '\n' ? 0 : 1);
				return MakeResult(id, json{ {"path", CodeRelative(path)}, {"lines", lines}, {"content", text} });
			});

			Reg("write_game_source", W, Main, Batch, [](int64_t id, const json& p) -> json {
				fs::path path;
				std::string err;
				if (!ResolveGameSourcePath(RequireString(p, "path"), path, err)) return MakeError(id, ErrorCode::InvalidParams, err);
				if (SourceKindOf(path) == SourceKind::None)
					return MakeError(id, ErrorCode::InvalidParams, "only C/C++ sources and headers can be written (.cpp .cc .cxx .c .h .hpp .hxx .inl)");
				if (!p.contains("content") || !p["content"].is_string()) throw ParamError("'content' (the whole file text) is required");
				const std::string& content = p["content"].get_ref<const std::string&>();
				if (content.size() > 2 * 1024 * 1024) return MakeError(id, ErrorCode::InvalidParams, "content is over 2 MB");
				std::error_code ec;
				const bool existed = fs::exists(path, ec);
				if (existed && !p.value("overwrite", true))
					return MakeError(id, ErrorCode::InvalidParams, "'" + CodeRelative(path) + "' exists (overwrite: false)");
				fs::create_directories(path.parent_path(), ec);
				{
					std::ofstream out(path, std::ios::binary | std::ios::trunc);
					if (!out) return MakeError(id, ErrorCode::Internal, "cannot write '" + path.string() + "'");
					out << content;
				}
				json r = json{ {"path", CodeRelative(path)}, {"created", !existed}, {"bytes", content.size()} };
				if (!existed && p.value("addToProject", true))
				{
					const fs::path proj = OwningVcxproj(path);
					bool added = false;
					if (proj.empty()) r["warning"] = "no .vcxproj found under Code; the file is not part of the build";
					else if (!AddToVcxproj(proj, path, added, err)) r["warning"] = err;
					else { r["project"] = CodeRelative(proj); r["addedToProject"] = added; }
				}
				IEditorContext* ctx = Ctx();
				if (ctx && ctx->GetGameAutoReload() && ctx->IsGameModuleLoaded())
					r["note"] = "auto-reload is ON: the editor rebuilds + reloads by itself ~0.4 s after this change. Use set_game_auto_reload false and build_game to control builds yourself.";
				return MakeResult(id, r);
			});

			// ---- Scripts (Lua, HexEngine.ScriptingPlugin) ----
			//
			// Scripts are plain files under the project's Data folder, referenced
			// like any asset ("GameData.Scripts/Door.lua") and attached with
			// add_component ScriptComponent {script, properties}. The engine
			// hot-reloads a changed file within ~0.25 s, also during play.

			Reg("list_scripts", R, Main, Batch, [this](int64_t id, const json& p) -> json {
				IEditorContext* ctx = Ctx();
				FileSystem* projectFs = ctx ? ctx->GetProjectFileSystem() : nullptr;
				if (!projectFs) return MakeError(id, ErrorCode::NotAvailable, "no project is open");
				IScriptEngine* engine = g_pEnv->_scriptEngine;
				const std::string ext = engine ? engine->GetFileExtension() : ".lua";
				const bool check = p.value("check", false);
				const fs::path dataDir = projectFs->GetDataDirectory();
				const std::string fsName = WideToUtf8(projectFs->GetName());

				// Which entities in the loaded scenes use each script.
				std::map<std::string, json> users;
				for (auto& scene : g_pEnv->GetSceneManager().GetAllScenes())
				{
					if (!scene || HEX_HASFLAG(scene->GetFlags(), SceneFlags::Utility)) continue;
					std::vector<EntityId> ids;
					scene->GetLiveEntityIds(ids);
					for (const EntityId& eid : ids)
					{
						Entity* e = scene->TryGetEntity(eid);
						auto* sc = e ? e->GetComponent<ScriptComponent>() : nullptr;
						if (!sc || sc->GetScript().empty()) continue;
						json& u = users[sc->GetScript()];
						if (!u.is_array()) u = json::array();
						if (u.size() < 50) u.push_back(json{ {"entity", e->GetName()}, {"scene", WideToUtf8(scene->GetName())} });
					}
				}

				json scripts = json::array();
				std::error_code ec;
				for (fs::recursive_directory_iterator it(dataDir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
				{
					if (!it->is_regular_file(ec)) continue;
					const fs::path& f = it->path();
					if (_wcsicmp(f.extension().c_str(), Utf8ToWide(ext).c_str()) != 0) continue;
					const std::string ref = fsName + "." + fs::relative(f, dataDir).generic_string();
					std::error_code se;
					json s = json{ {"ref", ref}, {"bytes", (uint64_t)fs::file_size(f, se)} };
					if (auto u = users.find(ref); u != users.end()) s["usedBy"] = u->second;
					if (check && engine)
					{
						std::string err;
						const bool ok = engine->CheckScript(ref, err);
						s["compiles"] = ok;
						if (!ok) s["error"] = err;
					}
					scripts.push_back(std::move(s));
					if (scripts.size() >= 2000) break;
				}
				json r = json{ {"scripts", scripts} };
				r["scriptEngine"] = engine ? json(engine->GetLanguageName()) : json(nullptr);
				if (!engine) r["warning"] = "no script engine is loaded (HexEngine.ScriptingPlugin): ScriptComponents stay inert";
				return MakeResult(id, r);
			});

			Reg("read_script", R, Main, Batch, [this](int64_t id, const json& p) -> json {
				const std::string ext = g_pEnv->_scriptEngine ? g_pEnv->_scriptEngine->GetFileExtension() : ".lua";
				AssetRef a;
				std::string err;
				if (!ResolveAssetRef(RequireString(p, "path"), ext.c_str(), false, a, err)) return MakeError(id, ErrorCode::InvalidParams, err);
				std::ifstream in(a.absolute, std::ios::binary);
				if (!in) return MakeError(id, ErrorCode::NotAvailable, "no script at '" + a.ref + "'");
				std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
				if (text.size() > 2 * 1024 * 1024) return MakeError(id, ErrorCode::InvalidParams, "file is over 2 MB");
				const size_t lines = std::count(text.begin(), text.end(), '\n') + (text.empty() || text.back() == '\n' ? 0 : 1);
				return MakeResult(id, json{ {"ref", a.ref}, {"lines", lines}, {"content", text} });
			});

			Reg("write_script", W, Main, Batch, [this](int64_t id, const json& p) -> json {
				IScriptEngine* engine = g_pEnv->_scriptEngine;
				const std::string ext = engine ? engine->GetFileExtension() : ".lua";
				AssetRef a;
				std::string err;
				if (!ResolveAssetRef(RequireString(p, "path"), ext.c_str(), true, a, err)) return MakeError(id, ErrorCode::InvalidParams, err);
				if (!p.contains("content") || !p["content"].is_string()) throw ParamError("'content' (the whole script text) is required");
				const std::string& content = p["content"].get_ref<const std::string&>();
				if (content.size() > 2 * 1024 * 1024) return MakeError(id, ErrorCode::InvalidParams, "content is over 2 MB");
				std::error_code ec;
				const bool existed = fs::exists(a.absolute, ec);
				if (existed && !p.value("overwrite", true))
					return MakeError(id, ErrorCode::InvalidParams, "'" + a.ref + "' exists (overwrite: false)");
				fs::create_directories(a.absolute.parent_path(), ec);
				{
					std::ofstream out(a.absolute, std::ios::binary | std::ios::trunc);
					if (!out) return MakeError(id, ErrorCode::Internal, "cannot write '" + a.absolute.string() + "'");
					out << content;
				}
				json r = json{ {"ref", a.ref}, {"created", !existed}, {"bytes", content.size()} };
				if (!engine)
				{
					r["warning"] = "no script engine is loaded (HexEngine.ScriptingPlugin): the file is written but nothing runs it";
					return MakeResult(id, r);
				}
				// Compile it now so the caller gets errors straight away. (The
				// engine also hot-reloads it within ~0.25 s; a compile error keeps
				// the previous version running.)
				std::string compileError;
				const bool ok = engine->CheckScript(a.ref, compileError);
				r["compiles"] = ok;
				if (!ok) r["error"] = compileError;
				r["note"] = existed
					? std::string("running instances hot-reload within ~0.25 s and keep their state (OnReload is called); runtime errors appear in get_recent_engine_logs as [Script] warnings")
					: "attach with add_component {component: \"ScriptComponent\", fields: {script: \"" + a.ref + "\", properties: {...}}}; scripts run in play mode";
				return MakeResult(id, r);
			});

			Reg("check_script", R, Main, Batch, [this](int64_t id, const json& p) -> json {
				IScriptEngine* engine = g_pEnv->_scriptEngine;
				if (!engine) return MakeError(id, ErrorCode::NotAvailable, "no script engine is loaded (HexEngine.ScriptingPlugin)");
				AssetRef a;
				std::string err;
				if (!ResolveAssetRef(RequireString(p, "path"), engine->GetFileExtension(), false, a, err)) return MakeError(id, ErrorCode::InvalidParams, err);
				std::string compileError;
				const bool ok = engine->CheckScript(a.ref, compileError);
				json r = json{ {"ref", a.ref}, {"compiles", ok} };
				if (!ok) r["error"] = compileError;
				return MakeResult(id, r);
			});

			Reg("set_game_auto_reload", W, Main, Batch, [](int64_t id, const json& p) -> json {
				IEditorContext* ctx = Ctx();
				if (!ctx) return MakeError(id, ErrorCode::NotAvailable, "editor context not available");
				if (!p.contains("enabled") || !p["enabled"].is_boolean()) throw ParamError("'enabled' must be true or false");
				ctx->SetGameAutoReload(p["enabled"].get<bool>());
				return MakeResult(id, json{ {"autoReload", ctx->GetGameAutoReload()} });
			});

			Reg("build_game", W, Pipe, Solo, [this](int64_t id, const json& p) -> json {
				const bool reload = p.value("reload", true);
				const int waitSeconds = std::clamp(p.value("waitSeconds", 100), 0, 110);
				{
					std::lock_guard<std::mutex> lock(_build.m);
					if (_build.running)
						return MakeError(id, ErrorCode::NotAvailable, "a build is already running (build " + std::to_string(_build.id) + "); poll get_build_status");
					if (_build.thread.joinable()) _build.thread.join();
					_build.running = true;
					_build.id += 1;
					_build.started = std::chrono::steady_clock::now();
					_build.result = json{ {"state", "running"}, {"buildId", _build.id} };
				}
				const uint64_t buildId = _build.id;
				_build.thread = std::thread([this, reload, buildId]() {
					IEditorContext* ctx = Ctx();
					std::string err;
					const bool ok = ctx && ctx->BuildGameModule(err);
					const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - _build.started).count();
					const fs::path code = GameCodeDir();
					const fs::path log = code.empty() ? fs::path() : code.parent_path() / "Build" / "GameBuild.log";
					BuildDiagnostics d = ParseBuildLog(log);
					json r = json{ {"buildId", buildId}, {"state", ok ? "succeeded" : "failed"}, {"success", ok}, {"seconds", secs},
						{"errorCount", d.errorCount}, {"warningCount", d.warningCount}, {"errors", d.errors}, {"warnings", d.warnings},
						{"log", log.string()} };
					if (!ok)
					{
						r["error"] = err;
						if (d.errors.empty()) r["logTail"] = d.tail;
					}
					if (ok && reload)
					{
						json rr = OnMain(0, [&]() -> json {
							std::string e;
							if (!ctx->ReloadGameModule(e)) return MakeError(0, ErrorCode::Internal, e);
							return MakeResult(0, json{ {"playing", ctx->IsPlayModeActive()} });
						});
						r["reloaded"] = !rr.contains("error");
						if (rr.contains("error")) r["reloadError"] = rr["error"].value("message", std::string("reload failed"));
						else r["playing"] = rr["result"].value("playing", false);
					}
					std::lock_guard<std::mutex> lock(_build.m);
					_build.result = r;
					_build.running = false;
					_build.cv.notify_all();
				});
				std::unique_lock<std::mutex> lock(_build.m);
				_build.cv.wait_for(lock, std::chrono::seconds(waitSeconds), [&] { return !_build.running || _build.id != buildId; });
				json r = _build.result;
				if (_build.running)
				{
					r["note"] = "still building; poll get_build_status";
					r["seconds"] = std::chrono::duration<double>(std::chrono::steady_clock::now() - _build.started).count();
				}
				return MakeResult(id, r);
			});

			Reg("get_build_status", R, Pipe, Batch, [this](int64_t id, const json& p) -> json {
				const int waitSeconds = std::clamp(p.value("waitSeconds", 0), 0, 110);
				std::unique_lock<std::mutex> lock(_build.m);
				if (_build.id == 0) return MakeResult(id, json{ {"state", "idle"}, {"note", "no build has run in this editor session"} });
				if (waitSeconds > 0)
					_build.cv.wait_for(lock, std::chrono::seconds(waitSeconds), [&] { return !_build.running; });
				json r = _build.result;
				if (_build.running)
					r["seconds"] = std::chrono::duration<double>(std::chrono::steady_clock::now() - _build.started).count();
				return MakeResult(id, r);
			});

			Reg("reload_game", W, Main, Solo, [](int64_t id, const json&) -> json {
				IEditorContext* ctx = Ctx();
				if (!ctx) return MakeError(id, ErrorCode::NotAvailable, "editor context not available");
				std::error_code ec;
				const fs::path dll = fs::path(ctx->GetProjectFolderPath()) / "Build" / "Game.dll";
				if (!fs::exists(dll, ec)) return MakeError(id, ErrorCode::NotAvailable, "no built game at '" + dll.string() + "' (build_game first)");
				std::string err;
				if (!ctx->ReloadGameModule(err)) return MakeError(id, ErrorCode::Internal, err);
				return MakeResult(id, json{ {"reloaded", true}, {"playing", ctx->IsPlayModeActive()} });
			});

			// ---- Scenes + environment ----

			Reg("list_scenes", R, Main, Batch, [](int64_t id, const json&) -> json {
				IEditorContext* ctx = Ctx();
				if (!ctx) return MakeError(id, ErrorCode::NotAvailable, "editor context not available");
				json scenes = ctx->ListProjectScenes();
				return MakeResult(id, json{ {"count", scenes.size()}, {"scenes", scenes},
					{"note", "open_scene switches the editor to a scene (loading it if needed); new_scene creates one under Data/Scenes."} });
			});

			Reg("new_scene", W, Main, Solo, [this](int64_t id, const json& p) -> json {
				IEditorContext* ctx = Ctx();
				if (!ctx) return MakeError(id, ErrorCode::NotAvailable, "editor context not available");
				const std::string name = RequireString(p, "name");
				std::string err;
				if (!ctx->CreateProjectScene(name, p.value("withSun", true), p.value("activate", true), err))
					return MakeError(id, ErrorCode::InvalidParams, err);
				MarkPvsDirty();
				auto scene = PrimaryUserScene();
				return MakeResult(id, json{ {"created", name}, {"path", "Data/Scenes/" + name + ".hscene"},
					{"activeScene", scene ? WideToUtf8(scene->GetName()) : std::string()},
					{"note", "Saved with its project entry. It starts with a MainCamera (+ a default sun unless withSun=false). Switching scenes clears the undo history."} });
			});

			Reg("open_scene", W, Main, Solo, [this](int64_t id, const json& p) -> json {
				IEditorContext* ctx = Ctx();
				if (!ctx) return MakeError(id, ErrorCode::NotAvailable, "editor context not available");
				std::string err;
				if (!ctx->ActivateProjectScene(RequireString(p, "scene"), err))
					return MakeError(id, ErrorCode::InvalidParams, err);
				MarkPvsDirty();
				auto scene = PrimaryUserScene();
				return MakeResult(id, json{ {"activeScene", scene ? WideToUtf8(scene->GetName()) : std::string()},
					{"entityCount", scene ? scene->GetTotalNumberOfEntities() : 0u},
					{"note", "Every tool now acts on this scene. The undo history was cleared."} });
			});

			Reg("close_scene", W, Main, Solo, [this](int64_t id, const json& p) -> json {
				IEditorContext* ctx = Ctx();
				if (!ctx) return MakeError(id, ErrorCode::NotAvailable, "editor context not available");
				const std::string sceneName = RequireString(p, "scene");
				const bool deleteFile = p.value("deleteFile", false);
				std::string err;
				if (!ctx->RemoveProjectScene(sceneName, deleteFile, err))
					return MakeError(id, ErrorCode::InvalidParams, err);
				MarkPvsDirty();
				return MakeResult(id, json{ {"closed", sceneName}, {"fileDeleted", deleteFile} });
			});

			Reg("get_environment", R, Main, Batch, [](int64_t id, const json&) -> json {
				auto scene = PrimaryUserScene();
				if (!scene) return NoScene(id);
				return MakeResult(id, EnvironmentJson(scene.get()));
			});

			Reg("set_environment", W, Main, Solo, [this](int64_t id, const json& p) -> json {
				auto scene = PrimaryUserScene();
				if (!scene) return NoScene(id);
				IEditorContext* ctx = Ctx();
				json applied = json::object();

				// --- validate everything first ---
				std::vector<std::pair<HVar*, json>> cvars;
				if (p.contains("cvars"))
				{
					if (!p["cvars"].is_object()) throw ParamError("'cvars' must be an object {name: value}");
					for (auto it = p["cvars"].begin(); it != p["cvars"].end(); ++it)
					{
						HVar* v = g_pEnv->_commandManager->FindHVar(it.key());
						if (!v) throw ParamError("unknown cvar '" + it.key() + "' (see list_cvars)");
						cvars.push_back({ v, *it });
					}
				}
				int weatherPreset = -1;
				if (p.contains("weather"))
				{
					if (!p["weather"].is_string()) throw ParamError("'weather' must be a preset name");
					const std::string w = p["weather"].get<std::string>();
					for (int i = 0; i < (int)std::size(kWeatherPresets); ++i)
						if (_stricmp(w.c_str(), kWeatherPresets[i]) == 0) weatherPreset = i;
					if (weatherPreset < 0)
					{
						std::string list;
						for (const char* n : kWeatherPresets) list += (list.empty() ? "" : ", ") + std::string(n);
						throw ParamError("unknown weather preset '" + w + "' (" + list + ")");
					}
					if (!g_pEnv->_classRegistry->Find("WeatherControllerComponent"))
						return MakeError(id, ErrorCode::NotAvailable, "the weather plugin isn't loaded (no WeatherControllerComponent)");
				}
				if (p.contains("timeOfDay") && (!p["timeOfDay"].is_number() || p["timeOfDay"].get<float>() < 0.0f || p["timeOfDay"].get<float>() > 24.0f))
					throw ParamError("'timeOfDay' must be hours 0..24");
				if (p.contains("seaLevel") && !p["seaLevel"].is_null() && !p["seaLevel"].is_number())
					throw ParamError("'seaLevel' must be a number, or null to go back to the auto-detected level");
				if (p.contains("ocean") && !p["ocean"].is_object()) throw ParamError("'ocean' must be an object");

				// --- time of day: DayNightCycleComponent on the sun ---
				if (p.contains("timeOfDay") || p.contains("freezeTime") || p.contains("dayLengthSeconds"))
				{
					Entity* dn = FindEntityWithComponent(scene.get(), "DayNightCycleComponent");
					if (!dn)
					{
						dn = FindEntityWithComponent(scene.get(), "DirectionalLight");
						if (!dn) return MakeError(id, ErrorCode::NotAvailable, "the scene has no sun (DirectionalLight) for a day/night cycle; new_scene withSun=true creates one");
						json err;
						json r = AddComponentByName(id, dn, json{ {"component", "DayNightCycleComponent"} }, err);
						if (r.is_null()) return err;
						if (ctx) ctx->RecordComponentAdded(FindComponent(dn, "DayNightCycleComponent"));
						applied["dayNightCycleAddedTo"] = dn->GetName();
					}
					json fields = json::object();
					if (p.contains("timeOfDay")) fields["_currentTimeHours"] = p["timeOfDay"];
					if (p.contains("freezeTime")) fields["_simulateCycle"] = !p["freezeTime"].get<bool>();
					if (p.contains("dayLengthSeconds")) fields["_dayLengthSeconds"] = p["dayLengthSeconds"];
					json after;
					std::string msg;
					if (ctx) ctx->BeginComponentEdit(dn);
					const bool ok = ApplyComponentFields(FindComponent(dn, "DayNightCycleComponent"), fields, after, msg);
					if (ctx) ctx->EndComponentEdit(dn);
					if (!ok) return MakeError(id, ErrorCode::InvalidParams, msg);
					applied["timeOfDay"] = fields;
				}

				// --- weather: WeatherControllerComponent ---
				if (weatherPreset >= 0)
				{
					Entity* we = FindEntityWithComponent(scene.get(), "WeatherControllerComponent");
					if (!we)
					{
						we = scene->CreateEntity("Weather", math::Vector3(0.0f, 0.0f, 0.0f), math::Quaternion(0.0f, 0.0f, 0.0f, 1.0f), math::Vector3(1.0f, 1.0f, 1.0f)); // explicit: SimpleMath statics are not exported to plugins
						json err;
						json r = AddComponentByName(id, we, json{ {"component", "WeatherControllerComponent"} }, err);
						if (r.is_null()) return err;
						if (ctx) ctx->RecordEntityCreated(we);
						applied["weatherEntityCreated"] = we->GetName();
					}
					json fields = json{ {"_globalPresetId", weatherPreset} };
					if (p.contains("weatherTransitionSeconds")) fields["_defaultTransitionSeconds"] = p["weatherTransitionSeconds"];
					json after;
					std::string msg;
					if (ctx) ctx->BeginComponentEdit(we);
					const bool ok = ApplyComponentFields(FindComponent(we, "WeatherControllerComponent"), fields, after, msg);
					if (ctx) ctx->EndComponentEdit(we);
					if (!ok) return MakeError(id, ErrorCode::InvalidParams, msg);
					applied["weather"] = kWeatherPresets[weatherPreset];
				}

				// --- sea level + ocean look (scene settings, saved with the scene) ---
				if (p.contains("seaLevel"))
				{
					if (p["seaLevel"].is_null()) scene->SetSeaLevelOverride(false, 0.0f);
					else scene->SetSeaLevelOverride(true, p["seaLevel"].get<float>());
					applied["seaLevel"] = p["seaLevel"];
				}
				if (p.contains("ocean"))
				{
					OceanSettings& o = scene->GetOcean();
					for (auto it = p["ocean"].begin(); it != p["ocean"].end(); ++it)
					{
						const std::string& k = it.key();
						if (k == "shallowColour") o.shallowColour = ReadColourValue(*it, "ocean.shallowColour");
						else if (k == "deepColour") o.deepColour = ReadColourValue(*it, "ocean.deepColour");
						else if (k == "fresnelPow") o.fresnelPow = it->get<float>();
						else if (k == "shoreFadeStrength") o.shoreFadeStrength = it->get<float>();
						else if (k == "fadeFactor") o.fadeFactor = it->get<float>();
						else if (k == "reflectionStrength") o.reflectionStrength = it->get<float>();
						else if (k == "reflectionNearDistance") o.reflectionNearDistance = it->get<float>();
						else if (k == "reflectionFarDistance") o.reflectionFarDistance = it->get<float>();
						else throw ParamError("unknown ocean field '" + k + "'");
					}
					applied["ocean"] = p["ocean"];
				}

				// --- cvars (render / fog / clouds / atmosphere ...; saved with the scene) ---
				if (!cvars.empty())
				{
					CommandManager::LockVars();
					json set = json::object();
					std::string msg;
					for (auto& [v, value] : cvars)
					{
						if (!SetHVarFromJson(v, value, msg, true)) { CommandManager::UnlockVars(); throw ParamError(msg); }
						set[v->_name] = HVarValueJson(v->_type, v->_val);
					}
					CommandManager::UnlockVars();
					applied["cvars"] = set;
				}

				if (applied.empty()) throw ParamError("nothing to set: pass timeOfDay, freezeTime, dayLengthSeconds, weather, weatherTransitionSeconds, seaLevel, ocean or cvars");
				MarkPvsDirty();
				json result = EnvironmentJson(scene.get());
				result["applied"] = applied;
				return MakeResult(id, result);
			});

			// ---- Mesh assets in the scene + model import ----

			Reg("spawn_mesh", W, Main, Batch, [this](int64_t id, const json& p) -> json {
				auto scene = PrimaryUserScene();
				if (!scene) return NoScene(id);
				AssetRef meshRef;
				std::string err;
				if (!ResolveAssetRef(RequireString(p, "mesh"), ".hmesh", false, meshRef, err))
					return MakeError(id, ErrorCode::InvalidParams, err);
				if (!AssetExists(meshRef))
					return MakeError(id, ErrorCode::NotAvailable, "no mesh at '" + meshRef.ref + "' (see list_assets type=mesh)");
				std::shared_ptr<Material> overrideMat;
				if (p.contains("material") && !p["material"].is_null())
				{
					AssetRef mref;
					if (!ResolveAssetRef(RequireString(p, "material"), ".hmat", false, mref, err)) return MakeError(id, ErrorCode::InvalidParams, err);
					if (!AssetExists(mref)) return MakeError(id, ErrorCode::NotAvailable, "no material at '" + mref.ref + "'");
					overrideMat = Material::Create(fs::path(Utf8ToWide(mref.ref)));
					if (!overrideMat) return MakeError(id, ErrorCode::Internal, "'" + mref.ref + "' could not be loaded");
				}
				Entity* parent = nullptr;
				if (p.contains("parent") && !p["parent"].is_null())
				{
					json perr;
					parent = RequireEntity(id, scene.get(), p, "parent", false, perr);
					if (!parent) return perr;
				}
				json r = SpawnMeshEntity(scene.get(), meshRef, p.value("name", meshRef.absolute.stem().string()), overrideMat, parent, p, err);
				if (r.is_null()) return MakeError(id, ErrorCode::Internal, err);
				return MakeResult(id, r);
			});

			Reg("import_model", W, Main, Solo, [this](int64_t id, const json& p) -> json {
				auto scene = PrimaryUserScene();
				IEditorContext* ctx = Ctx();
				FileSystem* projectFs = ctx ? ctx->GetProjectFileSystem() : nullptr;
				if (!projectFs) return MakeError(id, ErrorCode::NotAvailable, "no project is open");
				if (!g_pEnv->_modelImporter) return MakeError(id, ErrorCode::NotAvailable, "no model importer plugin is loaded");

				// Source: any readable file on disk (or a project path).
				fs::path src = fs::path(Utf8ToWide(RequireString(p, "source")));
				std::error_code ec;
				if (!src.is_absolute())
				{
					fs::path resolved;
					std::string perr;
					if (!ResolveProjectPath(RequireString(p, "source"), resolved, perr)) return MakeError(id, ErrorCode::InvalidParams, perr);
					src = resolved;
				}
				if (!fs::is_regular_file(src, ec)) return MakeError(id, ErrorCode::NotAvailable, "no file at '" + src.string() + "'");
				const std::wstring ext = src.extension().wstring();
				bool supported = false;
				for (const wchar_t* x : { L".fbx", L".gltf", L".glb", L".obj", L".blend", L".dae", L".3ds" }) if (_wcsicmp(ext.c_str(), x) == 0) supported = true;
				if (!supported) return MakeError(id, ErrorCode::InvalidParams, "unsupported model type '" + src.extension().string() + "' (fbx, gltf, glb, obj, dae, 3ds; .blend from Blender 4+ fails - export fbx/gltf)");

				// Destination folder inside the project's Data folder.
				std::string destReq = p.value("destFolder", std::string("Models/") + src.stem().string());
				AssetRef destRef;
				std::string err;
				if (!ResolveAssetRef(destReq + "/_.hmesh", ".hmesh", true, destRef, err)) return MakeError(id, ErrorCode::InvalidParams, "destFolder: " + err);
				const fs::path destDir = destRef.absolute.parent_path();
				fs::create_directories(destDir, ec);

				// Copy the model, plus the files it usually needs (textures,
				// glTF buffers, .mtl) from beside it, into the project.
				json copied = json::array();
				auto copyIn = [&](const fs::path& f) {
					const fs::path to = destDir / f.filename();
					if (fs::weakly_canonical(f, ec) == fs::weakly_canonical(to, ec)) return;
					fs::copy_file(f, to, fs::copy_options::overwrite_existing, ec);
					if (!ec) copied.push_back(f.filename().string());
				};
				copyIn(src);
				if (p.value("copyTextures", true))
				{
					for (fs::directory_iterator it(src.parent_path(), ec), end; !ec && it != end; it.increment(ec))
					{
						if (!it->is_regular_file()) continue;
						const std::wstring e = it->path().extension().wstring();
						for (const wchar_t* x : { L".png", L".jpg", L".jpeg", L".tga", L".dds", L".bmp", L".tif", L".tiff", L".bin", L".mtl" })
							if (_wcsicmp(e.c_str(), x) == 0) { copyIn(it->path()); break; }
					}
					ec.clear();
				}

				// Snapshot, import, diff: everything new in the folder is output.
				std::set<std::wstring> before;
				for (fs::directory_iterator it(destDir, ec), end; !ec && it != end; it.increment(ec)) before.insert(it->path().filename().wstring());
				ec.clear();
				ModelImportSettings settings;
				settings.importAnimations = p.value("importAnimations", true);
				settings.createMaterials = p.value("createMaterials", true);
				settings.mergeChildMeshesByMaterial = p.value("mergeByMaterial", false);
				settings.mergeSiblingAnimations = p.value("mergeSiblingAnimations", false);
				settings.importScale = p.value("scale", 1.0f);
				settings.textureSearchPaths.push_back(destDir.wstring());
				std::vector<fs::path> meshes;
				if (!g_pEnv->_modelImporter->ImportModel(destDir / src.filename(), projectFs, settings, meshes, err))
					return MakeError(id, ErrorCode::Internal, err);

				json meshList = json::array(), others = json::array();
				for (const fs::path& m : meshes)
				{
					AssetRef r;
					std::string e2;
					if (ResolveAssetRef(m.string(), ".hmesh", false, r, e2)) meshList.push_back(r.ref);
				}
				for (fs::directory_iterator it(destDir, ec), end; !ec && it != end; it.increment(ec))
				{
					if (before.count(it->path().filename().wstring()) || _wcsicmp(it->path().extension().c_str(), L".hmesh") == 0) continue;
					AssetRef r;
					std::string e2;
					if (ResolveAssetRef(it->path().string(), nullptr, false, r, e2)) others.push_back(r.ref);
				}
				json result = json{ {"source", src.string()}, {"folder", ProjectRelative(destDir)}, {"copied", copied},
					{"meshes", meshList}, {"otherNewAssets", others} };

				// Optionally place it: a root entity with one child per mesh
				// (imported meshes keep their model-space placement).
				if (p.value("spawn", false) && scene && !meshList.empty())
				{
					math::Vector3 pos(0, 0, 0);
					ReadVec3(p, "position", pos);
					Entity* root = scene->CreateEntity(p.value("name", src.stem().string()), pos, math::Quaternion(0.0f, 0.0f, 0.0f, 1.0f), math::Vector3(1.0f, 1.0f, 1.0f));
					json children = json::array();
					for (const auto& mr : meshList)
					{
						AssetRef r;
						std::string e2;
						ResolveAssetRef(mr.get<std::string>(), ".hmesh", false, r, e2);
						json c = SpawnMeshEntity(scene.get(), r, r.absolute.stem().string(), nullptr, root, json::object(), e2, false); // the root's record covers them
						if (!c.is_null()) children.push_back(c["id"]);
					}
					if (IEditorContext* c2 = Ctx()) c2->RecordEntityCreated(root);
					MarkPvsDirty();
					result["spawned"] = json{ {"root", EntityBrief(root)}, {"meshEntities", children.size()} };
				}
				result["note"] = "Place meshes with spawn_mesh (or spawn: true here). Imported meshes keep their model-space offsets; parent them under one root to move them together.";
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

			// Methods whose edits go through the editor's undo history (each call
			// or batch = one step). The mesh builders only record an entity once
			// it is backed by a .hmesh (saveAs): an in-memory mesh couldn't be
			// rebuilt by redo.
			for (const char* name : { "create_entity", "add_component", "remove_component", "rename_entity", "set_parent",
				"set_entity_flags", "clone_entity", "set_entity_transform", "set_component_field", "delete_entity",
				"instantiate_prefab", "create_prefab", "create_mesh_entity", "create_primitive", "assign_material", "snap_to_ground", "spawn_mesh", "import_model" })
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

		// ---- D2: scene validation (main thread) ----
		json ValidateScene(int64_t id, Scene* scene, const json& p, bool referencesOnly)
		{
			const size_t limit = (size_t)std::clamp(p.value("limit", 200), 1, 5000);
			const int minRank = SeverityRank(p.value("minSeverity", std::string("info")));
			const float killY = p.value("killY", -500.0f);
			const bool groundCheck = !referencesOnly && p.value("groundCheck", false);
			const float floatTolerance = p.value("floatTolerance", 0.25f);
			IEditorContext* ctx = Ctx();

			std::vector<Issue> issues;
			auto add = [&](const char* sev, const char* code, Entity* e, std::string msg)
			{
				if (SeverityRank(sev) >= minRank) issues.push_back({ sev, code, e, std::move(msg) });
			};

			std::vector<EntityId> ids;
			scene->GetLiveEntityIds(ids);
			size_t lights = 0, playerStarts = 0, meshes = 0;
			std::map<std::string, Entity*> placed; // duplicate-spawn detection
			std::vector<Entity*> groundCandidates;
			std::map<std::string, std::string> scriptErrors; // one compile per script path
			for (const EntityId& eid : ids)
			{
				Entity* e = scene->TryGetEntity(eid);
				if (!e || e->IsPendingDeletion()) continue;
				const bool saved = !e->HasFlag(EntityFlags::DoNotSave);

				if (!referencesOnly)
				{
					const math::Vector3 pos = e->GetPosition(), sc = e->GetScale();
					const math::Quaternion q = e->GetRotation();
					if (!Finite3(pos) || !Finite3(sc) || !std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z) || !std::isfinite(q.w))
						add("error", "nonFiniteTransform", e, "position, rotation or scale is NaN/infinite");
					else
					{
						const math::Vector3 wp = e->GetWorldTM().Translation();
						if (wp.y < killY)
							add("warning", "fellOutOfWorld", e, "world y " + std::to_string(wp.y) + " is below killY " + std::to_string(killY) + " (fell through the ground?)");
						if (fabsf(sc.x) < 1e-6f || fabsf(sc.y) < 1e-6f || fabsf(sc.z) < 1e-6f)
							add("warning", "zeroScale", e, "a scale component is ~0 (invisible / degenerate physics)");
					}
					if (FindComponent(e, "PlayerStartComponent")) ++playerStarts;
					for (const char* lt : { "PointLight", "SpotLight", "DirectionalLight", "AreaLight" })
						if (FindComponent(e, lt)) { ++lights; break; }
					if (ctx && e->IsPrefabInstanceRoot())
					{
						const json info = ctx->DescribePrefabInstance(e);
						if (info.value("outOfDate", false))
							add("info", "prefabOutOfDate", e, "built from an older version of " + info.value("sourcePath", std::string("its prefab")) + " (update_prefab_instance)");
					}
				}

				if (auto* script = e->GetComponent<ScriptComponent>(); script != nullptr)
				{
					const std::string& scriptPath = script->GetScript();
					if (scriptPath.empty())
						add("warning", "scriptMissing", e, "ScriptComponent has no script");
					else if (g_pEnv->_scriptEngine != nullptr)
					{
						auto it = scriptErrors.find(scriptPath);
						if (it == scriptErrors.end())
						{
							std::string err;
							if (g_pEnv->_scriptEngine->CheckScript(scriptPath, err)) err.clear();
							else if (err.empty()) err = "does not compile";
							it = scriptErrors.emplace(scriptPath, err).first;
						}
						if (!it->second.empty())
							add("error", "scriptError", e, "script " + scriptPath + ": " + it->second);
					}
				}

				auto* smc = e->GetComponent<StaticMeshComponent>();
				if (!smc) continue;
				++meshes;
				auto mesh = smc->GetMesh();
				if (!mesh)
				{
					add("error", "meshMissing", e, "StaticMeshComponent has no mesh");
					continue;
				}
				const std::string meshProblem = CheckAssetPath(mesh->GetFileSystemPath());
				if (!meshProblem.empty())
				{
					if (mesh->GetFileSystemPath().empty())
					{
						if (saved) add("warning", "meshUnsaved", e, "in-memory mesh on an entity that saves with the scene: the saved reference will be empty (save_mesh_asset)");
					}
					else if (mesh->GetFileSystemPath().generic_string() != "TERRAIN")
						add("error", "meshFileMissing", e, "mesh " + meshProblem);
				}
				auto mat = smc->GetMaterial();
				if (!mat)
					add("warning", "materialMissing", e, "mesh has no material");
				else
				{
					const std::string matProblem = CheckAssetPath(mat->GetFileSystemPath());
					if (!matProblem.empty())
					{
						if (mat->GetFileSystemPath().empty())
						{
							if (saved) add("warning", "materialUnsaved", e, "private (unsaved) material on an entity that saves with the scene (save_mesh_asset or assign_material)");
						}
						else
							add("error", "materialFileMissing", e, "material " + matProblem);
					}
				}

				if (!referencesOnly && !mesh->GetFileSystemPath().empty())
				{
					// Same mesh at the same spot and orientation = a double spawn.
					const math::Vector3 wp = e->GetWorldTM().Translation();
					const math::Quaternion q = e->GetRotation();
					char key[512];
					snprintf(key, sizeof(key), "%s|%.2f|%.2f|%.2f|%.2f|%.2f|%.2f|%.2f", mesh->GetFileSystemPath().generic_string().c_str(),
						wp.x, wp.y, wp.z, q.x, q.y, q.z, q.w);
					auto [it, inserted] = placed.emplace(key, e);
					if (!inserted)
						add("warning", "duplicateOverlap", e, "same mesh, position and rotation as '" + it->second->GetName() + "' (spawned twice?)");
				}
				if (groundCheck && !e->GetParent() && !e->HasFlag(EntityFlags::DoNotRender))
					groundCandidates.push_back(e);
			}

			if (groundCheck)
			{
				// Floating roots: the surface under the bounds' bottom centre is
				// more than floatTolerance below it.
				size_t checked = 0;
				for (Entity* e : groundCandidates)
				{
					if (++checked > 3000) break;
					dx::BoundingBox box;
					HierarchyAABB(e, box);
					RayOptions opt;
					opt.rigidBodyColliders = false;
					opt.maxDistance = 200.0f;
					AddHierarchy(e, opt.ignore);
					const float bottom = box.Center.y - box.Extents.y;
					const RayResult r = CastRay(scene, math::Vector3(box.Center.x, bottom + 0.01f, box.Center.z), math::Vector3(0, -1, 0), opt);
					if (r.hit && r.distance - 0.01f > floatTolerance)
						add("info", "floating", e, "bottom is " + std::to_string(r.distance - 0.01f) + " above the surface below (" + (r.entity ? r.entity->GetName() : std::string("collider")) + "); snap_to_ground?");
				}
			}

			if (!referencesOnly)
			{
				if (!scene->GetMainCamera()) add("error", "noMainCamera", nullptr, "the scene has no main camera");
				if (lights == 0) add("warning", "noLights", nullptr, "no light components in the scene");
				if (playerStarts == 0) add("info", "noPlayerStart", nullptr, "no PlayerStartComponent: play mode starts wherever the editor camera is");
				if (ids.empty()) add("warning", "emptyScene", nullptr, "the scene has no entities");
			}

			std::stable_sort(issues.begin(), issues.end(), [](const Issue& a, const Issue& b) { return SeverityRank(a.severity) > SeverityRank(b.severity); });
			json counts = json::object(), list = json::array();
			json bySeverity = json{ {"error", 0}, {"warning", 0}, {"info", 0} };
			for (const auto& is : issues)
			{
				counts[is.code] = counts.value(is.code, 0) + 1;
				bySeverity[is.severity] = bySeverity[is.severity].get<int>() + 1;
				if (list.size() < limit)
					list.push_back(json{ {"severity", is.severity}, {"code", is.code}, {"entity", is.entity ? EntityBrief(is.entity) : json(nullptr)}, {"message", is.message} });
			}
			json result = json{
				{"sceneName", WideToUtf8(scene->GetName())},
				{"entityCount", ids.size()},
				{"meshEntities", meshes},
				{"issueCount", issues.size()},
				{"bySeverity", bySeverity},
				{"counts", counts},
				{"issues", list},
				{"truncated", issues.size() > list.size()},
			};
			(void)id;
			return result;
		}

		// Frames until `pred` holds (pipe thread). False on timeout.
		bool WaitFrames(int frames)
		{
			const uint64_t target = _frameCount.load() + (uint64_t)std::max(frames, 0);
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kPendingTimeoutMs * 6);
			while (_frameCount.load() < target)
			{
				if (std::chrono::steady_clock::now() >= deadline) return false;
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
			}
			return true;
		}

		json SimulationJson()
		{
			TimeManager* tm = g_pEnv ? g_pEnv->_timeManager : nullptr;
			const bool playing = Ctx() ? Ctx()->IsPlayModeActive() : (g_pEnv && g_pEnv->IsGameRunning());
			if (!tm) return json{ {"playing", playing} };
			return json{ {"playing", playing}, {"paused", tm->_simulationPaused}, {"timeScale", tm->_timeScale},
				{"simulationTime", tm->_simulationTime}, {"fixedStep", 1.0f / std::max(1.0f, SimulationRate())} };
		}

		static float SimulationRate()
		{
			if (HVar* v = g_pEnv && g_pEnv->_commandManager ? g_pEnv->_commandManager->FindHVar("cl_simulationRate") : nullptr)
				return v->_val.f32;
			return 60.0f;
		}

		// An entity showing a mesh ASSET (StaticMeshComponent, the mesh's own
		// material unless overridden). Transform from p (position / rotation /
		// eulerDegrees / scale). `record` = add it to the undo history (false
		// for children whose root is recorded afterwards).
		json SpawnMeshEntity(Scene* scene, const AssetRef& meshRef, const std::string& name, const std::shared_ptr<Material>& overrideMat, Entity* parent, const json& p, std::string& err, bool record = true)
		{
			auto mesh = Mesh::Create(fs::path(Utf8ToWide(meshRef.ref)));
			if (!mesh) { err = "'" + meshRef.ref + "' could not be loaded (see engine logs)"; return nullptr; }
			math::Vector3 pos(0, 0, 0), scale(1, 1, 1);
			ReadVec3(p, "position", pos);
			ReadVec3(p, "scale", scale);
			math::Quaternion rot(0, 0, 0, 1);
			ReadRotation(p, rot);
			Entity* e = scene->CreateEntity(name, pos, rot, scale);
			if (!e) { err = "Scene::CreateEntity failed"; return nullptr; }
			if (parent) e->SetParent(parent, false);
			auto* smc = e->AddComponent<StaticMeshComponent>();
			smc->SetMesh(mesh);
			if (overrideMat) smc->SetMaterial(overrideMat);
			else if (mesh->GetMaterial()) smc->SetMaterial(mesh->GetMaterial());
			if (record)
				if (IEditorContext* ctx = Ctx()) ctx->RecordEntityCreated(e);
			// Register with every PVS now (see clone_entity); re-file the parent too,
			// since attaching a bone-bound child to a skinned character during a bulk
			// spawn is exactly the session that lost characters from the PVS.
			scene->FlushPVS(e, false);
			if (parent) scene->FlushPVS(parent, false);
			MarkPvsDirty();
			json r = EntityBrief(e);
			r["mesh"] = meshRef.ref;
			const auto mat = smc->GetMaterial();
			r["material"] = mat && !mat->GetFileSystemPath().empty() ? json(mat->GetFileSystemPath().generic_string()) : json(nullptr);
			const dx::BoundingBox& b = e->GetWorldAABB();
			r["aabbMin"] = json::array({ b.Center.x - b.Extents.x, b.Center.y - b.Extents.y, b.Center.z - b.Extents.z });
			r["aabbMax"] = json::array({ b.Center.x + b.Extents.x, b.Center.y + b.Extents.y, b.Center.z + b.Extents.z });
			if (mesh->HasAnimations()) r["animated"] = true;
			return r;
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
		std::mutex      _captureMutex;     // capture_view: one camera hijack at a time

		// build_game: one background game build at a time.
		struct BuildJob
		{
			std::mutex m;
			std::condition_variable cv;
			std::thread thread;
			bool running = false;
			uint64_t id = 0;
			std::chrono::steady_clock::time_point started;
			json result;
		} _build;
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
