#pragma once

#include <HexEngine.Core\HexEngine.hpp>

namespace HexEngine
{
	class ComponentWidget;
	class LineEdit;
	class GuiRenderer;
}

namespace HexEditor
{
	// Editor dialog for testing/driving multiplayer: direct-IP host/join (works
	// on the GameNetworkingSockets backend), Steam lobby host + friend-invite
	// (net_backend=2), disconnect, and a live status readout. All actions go
	// through g_pEnv->_networkSystem / g_pEnv->_steamworksProvider and no-op
	// gracefully when those aren't present.
	class MultiplayerTool : public HexEngine::Dialog
	{
	public:
		MultiplayerTool(Element* parent, const HexEngine::Point& position, const HexEngine::Point& size);
		~MultiplayerTool();

		static MultiplayerTool* CreateEditorDialog(Element* parent);

		// Refresh the live status line every frame.
		virtual void Render(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h) override;

	private:
		void RefreshStatus();
		// Push the Player Prefab path onto the scene's replication system before
		// hosting, so the host spawns a player per connection.
		void ApplyPlayerPrefab();

		HexEngine::ComponentWidget* _settings = nullptr;
		HexEngine::LineEdit* _address     = nullptr;
		HexEngine::LineEdit* _playerPrefab = nullptr;
		HexEngine::LineEdit* _status       = nullptr;
		int32_t _port = 27015;
	};
}
