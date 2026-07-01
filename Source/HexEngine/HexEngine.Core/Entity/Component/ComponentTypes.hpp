
#pragma once

#include "../../Environment/IEnvironment.hpp"
#include "../../Environment/ClassRegistry.hpp"

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

	using ComponentId = uint32_t;
	// 64 component types (up from 32). Game/plugin DLLs share this global registry
	// and we keep adding components, so the signature is a 64-bit mask. IMPORTANT:
	// every `signature bit` must be built as ((ComponentSignature)1 << compId) -
	// a bare `1 << compId` is a signed int and sign-extends past bit 31.
	const ComponentId MAX_COMPONENTS = 64;
	using ComponentSignature = uint64_t;
}