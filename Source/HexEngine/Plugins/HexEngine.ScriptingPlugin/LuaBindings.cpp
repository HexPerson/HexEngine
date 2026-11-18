
#include "LuaCommon.hpp"
#include "LuaScriptEngine.hpp"
#include "LuaScriptInstance.hpp"

#include <HexEngine.Core\FileSystem\PrefabLoader.hpp>

#include <cstdio>

namespace HexScripting
{
	using namespace HexEngine;
	using V = math::Vector3;
	using Q = math::Quaternion;

	// ------------------------------------------------------------------------
	// Handles
	// ------------------------------------------------------------------------

	Entity* EntityRef::Get() const
	{
		if (scene == nullptr || !id.IsValid() || g_pScriptEngine == nullptr || !g_pScriptEngine->IsSceneAlive(scene))
			return nullptr;
		Entity* entity = scene->TryGetEntity(id);
		if (entity == nullptr || entity->IsPendingDeletion())
			return nullptr;
		return entity;
	}

	EntityRef EntityRef::From(Entity* entity)
	{
		EntityRef ref;
		if (entity != nullptr)
		{
			ref.scene = entity->GetScene();
			ref.id = entity->GetId();
		}
		return ref;
	}

	Light* LightRef::Get() const
	{
		Entity* e = entity.Get();
		return e != nullptr ? e->GetComponentDerived<Light>() : nullptr;
	}

	RigidBody* RigidBodyRef::Get() const
	{
		Entity* e = entity.Get();
		return e != nullptr ? e->GetComponent<RigidBody>() : nullptr;
	}

	// ------------------------------------------------------------------------
	// Helpers
	// ------------------------------------------------------------------------

	std::wstring Utf8ToWide(const std::string& s)
	{
		if (s.empty())
			return {};
		const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
		std::wstring out(n, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
		return out;
	}

	std::string WideToUtf8(const std::wstring& s)
	{
		if (s.empty())
			return {};
		const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
		std::string out(n, '\0');
		WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n, nullptr, nullptr);
		return out;
	}

	namespace
	{
		struct KeyName { const char* name; int32_t vk; };

		const KeyName kKeyNames[] = {
			{ "Space", VK_SPACE }, { "Enter", VK_RETURN }, { "Escape", VK_ESCAPE }, { "Tab", VK_TAB },
			{ "Backspace", VK_BACK }, { "Shift", VK_SHIFT }, { "Ctrl", VK_CONTROL }, { "Alt", VK_MENU },
			{ "Up", VK_UP }, { "Down", VK_DOWN }, { "Left", VK_LEFT }, { "Right", VK_RIGHT },
			{ "Insert", VK_INSERT }, { "Delete", VK_DELETE }, { "Home", VK_HOME }, { "End", VK_END },
			{ "PageUp", VK_PRIOR }, { "PageDown", VK_NEXT },
			{ "Mouse1", VK_LBUTTON }, { "Mouse2", VK_RBUTTON }, { "Mouse3", VK_MBUTTON },
			{ "Mouse4", VK_XBUTTON1 }, { "Mouse5", VK_XBUTTON2 },
		};

		std::string Lower(std::string s)
		{
			for (char& c : s)
				c = (char)tolower((unsigned char)c);
			return s;
		}
	}

	int32_t KeyNameToVk(const std::string& name)
	{
		if (name.size() == 1)
		{
			const char c = (char)toupper((unsigned char)name[0]);
			if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
				return c;
		}
		const std::string lower = Lower(name);
		if (lower.size() >= 2 && lower[0] == 'f')
		{
			const int n = atoi(lower.c_str() + 1);
			if (n >= 1 && n <= 12)
				return VK_F1 + (n - 1);
		}
		for (const KeyName& k : kKeyNames)
		{
			if (Lower(k.name) == lower)
				return k.vk;
		}
		if (lower == "control") return VK_CONTROL;
		if (lower == "return") return VK_RETURN;
		if (lower == "esc") return VK_ESCAPE;
		return 0;
	}

	std::string VkToKeyName(int32_t vk)
	{
		if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9'))
			return std::string(1, (char)vk);
		if (vk >= VK_F1 && vk <= VK_F12)
			return "F" + std::to_string(vk - VK_F1 + 1);
		for (const KeyName& k : kKeyNames)
		{
			if (k.vk == vk)
				return k.name;
		}
		if (vk == VK_LSHIFT || vk == VK_RSHIFT) return "Shift";
		if (vk == VK_LCONTROL || vk == VK_RCONTROL) return "Ctrl";
		return "VK" + std::to_string(vk);
	}

