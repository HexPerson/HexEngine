

#pragma once

#include "ToolBase.hpp"

class FoliageTool : public ToolBase
{
public:
	virtual void Create() override;

	virtual void Update(int x, int y) override;

private:
	std::vector<HexEngine::Model*> _models;
	std::vector<HexEngine::Entity*> _placedEntities;
	float _lastPlaceTime = 0.0f;
};
