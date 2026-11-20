#pragma once

// sol2 configuration. Lua is built as C++ (vcpkg lua[cpp] -> lua-c++.lib), so
// Lua errors unwind as C++ exceptions and C++ destructors run when a script
// errors. These are also set in the vcxproj; repeated here so a stray include
// order can't pick different settings.
#ifndef SOL_USING_CXX_LUA
#define SOL_USING_CXX_LUA 1
#endif
#ifndef SOL_ALL_SAFETIES_ON
#define SOL_ALL_SAFETIES_ON 1
#endif
#ifndef SOL_PRINT_ERRORS
#define SOL_PRINT_ERRORS 0
#endif

#include <HexEngine.Core\HexEngine.hpp>
#include <sol/sol.hpp>

namespace HexScripting
{
	// A script's reference to an entity: scene + generational id, resolved on
	// every use, so a handle to a destroyed entity is simply invalid instead of
	// dangling.
	struct EntityRef
	{
		HexEngine::Scene* scene = nullptr;
		HexEngine::EntityId id = HexEngine::InvalidEntityId;

		HexEngine::Entity* Get() const;
		static EntityRef From(HexEngine::Entity* entity);
	};

	struct LightRef { EntityRef entity; HexEngine::Light* Get() const; };
	struct RigidBodyRef { EntityRef entity; HexEngine::RigidBody* Get() const; };

	// UTF-8 <-> wide (paths are UTF-8 on the Lua side).
	std::wstring Utf8ToWide(const std::string& s);
	std::string WideToUtf8(const std::wstring& s);

	// "W" / "Space" / "F1" / "Mouse1" ... <-> virtual-key code. 0 when unknown.
	int32_t KeyNameToVk(const std::string& name);
	std::string VkToKeyName(int32_t vk);

	// Lua value <-> JSON (properties and generic component fields). `like`
	// steers the conversion (e.g. a 3-array becomes a Vec3 when the default is one).
	json LuaToJson(const sol::object& value, bool& ok);
	sol::object JsonToLua(sol::state_view lua, const json& value, const json& like);

	// Lua errors carry a long traceback; keep the message and the first frames.
	std::string TrimTraceback(const std::string& error, size_t maxLines = 8);

	// Registers Vec3, Quat, Entity, Light, RigidBody, Scene, Input, Time, Log.
	void RegisterBindings(sol::state& lua);
}