	namespace
	{
		bool IsLuaInteger(const sol::object& o)
		{
			lua_State* L = o.lua_state();
			o.push();
			const bool isInt = lua_isinteger(L, -1) != 0;
			lua_pop(L, 1);
			return isInt;
		}

		json LuaToJsonImpl(const sol::object& value, bool& ok, int depth)
		{
			if (depth > 16)
			{
				ok = false;
				return nullptr;
			}

			switch (value.get_type())
			{
			case sol::type::lua_nil:
			case sol::type::none:
				return nullptr;
			case sol::type::boolean:
				return value.as<bool>();
			case sol::type::number:
				if (IsLuaInteger(value))
					return value.as<int64_t>();
				return value.as<double>();
			case sol::type::string:
				return value.as<std::string>();
			case sol::type::userdata:
				if (value.is<V>())
				{
					const V v = value.as<V>();
					return json::array({ v.x, v.y, v.z });
				}
				if (value.is<Q>())
				{
					const Q q = value.as<Q>();
					return json::array({ q.x, q.y, q.z, q.w });
				}
				ok = false;
				return nullptr;
			case sol::type::table:
			{
				sol::table t = value.as<sol::table>();
				const size_t n = t.size();
				bool isArray = n > 0;
				size_t count = 0;
				for (auto& kv : t)
				{
					(void)kv;
					++count;
				}
				if (count != n)
					isArray = false;

				if (isArray)
				{
					json arr = json::array();
					for (size_t i = 1; i <= n; ++i)
						arr.push_back(LuaToJsonImpl(t[i], ok, depth + 1));
					return arr;
				}

				json obj = json::object();
				for (auto& [k, v] : t)
				{
					if (!k.is<std::string>())
					{
						ok = false;
						continue;
					}
					obj[k.as<std::string>()] = LuaToJsonImpl(v, ok, depth + 1);
				}
				return obj;
			}
			default:
				ok = false;
				return nullptr;
			}
		}

		bool IsNumberArray(const json& j, size_t n)
		{
			if (!j.is_array() || j.size() != n)
				return false;
			for (const auto& e : j)
				if (!e.is_number())
					return false;
			return true;
		}
	}

	json LuaToJson(const sol::object& value, bool& ok)
	{
		ok = true;
		return LuaToJsonImpl(value, ok, 0);
	}

	sol::object JsonToLua(sol::state_view lua, const json& value, const json& like)
	{
		if (IsNumberArray(value, 3) && IsNumberArray(like, 3))
			return sol::make_object(lua, V(value[0].get<float>(), value[1].get<float>(), value[2].get<float>()));

		switch (value.type())
		{
		case json::value_t::null:
			return sol::make_object(lua, sol::lua_nil);
		case json::value_t::boolean:
			return sol::make_object(lua, value.get<bool>());
		case json::value_t::number_integer:
		case json::value_t::number_unsigned:
			return sol::make_object(lua, value.get<int64_t>());
		case json::value_t::number_float:
			return sol::make_object(lua, value.get<double>());
		case json::value_t::string:
			return sol::make_object(lua, value.get<std::string>());
		case json::value_t::array:
		{
			sol::table t = lua.create_table((int)value.size(), 0);
			for (size_t i = 0; i < value.size(); ++i)
				t[i + 1] = JsonToLua(lua, value[i], value[i]);
			return t;
		}
		case json::value_t::object:
		{
			sol::table t = lua.create_table();
			for (auto& [k, v] : value.items())
				t[k] = JsonToLua(lua, v, v);
			return t;
		}
		default:
			return sol::make_object(lua, sol::lua_nil);
		}
	}

	namespace
	{
		// A JsonFile with no disk I/O, for driving a component's own
		// Serialize/Deserialize (same approach as the editor bridge).
		class MemoryJsonFile : public JsonFile
		{
		public:
			MemoryJsonFile() :
				JsonFile(fs::path(L"<script-memory>"), std::ios::out | std::ios::binary, DiskFileOptions::None)
			{}

			bool     Open() override { _isOpen = true; return true; }
			void     Close() override { _isOpen = false; }
			bool     DoesExist() override { return false; }
			bool     Delete() override { return false; }
			bool     IsOpen() const override { return _isOpen; }
			uint32_t GetSize() override { return 0; }
			uint32_t Read(void*, uint32_t) override { return 0; }
			void     Flush() override {}
			uint32_t Write(void*, uint32_t size) override { return size; }

