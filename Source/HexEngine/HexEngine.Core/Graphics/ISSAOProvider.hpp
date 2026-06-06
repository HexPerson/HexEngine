
#pragma once

#include "../Plugin/IPlugin.hpp"

namespace HexEngine
{
	class Camera;
	class ITexture2D;

	/** @brief Plugin interface for screen-space ambient occlusion providers. */
	class HEX_API ISSAOProvider : public IPluginInterface
	{
	public:
		DECLARE_PLUGIN_INTERFACE(ISSAOProvider, 001);

		/**
		 * @brief Applies ambient occlusion and writes the result into `target`.
		 * @param camera Active render camera.
		 * @param depthBuffer Depth input texture.
		 * @param normals Normal input texture.
		 * @param target Output AO/composited texture.
		 */
		virtual void ApplyAmbientOcclusion(Camera* camera, ITexture2D* depthBuffer, ITexture2D* normals, ITexture2D* target) = 0;
	};
}
