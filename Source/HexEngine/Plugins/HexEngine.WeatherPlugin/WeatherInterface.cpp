#include "WeatherInterface.hpp"

#include "WeatherControllerComponent.hpp"
#include "WeatherZoneComponent.hpp"

namespace HexEngine::Weather
{
	bool WeatherInterface::Create()
	{
		if (_registered)
			return true;

		REG_CLASS(WeatherControllerComponent);
		REG_CLASS(WeatherZoneComponent);
		_registered = true;
		return true;
	}

	void WeatherInterface::Destroy()
	{
		_registered = false;
	}

	Entity* WeatherInterface::CreateWeatherControllerEntity(Scene* scene, const math::Vector3& position, const std::string& name)
	{
		if (scene == nullptr)
			return nullptr;

		Entity* entity = scene->CreateEntity(name, position);
		if (entity != nullptr)
			entity->AddComponent<WeatherControllerComponent>();
		return entity;
	}

	Entity* WeatherInterface::CreateWeatherZoneEntity(Scene* scene, const math::Vector3& position, const std::string& name)
	{
		if (scene == nullptr)
			return nullptr;

		Entity* entity = scene->CreateEntity(name, position);
		if (entity != nullptr)
			entity->AddComponent<WeatherZoneComponent>();
		return entity;
	}
}