		private:
			bool _isOpen = true;
		};

		Entity* Need(const EntityRef& ref)
		{
			Entity* e = ref.Get();
			if (e == nullptr)
				throw sol::error("entity is not valid (destroyed, or never found) - check entity:IsValid()");
			return e;
		}

		Light* NeedLight(const LightRef& ref)
		{
			Light* l = ref.Get();
			if (l == nullptr)
				throw sol::error("light is not valid (its entity was destroyed or lost its light)");
			return l;
		}

		IRigidBody* NeedBody(const RigidBodyRef& ref)
		{
			RigidBody* rb = ref.Get();
			IRigidBody* body = rb != nullptr ? rb->GetIRigidBody() : nullptr;
			if (body == nullptr)
				throw sol::error("rigid body is not valid (no RigidBody, or it has no physics body yet)");
			return body;
		}

		int32_t ToVk(const sol::object& key)
		{
			if (key.get_type() == sol::type::number)
				return key.as<int32_t>();
			if (key.get_type() == sol::type::string)
			{
				const std::string name = key.as<std::string>();
				const int32_t vk = KeyNameToVk(name);
				if (vk == 0)
					throw sol::error("unknown key name '" + name + "' (use e.g. \"W\", \"Space\", \"Shift\", \"F1\", \"Mouse1\")");
				return vk;
			}
			throw sol::error("key must be a name like \"W\" or a virtual-key code");
		}

		std::string Fmt(const char* fmt, double a, double b, double c)
		{
			char buf[128];
			snprintf(buf, sizeof(buf), fmt, a, b, c);
			return buf;
		}

		Entity* FindChildRecursive(Entity* parent, const std::string& name)
		{
			for (Entity* child : parent->GetChildren())
			{
				if (child == nullptr || child->IsPendingDeletion())
					continue;
				if (child->GetName() == name)
					return child;
				if (Entity* found = FindChildRecursive(child, name))
					return found;
			}
			return nullptr;
		}

		// Optional field of an options table, with a default.
		template <typename T>
		T Opt(const sol::table& t, const char* key, T fallback)
		{
			sol::optional<T> v = t[key];
			return v ? *v : fallback;
		}

		Scene* NeedScene()
		{
			Scene* scene = g_pScriptEngine != nullptr ? g_pScriptEngine->GetCurrentScene() : nullptr;
			if (scene == nullptr)
				throw sol::error("no scene is active");
			return scene;
		}

		float FixedDeltaTime()
		{
			if (g_pEnv != nullptr && g_pEnv->_commandManager != nullptr)
			{
				if (HVar* rate = g_pEnv->_commandManager->FindHVar("cl_simulationRate"); rate != nullptr && rate->_val.f32 > 0.0f)
					return 1.0f / rate->_val.f32;
			}
			return 1.0f / 120.0f;
		}

		std::string JoinArgs(sol::variadic_args args, sol::this_state ts)
		{
			sol::state_view lua(ts);
			sol::protected_function tostr = lua["tostring"];
			std::string line;
			for (auto arg : args)
			{
				if (!line.empty())
					line += ' ';
				sol::protected_function_result r = tostr(arg.get<sol::object>());
				line += r.valid() ? r.get<std::string>() : std::string("?");
			}
			return line;
		}

		// ---- Vec3 / Quat ----------------------------------------------------

