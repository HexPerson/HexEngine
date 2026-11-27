
#pragma once

#include "../Required.hpp"
#include <type_traits>

namespace HexEngine
{
	class BaseComponent;
	class Entity;

	class HEX_API ClassRegistry
	{
	public:
		using CloneInstanceFn = std::function<BaseComponent* (Entity*, BaseComponent*)>;
		using NewInstanceFn = std::function<BaseComponent* (Entity*)>;

		struct Class
		{
			uint32_t nameHash;
			std::string name;
			const type_info* type;
			CloneInstanceFn cloneInstanceFn;
			NewInstanceFn newInstanceFn;
			uint32_t compId;
			// Whether the type overrides BaseComponent::OnDebugRender, so
			// Scene::RenderDebug only visits pools that draw something. Kept
			// LAST (other modules read compId at its existing offset) and
			// defaulting to true: a module built before REG_CLASS recorded
			// this stays drawn rather than silently going dark.
			bool hasDebugRender = true;
		};

		ClassRegistry();

		void RegisterAllClasses();

		uint32_t Register(uint32_t nameHash, const std::string& name, const type_info& type, CloneInstanceFn cloneInstanceFn, NewInstanceFn newInstanceFn);

		Class* Find(uint32_t nameHash);

		Class* Find(const std::string& name);

		Class* FindByComponentId(uint32_t componentId);

		const std::map<uint32_t, Class>& GetAllClasses() const;

		void SetHasDebugRender(uint32_t nameHash, bool hasDebugRender);

		// Unknown ids answer true (conservative - see Class::hasDebugRender).
		bool HasDebugRender(uint32_t componentId);

		// Hot reload of a module that registered classes (the game DLL).
		// Classes are attributed to the module their type_info lives in.
		std::vector<std::string> GetModuleClassNames(void* moduleHandle) const;
		// Drop the factories of every class owned by `moduleHandle`. Must run
		// while the module is STILL LOADED: the std::functions were built in it,
		// so destroying them executes its code. The entries (and their compIds)
		// stay; re-registering the same name from the reloaded module fills
		// them back in. A released class has empty newInstanceFn/cloneInstanceFn
		// and a null type - callers must check before creating instances.
		void ReleaseModuleClasses(void* moduleHandle);

	private:
		std::map<uint32_t, Class> _registry;
	};

	// True when T (or a base between it and BaseComponent) declares its own
	// OnDebugRender. An inherited override names its declaring class in the
	// member-pointer type, so this is exact at compile time with no vtable
	// inspection.
	template<typename T>
	constexpr bool ComponentOverridesDebugRender()
	{
		return !std::is_same_v<decltype(&T::OnDebugRender), void (BaseComponent::*)()>;
	}

#define REG_CLASS(cls) HexEngine::g_pEnv->_classRegistry->Register(ConstCRC32(#cls), #cls, typeid(cls),\
[](HexEngine::Entity* entity, HexEngine::BaseComponent* copy){ return (HexEngine::BaseComponent*)new cls(entity, (cls*)copy); },\
[](HexEngine::Entity* entity){ return (HexEngine::BaseComponent*)new cls(entity); });\
HexEngine::g_pEnv->_classRegistry->SetHasDebugRender(ConstCRC32(#cls), HexEngine::ComponentOverridesDebugRender<cls>());
}
