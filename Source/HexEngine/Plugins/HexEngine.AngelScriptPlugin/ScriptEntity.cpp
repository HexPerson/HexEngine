
#include "ScriptEntity.hpp"

ScriptEntity::ScriptEntity(Entity* entity) :
	_entity(entity),
	_weakRefFlag(nullptr)
{}

asILockableSharedBool* ScriptEntity::GetWeakRefFlag()
{
	if (!_weakRefFlag)
		_weakRefFlag = asCreateLockableSharedBool();

	return _weakRefFlag;
}

int ScriptEntity::AddRef()
{
	return ++_refCount;
}

int ScriptEntity::Release()
{
	if (--_refCount == 0)
	{
		delete this;
		return 0;
	}
	return _refCount;
}

bool ScriptEntity::IsLookingAt(ScriptEntity* other, float maxDistance)
{
	auto thisEntity = _entity;
	auto otherEntity = other->_entity;

	auto thisTrans = thisEntity->GetComponent<Transform>();
	auto otherTrans = otherEntity->GetComponent<Transform>();

	RayHit hit;
	
	if (PhysUtils::RayCast(
		thisTrans->GetPosition(),
		thisTrans->GetPosition() + thisTrans->GetForward() * maxDistance,
		LAYERMASK(Layer::DynamicGeometry) | LAYERMASK(Layer::StaticGeometry),
		&hit))
	{
		if (hit.entity == otherEntity)
			return true;
	}

	return false;
}

BaseComponent* ScriptEntity::GetComponent(const std::string& name)
{
	return _entity->GetComponentByClassName(name);
}