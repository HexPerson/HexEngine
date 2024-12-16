

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
		ShadowMap(uint32_t width, uint32_t height);

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

#if 1//def _DEBUG
		ITexture2D* _depthMapRT = nullptr;
#endif
	};
}