		void RegisterMath(sol::state& lua)
		{
			lua.new_usertype<V>("Vec3",
				sol::call_constructor, sol::factories(
					[]() { return V(0.0f, 0.0f, 0.0f); },
					[](float s) { return V(s, s, s); },
					[](float x, float y, float z) { return V(x, y, z); }),
				"x", sol::property([](const V& v) { return v.x; }, [](V& v, float x) { v.x = x; }),
				"y", sol::property([](const V& v) { return v.y; }, [](V& v, float y) { v.y = y; }),
				"z", sol::property([](const V& v) { return v.z; }, [](V& v, float z) { v.z = z; }),
				sol::meta_function::addition, [](const V& a, const V& b) { return a + b; },
				sol::meta_function::subtraction, [](const V& a, const V& b) { return a - b; },
				sol::meta_function::multiplication, sol::overload(
					[](const V& a, float s) { return a * s; },
					[](float s, const V& a) { return a * s; },
					[](const V& a, const V& b) { return a * b; }),
				sol::meta_function::division, sol::overload(
					[](const V& a, float s) { return a / s; },
					[](const V& a, const V& b) { return a / b; }),
				sol::meta_function::unary_minus, [](const V& a) { return -a; },
				sol::meta_function::equal_to, [](const V& a, const V& b) { return a == b; },
				sol::meta_function::to_string, [](const V& v) { return Fmt("Vec3(%.3f, %.3f, %.3f)", v.x, v.y, v.z); },
				"Length", [](const V& v) { return v.Length(); },
				"LengthSquared", [](const V& v) { return v.LengthSquared(); },
				"Normalized", [](const V& v) { V r = v; if (r.LengthSquared() > 1e-12f) r.Normalize(); return r; },
				"Dot", [](const V& a, const V& b) { return a.Dot(b); },
				"Cross", [](const V& a, const V& b) { return a.Cross(b); },
				"Distance", [](const V& a, const V& b) { return V::Distance(a, b); },
				"Lerp", [](const V& a, const V& b, float t) { return V::Lerp(a, b, t); },
				"Zero", []() { return V(0.0f, 0.0f, 0.0f); },
				"One", []() { return V(1.0f, 1.0f, 1.0f); },
				"Up", []() { return V::Up; },
				"Right", []() { return V::Right; },
				"Forward", []() { return V::Forward; });

			lua.new_usertype<Q>("Quat",
				sol::call_constructor, sol::factories(
					[]() { return Q::Identity; },
					[](float x, float y, float z, float w) { return Q(x, y, z, w); }),
				"x", sol::property([](const Q& q) { return q.x; }, [](Q& q, float v) { q.x = v; }),
				"y", sol::property([](const Q& q) { return q.y; }, [](Q& q, float v) { q.y = v; }),
				"z", sol::property([](const Q& q) { return q.z; }, [](Q& q, float v) { q.z = v; }),
				"w", sol::property([](const Q& q) { return q.w; }, [](Q& q, float v) { q.w = v; }),
				"Identity", []() { return Q::Identity; },
				// Degrees: pitch about X, yaw about Y, roll about Z.
				"Euler", [](float pitch, float yaw, float roll) {
					return Q::CreateFromYawPitchRoll(DirectX::XMConvertToRadians(yaw), DirectX::XMConvertToRadians(pitch), DirectX::XMConvertToRadians(roll));
				},
				"AxisAngle", [](const V& axis, float degrees) {
					V a = axis;
					if (a.LengthSquared() < 1e-12f) a = V::Up;
					a.Normalize();
					return Q::CreateFromAxisAngle(a, DirectX::XMConvertToRadians(degrees));
				},
				"LookRotation", [](const V& forward, sol::optional<V> up) {
					V f = forward;
					if (f.LengthSquared() < 1e-12f) return Q::Identity;
					f.Normalize();
					return Q::LookRotation(f, up.value_or(V::Up));
				},
				"Slerp", [](const Q& a, const Q& b, float t) { return Q::Slerp(a, b, t); },
				// a * b = rotate by a, then by b. q * v rotates the vector.
				sol::meta_function::multiplication, sol::overload(
					[](const Q& a, const Q& b) { return a * b; },
					[](const Q& q, const V& v) { return V::Transform(v, q); }),
				"Rotate", [](const Q& q, const V& v) { return V::Transform(v, q); },
				"Inverse", [](const Q& q) { Q r; q.Inverse(r); return r; },
				// Degrees, as Vec3(pitch, yaw, roll).
				"ToEuler", [](const Q& q) {
					const V e = q.ToEuler();
					return V(DirectX::XMConvertToDegrees(e.x), DirectX::XMConvertToDegrees(e.y), DirectX::XMConvertToDegrees(e.z));
				},
				sol::meta_function::to_string, [](const Q& q) {
					char buf[128];
					snprintf(buf, sizeof(buf), "Quat(%.3f, %.3f, %.3f, %.3f)", q.x, q.y, q.z, q.w);
					return std::string(buf);
				});
		}

		// ---- Generic component fields -----------------------------------------

