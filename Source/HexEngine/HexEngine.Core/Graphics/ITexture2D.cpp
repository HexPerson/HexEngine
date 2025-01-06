

#include "ITexture2D.hpp"
#include "../HexEngine.hpp"

namespace HexEngine
{
	std::shared_ptr<ITexture2D> ITexture2D::GetDefaultTexture()
	{
		return ITexture2D::Create("EngineData.Textures/white.png");
	}

	std::shared_ptr<ITexture2D> ITexture2D::Create(const fs::path& absolutePath)
	{
		return reinterpret_pointer_cast<ITexture2D>(g_pEnv->_resourceSystem->LoadResource(absolutePath));
	}
}