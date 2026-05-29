
#pragma once

#define _CRT_SECURE_NO_WARNINGS

#include <HexEngine.Core/HexEngine.hpp>
#include "sdk/steam_api.h"

class Steamworks : public HexEngine::ISteamworksProvider
{
public:
	// IPluginInterface --------------------------------------------------------
	virtual bool Create() override;
	virtual void Destroy() override;

	// ISteamworksProvider -----------------------------------------------------
	virtual bool IsInitialised() const override { return _initialised; }
	virtual void Tick() override;

	virtual uint64_t    GetSteamID() const override;
	virtual std::string GetPersonaName() const override;

	virtual bool UnlockAchievement(const std::string& apiName) override;
	virtual bool IsAchievementUnlocked(const std::string& apiName) const override;
	virtual bool ClearAchievement(const std::string& apiName) override;

	virtual bool GetStatInt(const std::string& apiName, int32_t& outValue) const override;
	virtual bool GetStatFloat(const std::string& apiName, float& outValue) const override;
	virtual bool SetStatInt(const std::string& apiName, int32_t value) override;
	virtual bool SetStatFloat(const std::string& apiName, float value) override;
	virtual bool StoreStats() override;

	virtual bool SetRichPresence(const std::string& key, const std::string& value) override;
	virtual void ClearRichPresence() override;

	virtual bool IsOverlayActive() const override { return _overlayActive; }

private:
	bool _initialised = false;
	bool _overlayActive = false;

	// True after ISteamUserStats::RequestCurrentStats has returned (delivered
	// via the UserStatsReceived_t callback). Achievement / stat queries before
	// this fires return safe defaults rather than reading stale local state.
	bool _statsReady = false;

	// Steam callbacks. Registered via STEAM_CALLBACK so the callback machinery
	// (pumped by SteamAPI_RunCallbacks in Tick()) auto-dispatches them.
	STEAM_CALLBACK(Steamworks, OnGameOverlayActivated, GameOverlayActivated_t);
	STEAM_CALLBACK(Steamworks, OnUserStatsReceived,    UserStatsReceived_t);
	STEAM_CALLBACK(Steamworks, OnUserStatsStored,      UserStatsStored_t);
	STEAM_CALLBACK(Steamworks, OnAchievementStored,    UserAchievementStored_t);
};