		sol::object GetField(const EntityRef& ref, const std::string& component, const std::string& field, sol::this_state ts)
		{
			Entity* e = Need(ref);
			BaseComponent* c = e->GetComponentByClassName(component);
			if (c == nullptr)
				throw sol::error("entity '" + e->GetName() + "' has no component '" + component + "'");
			MemoryJsonFile mem;
			json data = json::object();
			c->Serialize(data, &mem);
			auto it = data.find(field);
			if (it == data.end())
				throw sol::error("component '" + component + "' has no serialised field '" + field + "'");
			return JsonToLua(sol::state_view(ts), *it, *it);
		}

		void SetField(const EntityRef& ref, const std::string& component, const std::string& field, const sol::object& value)
		{
			Entity* e = Need(ref);
			BaseComponent* c = e->GetComponentByClassName(component);
			if (c == nullptr)
				throw sol::error("entity '" + e->GetName() + "' has no component '" + component + "'");
			MemoryJsonFile mem;
			json data = json::object();
			c->Serialize(data, &mem);
			auto it = data.find(field);
			if (it == data.end())
				throw sol::error("component '" + component + "' has no serialised field '" + field + "'");
			bool ok = true;
			json j = LuaToJson(value, ok);
			if (!ok)
				throw sol::error("value for '" + field + "' can't be stored (use numbers, booleans, strings, Vec3 or tables of them)");
			if (it->is_object() && j.is_object())
				it->merge_patch(j);
			else
				*it = j;
			c->Deserialize(data, &mem, 0);
		}

		// ---- Entity ---------------------------------------------------------------

