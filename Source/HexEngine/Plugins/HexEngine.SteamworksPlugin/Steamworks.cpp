
#include "Steamworks.hpp"

// === Lifecycle ===

bool Steamworks::Create()
{
	// SteamAPI_Init returns false when:
	//   - Steam isn't running
	//   - The game wasn't launched through Steam AND no steam_appid.txt is in
	//     the bin directory matching the app id we registered
	//   - DRM rejected the launch (wrong user / family share restriction / etc)
	//
	// All of these are "we're not running under Steam" situations rather than
	// programming errors, so we return false to Game3DEnvironment which then
	// silently drops the provider pointer instead of crashing.
	_initialised = SteamAPI_Init();
	if (!_initialised)
	{
		LOG_WARN("SteamAPI_Init returned false; Steamworks features disabled this run");
		return false;
	}

	LOG_INFO("SteamAPI_Init succeeded; signed in as '%s' (SteamID %llu)",
		SteamFriends() != nullptr ? SteamFriends()->GetPersonaName() : "<unknown>",
		SteamUser() != nullptr ? SteamUser()->GetSteamID().ConvertToUint64() : 0ull);

	// Kick off the async stats fetch. Achievement / stat queries before the
	// UserStatsReceived_t callback fires will return safe defaults; the
	// callback flips _statsReady once Steam delivers.
	if (auto* stats = SteamUserStats(); stats != nullptr)
		stats->RequestCurrentStats();

	return true;
}

void Steamworks::Destroy()
{
	if (!_initialised)
		return;
	SteamAPI_Shutdown();
	_initialised = false;
}

// === Tick ===

void Steamworks::Tick()
{
	if (!_initialised)
		return;
	SteamAPI_RunCallbacks();
}

// === User identity ===

uint64_t Steamworks::GetSteamID() const
{
	if (!_initialised)
		return 0ull;
	auto* user = SteamUser();
	return (user != nullptr) ? user->GetSteamID().ConvertToUint64() : 0ull;
}

std::string Steamworks::GetPersonaName() const
{
	if (!_initialised)
		return std::string();
	auto* friends = SteamFriends();
	if (friends == nullptr)
		return std::string();
	const char* name = friends->GetPersonaName();
	return (name != nullptr) ? std::string(name) : std::string();
}

// === Achievements ===

bool Steamworks::UnlockAchievement(const std::string& apiName)
{
	if (!_initialised || !_statsReady)
		return false;
	auto* stats = SteamUserStats();
	if (stats == nullptr)
		return false;

	// SetAchievement is a no-op + returns true when the achievement is already
	// unlocked, which is fine - callers can safely re-call on every kill /
	// pickup without worrying about toast-spam. The Steam toast only fires
	// once.
	if (!stats->SetAchievement(apiName.c_str()))
		return false;

	// Required: stats need to be stored to actually persist + fire the toast.
	// Callers can batch via the StoreStats method but the typical "you just
	// unlocked something" path benefits from immediate persistence.
	return stats->StoreStats();
}

bool Steamworks::IsAchievementUnlocked(const std::string& apiName) const
{
	if (!_initialised || !_statsReady)
		return false;
	auto* stats = SteamUserStats();
	if (stats == nullptr)
		return false;
	bool unlocked = false;
	if (!stats->GetAchievement(apiName.c_str(), &unlocked))
		return false;
	return unlocked;
}

bool Steamworks::ClearAchievement(const std::string& apiName)
{
	if (!_initialised || !_statsReady)
		return false;
	auto* stats = SteamUserStats();
	if (stats == nullptr)
		return false;
	if (!stats->ClearAchievement(apiName.c_str()))
		return false;
	return stats->StoreStats();
}

// === Stats ===

bool Steamworks::GetStatInt(const std::string& apiName, int32_t& outValue) const
{
	if (!_initialised || !_statsReady)
		return false;
	auto* stats = SteamUserStats();
	return stats != nullptr && stats->GetStat(apiName.c_str(), &outValue);
}

