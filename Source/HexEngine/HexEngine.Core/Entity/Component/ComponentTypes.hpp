
#pragma once

#include "../../Environment/IEnvironment.hpp"
#include "../../Environment/ClassRegistry.hpp"
#include "ComponentSignature.hpp"

namespace HexEngine
{

#define CREATE_COMPONENT_ID(type)\
static HexEngine::ComponentId _GetComponentId() {\
	static HexEngine::ComponentId sComponentId_##type = HexEngine::g_pEnv->_classRegistry->Find(ConstCRC32(#type))->compId;\
	return sComponentId_##type;\
}\
static const char* _GetComponentName() {\
	return #type;\
}\
virtual HexEngine::ComponentId GetComponentId() override {\
	if(_componentId == -1)\
		_componentId = std::decay_t<decltype(*this)>::_GetComponentId();\
	return _componentId;\
}\
virtual const char* GetComponentName() override {\
	return std::decay_t<decltype(*this)>::_GetComponentName();\
}
}
