
#pragma once

#define _CRT_SECURE_NO_WARNINGS

#include <HexEngine.Core/HexEngine.hpp>
#include "sdk\steam_api.h"

class Steamworks : public IPluginInterface
{
public:
	virtual bool Create() override;

	virtual void Destroy() override;

	STEAM_CALLBACK(Steamworks, OnGameOverlayActivated, GameOverlayActivated_t);
};
