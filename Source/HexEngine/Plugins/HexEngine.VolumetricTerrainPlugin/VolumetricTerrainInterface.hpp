#pragma once

#include <HexEngine.Core/HexEngine.hpp>

namespace HexEngine::VolumetricTerrain
{
	class VolumetricTerrainInterface : public IPluginInterface
	{
	public:
		DECLARE_PLUGIN_INTERFACE(VolumetricTerrainInterface, 001);

		virtual bool Create() override;
		virtual void Destroy() override;

		Entity* CreateVolumetricTerrainEntity(Scene* scene, const math::Vector3& position, const std::string& name = "VolumetricTerrain");

	private:
		bool _registered = false;
	};
}
