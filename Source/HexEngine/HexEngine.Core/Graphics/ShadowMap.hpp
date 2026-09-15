

#pragma once

#include "../Required.hpp"
#include "ITexture2D.hpp"
#include "../Graphics/TAA.hpp"

namespace HexEngine
{
	class IShader;
	class GuiRenderer;

	class ShadowMap
	{
	public:
		// needsColourTarget allocates a paired R32_FLOAT colour RT alongside the depth map
		// and makes the geometry pass write NDC.z into it. Only POINT lights need this: the
		// volumetric scattering path copies their six faces into a TextureCubeArray and
		// needs a plain (non-typeless, non-DSV) source to copy from. Directional cascades
		// and spot maps are sampled through the depth SRV only, so they leave it off - at
		// the sun's 8192 square cascades the colour copy was ~268 MB each, over a gigabyte
		// of VRAM written every frame and never read.
		ShadowMap(uint32_t width, uint32_t height, bool needsColourTarget = false);

		~ShadowMap();

		void Create();

		void SetRenderTarget();

		ITexture2D* GetDepthMap() const { return _depthMap; }
		ITexture2D* GetRenderTarget() const { return _depthMapRT; }

		void Destroy();

		void BindAsShaderResource() const;

		void RenderDebugTargets(int32_t x, int32_t y, int32_t size, GuiRenderer* renderer);

		void Resolve();

		const math::Viewport& GetViewport() const;

	private:
		math::Viewport _viewport;
		ITexture2D* _depthMap = nullptr;
		//TAA _taa;

		// Colour render target paired with _depthMap - null unless _needsColourTarget.
		// See the constructor comment for who actually needs it.
		ITexture2D* _depthMapRT = nullptr;
		bool _needsColourTarget = false;
	};
}
