
#pragma once

#include <HexEngine.Core\HexEngine.hpp>
#include <angelscript.h>

class ScriptEntity
{
public:
	ScriptEntity(Entity* entity);

	asILockableSharedBool* GetWeakRefFlag();

	int AddRef();

	int Release();

	bool IsLookingAt(ScriptEntity* other, float maxDistance);

	BaseComponent* GetComponent(const std::string& name);

private:
	Entity* _entity;
	int _refCount;
	asILockableSharedBool* _weakRefFlag;
};
