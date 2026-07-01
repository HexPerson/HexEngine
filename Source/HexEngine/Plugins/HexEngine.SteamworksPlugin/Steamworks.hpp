
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

	// Lobbies / invites -------------------------------------------------------
	virtual bool CreateLobby(int32_t maxMembers) override;
	virtual void LeaveLobby() override;
	virtual bool IsInLobby() const override { return _lobbyId.IsValid(); }
	virtual bool IsLobbyHost() const override { return _isLobbyHost; }
	virtual uint64_t GetLobbyId() const override { return _lobbyId.ConvertToUint64(); }
	virtual uint64_t GetLobbyOwner() const override;
	virtual void OpenInviteOverlay() override;
	virtual bool ConsumePendingHostStart() override;
	virtual bool ConsumePendingClientConnect(uint64_t& outHostSteamId) override;

private:
	bool _initialised = false;
	bool _overlayActive = false;

	// Lobby state (invalid CSteamID _lobbyId when not in a lobby).
	CSteamID _lobbyId;
	bool     _isLobbyHost = false;
	bool     _pendingHostStart = false;     // set on lobby-created; drained by the net bridge
	bool     _pendingClientConnect = false; // set on entering a lobby as a client
	uint64_t _pendingConnectHost = 0;

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

	// Lobby callbacks. LobbyEnter fires for both host and joiner; GameLobbyJoin-
	// Requested fires when a friend accepts an invite from the overlay/friends
	// list. CreateLobby's result comes back via the _lobbyCreatedResult call-result.
	STEAM_CALLBACK(Steamworks, OnLobbyEntered,          LobbyEnter_t);
	STEAM_CALLBACK(Steamworks, OnGameLobbyJoinRequested, GameLobbyJoinRequested_t);
	CCallResult<Steamworks, LobbyCreated_t> _lobbyCreatedResult;
	void OnLobbyCreated(LobbyCreated_t* pResult, bool bIOFailure);
};
