
#pragma once

#include "../Plugin/IPlugin.hpp"

namespace HexEngine
{
	class ISSAOProvider : public IPluginInterface
	{
	public:
		DECLARE_PLUGIN_INTERFACE(ISSAOProvider, 001);

		virtual void ApplyAmbientOcclusion(Camera* camera, ITexture2D* depthBuffer, ITexture2D* normals, ITexture2D* target) = 0;
	};
}
