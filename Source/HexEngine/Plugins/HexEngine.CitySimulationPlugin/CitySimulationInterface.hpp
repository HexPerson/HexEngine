#pragma once

#include <HexEngine.Core/HexEngine.hpp>

class CitySimulationInterface : public HexEngine::IPluginInterface
{
public:
	static inline const char* InterfaceName = "HexEngine.CitySimulationPlugin.Interface.v1";

	virtual bool Create() override;
	virtual void Destroy() override;
	bool OnEntityDuplicated(HexEngine::Entity* sourceEntity, HexEngine::Entity* duplicatedEntity);
	bool PlaceNextRoadSectionFromEntity(HexEngine::Entity* currentSectionEntity, HexEngine::Entity** outPlacedRoot = nullptr);

private:
	void LogRegistrationSummary() const;
	bool _registered = false;
};
