
#pragma once

#include "../Plugin/IPlugin.hpp"

namespace HexEngine
{
	/**
	 * @brief Plugin interface for Steamworks integration.
	 *
	 * Implementing plugin (HexEngine.SteamworksPlugin) wraps the Steam SDK so
	 * game code can call into a stable engine interface without pulling in the
	 * Steam headers directly. When the plugin DLL or steam_api64.dll isn't
	 * present the provider is just absent (g_pEnv->_steamworksProvider == null)
	 * - callers should treat that as "running outside Steam, do nothing".
	 *
	 * Per-frame Tick() is REQUIRED. Without it no Steam callbacks fire,
	 * including the GameOverlayActivated_t signal used to pause game input
	 * when the user opens the Steam overlay.
	 */
	class ISteamworksProvider : public IPluginInterface
	{
	public:
		DECLARE_PLUGIN_INTERFACE(ISteamworksProvider, 001);

		// === Lifecycle ===

		/**
		 * @brief Returns true if SteamAPI_Init succeeded - false means Steam
		 * isn't running, app id mismatch, or DRM rejected the launch. Callers
		 * should treat false the same way they'd treat g_pEnv->_steamworksProvider
		 * being null - feature gracefully disabled.
		 */
		virtual bool IsInitialised() const = 0;

		/**
		 * @brief Pump pending Steam callbacks. Call once per main-thread frame
		 * BEFORE any other Steamworks calls in the frame. Without this
		 * callbacks (overlay state, achievement unlock notifications, etc.)
		 * never fire and certain stat / leaderboard async results time out.
		 */
		virtual void Tick() = 0;

		// === User identity ===

		/** @brief Returns this user's 64-bit Steam ID, or 0 when not initialised. */
		virtual uint64_t GetSteamID() const = 0;

		/** @brief Returns the user's persona (display) name, or an empty string when not initialised. */
		virtual std::string GetPersonaName() const = 0;

		// === Achievements ===

		/**
		 * @brief Unlocks an achievement by its Steamworks-side id (the "API name").
		 * Triggers the Steam toast popup and persists to the user's Steam stats.
		 * Returns false if Steam rejected the call (not initialised, unknown id,
		 * stats not yet ready).
		 */
		virtual bool UnlockAchievement(const std::string& apiName) = 0;

		/** @brief Returns true if the named achievement is already unlocked. */
		virtual bool IsAchievementUnlocked(const std::string& apiName) const = 0;

		/**
		 * @brief Clears (re-locks) an achievement. Mostly useful in dev builds for
		 * testing; production code should normally never call this.
		 */
		virtual bool ClearAchievement(const std::string& apiName) = 0;

		// === Stats ===

		virtual bool GetStatInt(const std::string& apiName, int32_t& outValue) const = 0;
		virtual bool GetStatFloat(const std::string& apiName, float& outValue) const = 0;

		virtual bool SetStatInt(const std::string& apiName, int32_t value) = 0;
		virtual bool SetStatFloat(const std::string& apiName, float value) = 0;

		/**
		 * @brief Flush dirty stats + achievements to Steam. Steam batches these
		 * - call once when the game state genuinely changes (level complete,
		 * save game, etc.) rather than every frame.
		 */
		virtual bool StoreStats() = 0;

		// === Rich presence ===

		/**
		 * @brief Sets a rich-presence key/value pair shown in the friends list
		 * (e.g. "status" = "Exploring Old Town", "level" = "Forest 3"). Passing
		 * an empty value clears the key.
		 */
		virtual bool SetRichPresence(const std::string& key, const std::string& value) = 0;

		/** @brief Clear all rich-presence keys. */
		virtual void ClearRichPresence() = 0;

		// === Overlay state (driven by GameOverlayActivated_t callback) ===

		// === Lobbies / invites (Steam matchmaking, for P2P) ===
		// Non-pure so a provider only implements what it supports. These drive the
		// Steam P2P networking flow: the host creates a lobby and invites friends;
		// an invited friend auto-joins the lobby, and the networking bridge reads
		// the ConsumePending* signals to StartHostP2P / ConnectP2P automatically.

		/** @brief Create a friends-only Steam lobby (async). On success IsInLobby()
		 * becomes true and ConsumePendingHostStart() fires once. */
		virtual bool CreateLobby(int32_t maxMembers) { (void)maxMembers; return false; }

		/** @brief Leave the current lobby (if any). */
		virtual void LeaveLobby() {}

		virtual bool IsInLobby() const { return false; }
		virtual bool IsLobbyHost() const { return false; }
		virtual uint64_t GetLobbyId() const { return 0; }
		virtual uint64_t GetLobbyOwner() const { return 0; }

		/** @brief Open the Steam overlay's invite-friends dialog for the current lobby. */
		virtual void OpenInviteOverlay() {}

		/** @brief One-shot: true exactly once after a lobby is created as host - the
		 * networking bridge then calls INetworkSystem::StartHostP2P(). */
		virtual bool ConsumePendingHostStart() { return false; }

		/** @brief One-shot: true exactly once after entering a lobby as a client;
		 * outHostSteamId is the lobby owner to ConnectP2P() to. */
		virtual bool ConsumePendingClientConnect(uint64_t& outHostSteamId) { (void)outHostSteamId; return false; }

		/**
		 * @brief Returns true when the Steam overlay is currently up. The plugin
		 * automatically pauses input via g_pEnv->_inputSystem->EnableInput(false)
		 * when this transitions to true and re-enables when it transitions to
		 * false; the accessor is here for code that wants to also pause its own
		 * simulation / audio.
		 */
		virtual bool IsOverlayActive() const = 0;
	};
}
