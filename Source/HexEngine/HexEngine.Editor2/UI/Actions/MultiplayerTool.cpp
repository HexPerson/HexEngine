#include "MultiplayerTool.hpp"
#include "../../Editor.hpp"
#include "../EditorUI.hpp"

#include <HexEngine.Core/GUI/Elements/ComponentWidget.hpp>
#include <HexEngine.Core/GUI/Elements/Button.hpp>
#include <HexEngine.Core/GUI/Elements/LineEdit.hpp>
#include <HexEngine.Core/GUI/Elements/DragInt.hpp>
#include <HexEngine.Core/Network/INetworkSystem.hpp>
#include <HexEngine.Core/Steam/ISteamworksProvider.hpp>
#include <HexEngine.Core/Scene/Scene.hpp>
#include <HexEngine.Core/Scene/SceneManager.hpp>
#include <HexEngine.Core/Scene/NetworkReplicationSystem.hpp>

#include <string>

namespace HexEditor
{
	MultiplayerTool::MultiplayerTool(Element* parent, const HexEngine::Point& position, const HexEngine::Point& size) :
		Dialog(parent, position, size, L"Multiplayer")
	{
		_settings = new HexEngine::ComponentWidget(this, HexEngine::Point(10, 10), HexEngine::Point(size.x - 20, -1), L"Session");
	}

	MultiplayerTool::~MultiplayerTool()
	{
	}

	MultiplayerTool* MultiplayerTool::CreateEditorDialog(Element* parent)
	{
		uint32_t width = 0, height = 0;
		HexEngine::g_pEnv->GetScreenSize(width, height);

		const int32_t sizeX = 440;
		const int32_t sizeY = 380;

		auto* dialog = new MultiplayerTool(
			parent,
			HexEngine::Point((int32_t)width / 2 - sizeX / 2, (int32_t)height / 2 - sizeY / 2),
			HexEngine::Point(sizeX, sizeY));

		const int32_t cw = sizeX - 40;
		const HexEngine::Point row(cw, 18);
		const HexEngine::Point btn(cw, 25);

		dialog->_address = new HexEngine::LineEdit(dialog->_settings, dialog->_settings->GetNextPos(), row, L"Address");
		dialog->_address->SetValue(L"127.0.0.1");
		new HexEngine::DragInt(dialog->_settings, dialog->_settings->GetNextPos(), row, L"Port", &dialog->_port, 1, 65535, 1);
		dialog->_playerPrefab = new HexEngine::LineEdit(dialog->_settings, dialog->_settings->GetNextPos(), row, L"Player Prefab");

		new HexEngine::Button(dialog->_settings, dialog->_settings->GetNextPos(), btn, L"Host (Direct IP)",
			[dialog](HexEngine::Button*)
			{
				dialog->ApplyPlayerPrefab();
				if (auto* net = HexEngine::g_pEnv->_networkSystem)
					net->StartHost((uint16_t)dialog->_port);
				else
					LOG_WARN("Multiplayer: no networking plugin loaded.");
				return true;
			});

		new HexEngine::Button(dialog->_settings, dialog->_settings->GetNextPos(), btn, L"Connect (Direct IP)",
			[dialog](HexEngine::Button*)
			{
				if (auto* net = HexEngine::g_pEnv->_networkSystem)
				{
					const std::wstring ws = dialog->_address->GetValue();
					const std::string addr(ws.begin(), ws.end());
					net->Connect(addr, (uint16_t)dialog->_port);
				}
				else
				{
					LOG_WARN("Multiplayer: no networking plugin loaded.");
				}
				return true;
			});

		new HexEngine::Button(dialog->_settings, dialog->_settings->GetNextPos(), btn, L"Host on Steam (create lobby)",
			[dialog](HexEngine::Button*)
			{
				dialog->ApplyPlayerPrefab();
				if (auto* sw = HexEngine::g_pEnv->_steamworksProvider)
					sw->CreateLobby(4); // net bridge auto-StartHostP2P on lobby-created (needs net_backend=2)
				else
					LOG_WARN("Multiplayer: Steamworks not available (run under Steam with net_backend=2).");
				return true;
			});

		new HexEngine::Button(dialog->_settings, dialog->_settings->GetNextPos(), btn, L"Invite Friends (Steam overlay)",
			[](HexEngine::Button*)
			{
				if (auto* sw = HexEngine::g_pEnv->_steamworksProvider)
					sw->OpenInviteOverlay();
				else
					LOG_WARN("Multiplayer: Steamworks not available.");
				return true;
			});

		new HexEngine::Button(dialog->_settings, dialog->_settings->GetNextPos(), btn, L"Disconnect / Leave",
			[](HexEngine::Button*)
			{
				if (auto* net = HexEngine::g_pEnv->_networkSystem)
					net->Disconnect();
				if (auto* sw = HexEngine::g_pEnv->_steamworksProvider)
					sw->LeaveLobby();
				return true;
			});

		dialog->_status = new HexEngine::LineEdit(dialog->_settings, dialog->_settings->GetNextPos(), row, L"Status");
		dialog->RefreshStatus();

		dialog->BringToFront();
		return dialog;
	}

	void MultiplayerTool::ApplyPlayerPrefab()
	{
		if (_playerPrefab == nullptr)
			return;
		auto scene = HexEngine::g_pEnv->_sceneManager->GetCurrentScene();
		if (!scene)
			return;
		const std::wstring ws = _playerPrefab->GetValue();
		const std::string path(ws.begin(), ws.end());
		scene->GetNetworkReplicationSystem()->SetPlayerPrefabPath(path);
	}

	void MultiplayerTool::RefreshStatus()
	{
		if (_status == nullptr)
			return;

		auto* net = HexEngine::g_pEnv->_networkSystem;
		auto* sw = HexEngine::g_pEnv->_steamworksProvider;

		std::wstring s;
		if (net == nullptr)
		{
			s = L"No networking plugin loaded";
		}
		else
		{
			const wchar_t* role = L"None";
			switch (net->GetRole())
			{
			case HexEngine::INetworkSystem::NetRole::Host:   role = L"Host";   break;
			case HexEngine::INetworkSystem::NetRole::Client: role = L"Client"; break;
			default: break;
			}

			std::vector<uint32_t> conns;
			net->GetConnections(conns);

			s = std::wstring(L"Role: ") + role + L"  Conns: " + std::to_wstring(conns.size());
			if (net->SupportsP2P())
				s += L"  MyID: " + std::to_wstring(net->GetLocalIdentity());
		}

		if (sw != nullptr && sw->IsInLobby())
			s += L"  Lobby: " + std::to_wstring(sw->GetLobbyId());

		_status->SetUneditableText(s);
	}

	void MultiplayerTool::Render(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		RefreshStatus();
		HexEngine::Dialog::Render(renderer, w, h);
	}
}
