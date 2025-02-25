
#pragma once

#include "../Required.hpp"

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
		};

		ClassRegistry();

		void RegisterAllClasses();

		uint32_t Register(uint32_t nameHash, const std::string& name, const type_info& type, CloneInstanceFn cloneInstanceFn, NewInstanceFn newInstanceFn);

		Class* Find(uint32_t nameHash);

		Class* Find(const std::string& name);

		Class* FindByComponentId(uint32_t componentId);

		const std::map<uint32_t, Class>& GetAllClasses() const;

	private:
		std::map<uint32_t, Class> _registry;
	};

#define REG_CLASS(cls) HexEngine::g_pEnv->_classRegistry->Register(ConstCRC32(#cls), #cls, typeid(cls),\
[](Entity* entity, BaseComponent* copy){ return (BaseComponent*)new cls(entity, (cls*)copy); },\
[](Entity* entity){ return (BaseComponent*)new cls(entity); });
}