		void RegisterEntity(sol::state& lua)
		{
			lua.new_usertype<EntityRef>("Entity", sol::no_constructor,
				"IsValid", [](const EntityRef& r) { return r.Get() != nullptr; },
				"GetName", [](const EntityRef& r) { Entity* e = r.Get(); return e ? e->GetName() : std::string(); },
				"GetId", [](const EntityRef& r) { return (int64_t)(((uint64_t)r.id.generation << 32) | r.id.index); },

				"GetPosition", [](const EntityRef& r) { return Need(r)->GetPosition(); },
				"SetPosition", sol::overload(
					[](const EntityRef& r, const V& p) { Need(r)->SetPosition(p); },
					[](const EntityRef& r, float x, float y, float z) { Need(r)->SetPosition(V(x, y, z)); }),
				"GetWorldPosition", [](const EntityRef& r) { return Need(r)->GetWorldTM().Translation(); },
				"GetRotation", [](const EntityRef& r) { return Need(r)->GetRotation(); },
				"SetRotation", [](const EntityRef& r, const Q& q) { Need(r)->SetRotation(q); },
				"GetScale", [](const EntityRef& r) { return Need(r)->GetScale(); },
				"SetScale", sol::overload(
					[](const EntityRef& r, const V& s) { Need(r)->SetScale(s); },
					[](const EntityRef& r, float s) { Need(r)->SetScale(V(s, s, s)); }),
				"Translate", [](const EntityRef& r, const V& delta) { Entity* e = Need(r); e->SetPosition(e->GetPosition() + delta); },
				// Rotate about an axis (parent space), degrees.
				"Rotate", [](const EntityRef& r, const V& axis, float degrees) {
					Entity* e = Need(r);
					V a = axis;
					if (a.LengthSquared() < 1e-12f) return;
					a.Normalize();
					e->SetRotation(e->GetRotation() * Q::CreateFromAxisAngle(a, DirectX::XMConvertToRadians(degrees)));
				},
				"LookAt", [](const EntityRef& r, const V& target, sol::optional<V> up) {
					Entity* e = Need(r);
					V dir = target - e->GetWorldTM().Translation();
					if (dir.LengthSquared() < 1e-12f) return;
					dir.Normalize();
					e->SetRotation(Q::LookRotation(dir, up.value_or(V::Up)));
				},
				"GetForward", [](const EntityRef& r) { Transform* t = Need(r)->GetComponent<Transform>(); return t ? t->GetForward() : V::Forward; },
				"GetRight", [](const EntityRef& r) { Transform* t = Need(r)->GetComponent<Transform>(); return t ? t->GetRight() : V::Right; },
				"GetUp", [](const EntityRef& r) { Transform* t = Need(r)->GetComponent<Transform>(); return t ? t->GetUp() : V::Up; },

				"SetVisible", [](const EntityRef& r, bool visible) {
					Entity* e = Need(r);
					if (visible) e->ClearFlags(EntityFlags::DoNotRender);
					else e->SetFlag(EntityFlags::DoNotRender);
				},
				"IsVisible", [](const EntityRef& r) { return !Need(r)->HasFlag(EntityFlags::DoNotRender); },

				"GetParent", [](const EntityRef& r) -> sol::optional<EntityRef> {
					Entity* p = Need(r)->GetParent();
					if (p == nullptr) return sol::nullopt;
					return EntityRef::From(p);
				},
				"GetChildren", [](const EntityRef& r, sol::this_state ts) {
					sol::state_view lua(ts);
					sol::table out = lua.create_table();
					int i = 1;
					for (Entity* child : Need(r)->GetChildren())
					{
						if (child != nullptr && !child->IsPendingDeletion())
							out[i++] = EntityRef::From(child);
					}
					return out;
				},
				"FindChild", [](const EntityRef& r, const std::string& name) -> sol::optional<EntityRef> {
					Entity* found = FindChildRecursive(Need(r), name);
					if (found == nullptr) return sol::nullopt;
					return EntityRef::From(found);
				},
				"Destroy", [](const EntityRef& r) {
					if (Entity* e = r.Get(); e != nullptr && e->GetScene() != nullptr)
						e->GetScene()->DestroyEntity(e);
				},

				"HasComponent", [](const EntityRef& r, const std::string& name) { return Need(r)->GetComponentByClassName(name) != nullptr; },
				"GetLight", [](const EntityRef& r) -> sol::optional<LightRef> {
					if (Need(r)->GetComponentDerived<Light>() == nullptr) return sol::nullopt;
					return LightRef{ r };
				},
				"GetRigidBody", [](const EntityRef& r) -> sol::optional<RigidBodyRef> {
					if (Need(r)->GetComponent<RigidBody>() == nullptr) return sol::nullopt;
					return RigidBodyRef{ r };
				},
				// The other entity's running script object (its `self`), or nil.
				"GetScript", [](const EntityRef& r, sol::this_state ts) -> sol::object {
					ScriptComponent* sc = Need(r)->GetComponent<ScriptComponent>();
					auto* inst = sc != nullptr ? static_cast<LuaScriptInstance*>(sc->GetScriptInstance()) : nullptr;
					if (inst == nullptr || !inst->GetSelf().valid())
						return sol::make_object(sol::state_view(ts), sol::lua_nil);
					return inst->GetSelf();
				},
				"GetField", &GetField,
				"SetField", &SetField,

				sol::meta_function::equal_to, [](const EntityRef& a, const EntityRef& b) { return a.scene == b.scene && a.id == b.id; },
				sol::meta_function::to_string, [](const EntityRef& r) {
					Entity* e = r.Get();
					return e ? "Entity(" + e->GetName() + ")" : std::string("Entity(<invalid>)");
				});

			lua.new_usertype<LightRef>("Light", sol::no_constructor,
				"GetStrength", [](const LightRef& l) { return NeedLight(l)->GetLightStrength(); },
				"SetStrength", [](const LightRef& l, float v) { NeedLight(l)->SetLightStength(v); },
				"GetMultiplier", [](const LightRef& l) { return NeedLight(l)->GetLightMultiplier(); },
				"SetMultiplier", [](const LightRef& l, float v) { NeedLight(l)->SetLightMultiplier(v); },
				"GetRadius", [](const LightRef& l) { return NeedLight(l)->GetRadius(); },
				"SetRadius", [](const LightRef& l, float v) { NeedLight(l)->SetRadius(v); },
				// RGB(A) 0..1
				"GetColour", [](const LightRef& l) {
					const math::Vector4 c = NeedLight(l)->GetDiffuseColour();
					return std::make_tuple(c.x, c.y, c.z, c.w);
				},
				"SetColour", [](const LightRef& l, float r, float g, float b, sol::optional<float> a) {
					NeedLight(l)->SetDiffuseColour(math::Color(r, g, b, a.value_or(1.0f)));
				},
				"GetEntity", [](const LightRef& l) { return l.entity; });

			lua.new_usertype<RigidBodyRef>("RigidBody", sol::no_constructor,
				"AddForce", [](const RigidBodyRef& b, const V& f) { IRigidBody* body = NeedBody(b); body->WakeUp(); body->ApplyForceToCenterOfMass(f); },
				"AddTorque", [](const RigidBodyRef& b, const V& t) { IRigidBody* body = NeedBody(b); body->WakeUp(); body->ApplyTorque(t); },
				"GetVelocity", [](const RigidBodyRef& b) { return NeedBody(b)->GetLinearVelocity(); },
				"GetMass", [](const RigidBodyRef& b) { return NeedBody(b)->GetMass(); },
				"SetMass", [](const RigidBodyRef& b, float m) { NeedBody(b)->SetMass(m); },
				"SetGravityEnabled", [](const RigidBodyRef& b, bool on) { NeedBody(b)->SetGravityEnabled(on); },
				"WakeUp", [](const RigidBodyRef& b) { NeedBody(b)->WakeUp(); },
				"IsOnGround", [](const RigidBodyRef& b) { return NeedBody(b)->IsOnGround(); },
				// Character controllers: move by a displacement this frame.
				"Move", [](const RigidBodyRef& b, const V& displacement, float dt) { NeedBody(b)->Move(displacement, 0.0001f, dt); },
				// Place the body (and its entity) somewhere without simulating the path.
				"Teleport", [](const RigidBodyRef& b, const V& position) {
					IRigidBody* body = NeedBody(b);
					body->UpdatePosePosition(position);
					if (Entity* e = b.entity.Get())
						e->SetPosition(position);
				},
				"GetEntity", [](const RigidBodyRef& b) { return b.entity; });
		}

