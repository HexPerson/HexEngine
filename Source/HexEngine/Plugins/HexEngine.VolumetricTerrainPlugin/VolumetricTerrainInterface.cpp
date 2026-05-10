#include "VolumetricTerrainInterface.hpp"
#include "Terrain/VolumetricTerrainComponent.hpp"

namespace HexEngine::VolumetricTerrain
{
	bool VolumetricTerrainInterface::Create()
	{
		if (_registered)
			return true;

		REG_CLASS(VolumetricTerrainComponent);
		_registered = true;
		return true;
	}

	void VolumetricTerrainInterface::Destroy()
	{
		_registered = false;
	}

	Entity* VolumetricTerrainInterface::CreateVolumetricTerrainEntity(Scene* scene, const math::Vector3& position, const std::string& name)
	{
		if (scene == nullptr)
			return nullptr;

		Entity* entity = scene->CreateEntity(name, position);
		if (entity == nullptr)
			return nullptr;

		auto* component = entity->AddComponent<VolumetricTerrainComponent>();
		if (component != nullptr)
		{
			component->InitializeTerrain();
		}

		return entity;
	}
}
