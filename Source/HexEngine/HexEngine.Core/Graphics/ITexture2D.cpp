

#include "ITexture2D.hpp"
#include "../HexEngine.hpp"

namespace HexEngine
{
	ITexture2D* ITexture2D::GetDefaultTexture()
	{
		static ITexture2D* sDefaultTexture = (HexEngine::ITexture2D*)HexEngine::g_pEnv->_resourceSystem->LoadResource("EngineData.Textures/white.png");

		sDefaultTexture->AddRef();
		return sDefaultTexture;
	}

	ITexture2D* ITexture2D::Create(const fs::path& absolutePath)
	{
		return (ITexture2D*)g_pEnv->_resourceSystem->LoadResource(absolutePath);
	}
}