		// ---- Scene ------------------------------------------------------------------

		void RegisterScene(sol::state& lua)
		{
			sol::table scene = lua.create_named_table("Scene");

			scene.set_function("Find", [](const std::string& name) -> sol::optional<EntityRef> {
				Entity* e = NeedScene()->GetEntityByName(name);
				if (e == nullptr || e->IsPendingDeletion())
					return sol::nullopt;
				return EntityRef::From(e);
			});

			scene.set_function("CreateEntity", [](const std::string& name, sol::optional<V> position) {
				Entity* e = NeedScene()->CreateEntity(name, position.value_or(V(0.0f, 0.0f, 0.0f)));
				if (e == nullptr)
					throw sol::error("could not create entity '" + name + "'");
				return EntityRef::From(e);
			});

			// Spawn("GameData.Prefabs/Crate.hprefab", pos?, rot?) -> root entity
			scene.set_function("Spawn", [](const std::string& prefabPath, sol::optional<V> position, sol::optional<Q> rotation) -> sol::optional<EntityRef> {
				Scene* s = NeedScene();
				auto shared = g_pScriptEngine->GetSharedScene(s);
				if (shared == nullptr || g_pEnv->_prefabLoader == nullptr)
					throw sol::error("prefab spawning is unavailable in this scene");

				std::vector<Entity*> spawned = g_pEnv->_prefabLoader->LoadPrefab(shared, fs::path(Utf8ToWide(prefabPath)));
				if (spawned.empty())
				{
					LOG_WARN("[Script] %s: Scene.Spawn('%s') failed (bad prefab path?)", g_pScriptEngine->DescribeContext().c_str(), prefabPath.c_str());
					return sol::nullopt;
				}

				Entity* root = nullptr;
				for (Entity* e : spawned)
				{
					if (e != nullptr && e->IsPrefabInstanceRoot()) { root = e; break; }
				}
				if (root == nullptr)
					root = spawned.front();

				if (position) root->SetPosition(*position);
				if (rotation) root->SetRotation(*rotation);
				return EntityRef::From(root);
			});

			// Raycast(origin, direction, maxDistance?, options?) -> hit table or nil
			//   options: { ignore = entity | {entities}, ignoreSelf = true,
			//              layerMask = n, meshes = true, physics = true, minNormalY = -2 }
			scene.set_function("Raycast", [](const V& origin, const V& direction, sol::optional<float> maxDistance, sol::optional<sol::table> options, sol::this_state ts) -> sol::object {
				sol::state_view lua(ts);
				Scene* s = NeedScene();

				RayCastOptions opts;
				opts.maxDistance = maxDistance.value_or(10000.0f);

				bool ignoreSelf = true;
				if (options)
				{
					sol::table o = *options;
					ignoreSelf = Opt<bool>(o, "ignoreSelf", true);
					opts.meshes = Opt<bool>(o, "meshes", opts.meshes);
					opts.physics = Opt<bool>(o, "physics", opts.physics);
					opts.minNormalY = Opt<float>(o, "minNormalY", opts.minNormalY);
					opts.layerMask = (LayerMask)Opt<int64_t>(o, "layerMask", (int64_t)opts.layerMask);

					sol::object ignore = o["ignore"];
					if (ignore.is<EntityRef>())
					{
						if (Entity* e = ignore.as<EntityRef>().Get())
							opts.ignore.insert(e);
					}
					else if (ignore.is<sol::table>())
					{
						for (auto& [k, v] : ignore.as<sol::table>())
						{
							if (v.is<EntityRef>())
								if (Entity* e = v.as<EntityRef>().Get())
									opts.ignore.insert(e);
						}
					}
				}

				if (ignoreSelf)
				{
					if (LuaScriptInstance* inst = g_pScriptEngine->GetCurrentInstance(); inst != nullptr && inst->GetOwner() != nullptr)
						if (Entity* self = inst->GetOwner()->GetEntity())
							opts.ignore.insert(self);
				}

				RayHit hit;
				if (!PhysUtils::RayCast(s, origin, direction, opts, &hit))
					return sol::make_object(lua, sol::lua_nil);

				sol::table t = lua.create_table();
				t["entity"] = hit.entity != nullptr ? sol::make_object(lua, EntityRef::From(hit.entity)) : sol::make_object(lua, sol::lua_nil);
				t["position"] = hit.position;
				t["normal"] = hit.normal;
				t["distance"] = hit.distance;
				return t;
			});
		}

