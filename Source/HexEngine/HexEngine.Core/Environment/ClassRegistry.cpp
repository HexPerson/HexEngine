

#include "ClassRegistry.hpp"

#include "../Entity/Component/PointLight.hpp"
#include "../Entity/Component/RTSCameraController.hpp"
#include "../Entity/Component/SphereCollider.hpp"
#include "../Entity/Component/TimedLifetimeComponent.hpp"
#include "../Entity/Component/MotorComponent.hpp"
#include "../Entity/Component/Billboard.hpp"
#include "../Entity/Component/ThirdPersonCameraController.hpp"
#include "../Entity/Component/TimedLifetimeComponent.hpp"
#include "../Entity/Component/HingeJoint.hpp"
#include "../Entity/Component/ScriptComponent.hpp"
#include "../HexEngine.hpp"


namespace HexEngine
{
	class FirstPersonCameraController;
	class PointLight;
	class InteractionComponent;
	class RTSCameraController;
	class SphereCollider;
	class TerrainCollider;
	class TimedLifetimeComponent;
	class SpotLight;

	ClassRegistry::ClassRegistry()
	{
	}

	void ClassRegistry::RegisterAllClasses()
	{
		REG_CLASS(Camera);
		REG_CLASS(DirectionalLight);
		REG_CLASS(FirstPersonCameraController);
		REG_CLASS(StaticMeshComponent);
		REG_CLASS(PointLight);
		REG_CLASS(RigidBody);
		REG_CLASS(InteractionComponent);		
		REG_CLASS(Transform);
		REG_CLASS(UpdateComponent);
		REG_CLASS(SpotLight);
		REG_CLASS(MotorComponent);
		REG_CLASS(Billboard);
		REG_CLASS(RTSCameraController);
		REG_CLASS(ThirdPersonCameraController);
		//REG_CLASS(SphereCollider);
		//REG_CLASS(TerrainCollider);
		REG_CLASS(TimedLifetimeComponent);
		REG_CLASS(HingeJoint);
		REG_CLASS(ScriptComponent);
	}

	uint32_t ClassRegistry::Register(uint32_t nameHash, const std::string& name, const type_info& type, CloneInstanceFn cloneInstanceFn, NewInstanceFn newInstanceFn)
	{
		if (auto current = Find(nameHash); current != nullptr)
		{
			LOG_DEBUG("A class with name '%s' has already been registered, ignoring", name.c_str());
			return current->compId;
		}

		Class cls;
		cls.name = name;
		cls.nameHash = nameHash;
		cls.type = &type;
		cls.cloneInstanceFn = cloneInstanceFn;
		cls.newInstanceFn = newInstanceFn;
		cls.compId = _registry.size();

		_registry[nameHash] = cls;

		LOG_INFO("Registered class '%s' with ID: %d", name.c_str(), cls.compId);
		return cls.compId;
	}

	ClassRegistry::Class* ClassRegistry::Find(uint32_t nameHash)
	{
		for (auto& it : _registry)
		{
			if (it.first == nameHash)
				return &it.second;
		}

		return nullptr;
	}

	ClassRegistry::Class* ClassRegistry::Find(const std::string& name)
	{
		for (auto& it : _registry)
		{
			if (it.second.name == name)
				return &it.second;
		}

		return nullptr;
	}

	ClassRegistry::Class* ClassRegistry::FindByComponentId(uint32_t componentId)
	{
		for (auto& it : _registry)
		{
			if (it.second.compId == componentId)
				return &it.second;
		}

		return nullptr;
	}

	const std::map<uint32_t, ClassRegistry::Class>& ClassRegistry::GetAllClasses() const
	{
		return _registry;
	}
}