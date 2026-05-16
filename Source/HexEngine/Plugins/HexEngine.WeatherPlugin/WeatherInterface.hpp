#pragma once

#include <HexEngine.Core/HexEngine.hpp>

namespace HexEngine::Weather
{
	class WeatherControllerComponent;
	class WeatherZoneComponent;

	class WeatherInterface final : public IPluginInterface
	{
	public:
		static inline const char* InterfaceName = "HexEngine.WeatherPlugin.Interface.v1";

		virtual bool Create() override;
		virtual void Destroy() override;

		Entity* CreateWeatherControllerEntity(Scene* scene, const math::Vector3& position = math::Vector3::Zero, const std::string& name = "WeatherController");
		Entity* CreateWeatherZoneEntity(Scene* scene, const math::Vector3& position = math::Vector3::Zero, const std::string& name = "WeatherZone");

	private:
		bool _registered = false;
	};
}