		// ---- Input / Time / Log ---------------------------------------------------------

		void RegisterInputTimeLog(sol::state& lua)
		{
			sol::table input = lua.create_named_table("Input");
			input.set_function("IsKeyDown", [](const sol::object& key) { return g_pScriptEngine->IsKeyDown(ToVk(key)); });
			input.set_function("WasKeyPressed", [](const sol::object& key) { return g_pScriptEngine->WasKeyPressed(ToVk(key)); });
			input.set_function("WasKeyReleased", [](const sol::object& key) { return g_pScriptEngine->WasKeyReleased(ToVk(key)); });
			// 0 = left, 1 = right, 2 = middle
			input.set_function("IsMouseButtonDown", [](int button) {
				const int32_t vk = button == 1 ? VK_RBUTTON : button == 2 ? VK_MBUTTON : VK_LBUTTON;
				return g_pScriptEngine->IsKeyDown(vk);
			});
			input.set_function("GetMouseDelta", []() {
				const math::Vector2 d = g_pScriptEngine->GetMouseDelta();
				return std::make_tuple(d.x, d.y);
			});
			input.set_function("GetMousePosition", []() {
				int x = 0, y = 0;
				if (g_pEnv != nullptr && g_pEnv->_inputSystem != nullptr)
					g_pEnv->_inputSystem->GetGameMousePosition(x, y);
				return std::make_tuple(x, y);
			});

			sol::table time = lua.create_named_table("Time");
			time.set_function("GetDeltaTime", []() { return g_pEnv->_timeManager->GetGameFrameTime(); });
			time.set_function("GetFixedDeltaTime", []() { return FixedDeltaTime(); });
			time.set_function("GetTime", []() { return g_pScriptEngine->GetGameTime(); });
			time.set_function("GetRealTime", []() { return g_pEnv->_timeManager->GetTime(); });
			time.set_function("GetTimeScale", []() { return g_pEnv->_timeManager->_timeScale; });
			time.set_function("IsPaused", []() { return g_pEnv->_timeManager->_simulationPaused; });

			sol::table log = lua.create_named_table("Log");
			log.set_function("Info", [](sol::variadic_args args, sol::this_state ts) {
				const std::string line = JoinArgs(args, ts);
				LOG_INFO("[Script] %s: %s", g_pScriptEngine->DescribeContext().c_str(), line.c_str());
			});
			log.set_function("Warn", [](sol::variadic_args args, sol::this_state ts) {
				const std::string line = JoinArgs(args, ts);
				LOG_WARN("[Script] %s: %s", g_pScriptEngine->DescribeContext().c_str(), line.c_str());
			});
			log.set_function("Error", [](sol::variadic_args args, sol::this_state ts) {
				const std::string line = JoinArgs(args, ts);
				LOG_WARN("[Script] %s: error: %s", g_pScriptEngine->DescribeContext().c_str(), line.c_str());
			});
		}
	}

	void RegisterBindings(sol::state& lua)
	{
		RegisterMath(lua);
		RegisterEntity(lua);
		RegisterScene(lua);
		RegisterInputTimeLog(lua);
	}
}