bool Steamworks::GetStatFloat(const std::string& apiName, float& outValue) const
{
	if (!_initialised || !_statsReady)
		return false;
	auto* stats = SteamUserStats();
	return stats != nullptr && stats->GetStat(apiName.c_str(), &outValue);
}

bool Steamworks::SetStatInt(const std::string& apiName, int32_t value)
{
	if (!_initialised || !_statsReady)
		return false;
	auto* stats = SteamUserStats();
	return stats != nullptr && stats->SetStat(apiName.c_str(), value);
}

bool Steamworks::SetStatFloat(const std::string& apiName, float value)
{
	if (!_initialised || !_statsReady)
		return false;
	auto* stats = SteamUserStats();
	return stats != nullptr && stats->SetStat(apiName.c_str(), value);
}

bool Steamworks::StoreStats()
{
	if (!_initialised)
		return false;
	auto* stats = SteamUserStats();
	return stats != nullptr && stats->StoreStats();
}

// === Rich presence ===

bool Steamworks::SetRichPresence(const std::string& key, const std::string& value)
{
	if (!_initialised)
		return false;
	auto* friends = SteamFriends();
	if (friends == nullptr)
		return false;
	// Steam treats SetRichPresence(key, nullptr or "") as a clear of the key,
	// per the SDK docs. Passing the empty string from std::string follows that
	// convention naturally.
	return friends->SetRichPresence(key.c_str(), value.c_str());
}

void Steamworks::ClearRichPresence()
{
	if (!_initialised)
		return;
	if (auto* friends = SteamFriends(); friends != nullptr)
		friends->ClearRichPresence();
}

// === Callbacks ===

void Steamworks::OnGameOverlayActivated(GameOverlayActivated_t* pCallback)
{
	if (pCallback == nullptr)
		return;
	_overlayActive = pCallback->m_bActive != 0;

	// Pause input when the overlay is up so the player doesn't accidentally
	// drive game controls while typing in chat / browsing. Matches the
	// behaviour of every commercial Steam title.
	if (HexEngine::g_pEnv != nullptr && HexEngine::g_pEnv->_inputSystem != nullptr)
		HexEngine::g_pEnv->_inputSystem->EnableInput(!_overlayActive);
}

void Steamworks::OnUserStatsReceived(UserStatsReceived_t* pCallback)
{
	if (pCallback == nullptr)
		return;
	// Filter to our app's stats (the same callback can fire for arbitrary
	// other apps when something else on the system queries them).
	if (SteamUtils() != nullptr && pCallback->m_nGameID != SteamUtils()->GetAppID())
		return;
	if (pCallback->m_eResult != k_EResultOK)
	{
		LOG_WARN("Steamworks: RequestCurrentStats failed with EResult=%d", (int)pCallback->m_eResult);
		return;
	}
	_statsReady = true;
	LOG_INFO("Steamworks: user stats ready");
}

void Steamworks::OnUserStatsStored(UserStatsStored_t* pCallback)
{
	if (pCallback == nullptr)
		return;
	if (SteamUtils() != nullptr && pCallback->m_nGameID != SteamUtils()->GetAppID())
		return;
	if (pCallback->m_eResult != k_EResultOK)
	{
		// k_EResultInvalidParam usually means the stat was clamped to its
		// configured range on the Steamworks dashboard side - the local value
		// got rejected. Not a hard error; logging at warn level so devs can
		// spot mis-tuned stat ranges.
		LOG_WARN("Steamworks: StoreStats failed with EResult=%d", (int)pCallback->m_eResult);
	}
}

void Steamworks::OnAchievementStored(UserAchievementStored_t* pCallback)
{
	if (pCallback == nullptr)
		return;
	if (SteamUtils() != nullptr && pCallback->m_nGameID != SteamUtils()->GetAppID())
		return;
	// Toast already shown by Steam; nothing for us to do here other than
	// logging for visibility while debugging.
	LOG_INFO("Steamworks: achievement '%s' stored (max progress %u of %u)",
		pCallback->m_rgchAchievementName,
		pCallback->m_nCurProgress,
		pCallback->m_nMaxProgress);
}
