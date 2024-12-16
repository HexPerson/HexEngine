

#include "Steamworks.hpp"

bool Steamworks::Create()
{
	return SteamAPI_Init();
}

void Steamworks::Destroy()
{
	SteamAPI_Shutdown();
}

void Steamworks::OnGameOverlayActivated(GameOverlayActivated_t* pCallback)
{
	if (pCallback->m_bActive)
		g_pEnv->_inputSystem->EnableInput(false);
	else
		g_pEnv->_inputSystem->EnableInput(true);
}