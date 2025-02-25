

#pragma once

#include "ITexture2D.hpp"
#include "IShader.hpp"

namespace HexEngine
{
	class IGraphicsDevice;
	class GuiRenderer;

	class HEX_API GBuffer
	{
	public:
		bool Create(IGraphicsDevice* device, int msaaLevel);
		void Destroy();
		void Resize(int32_t width, int32_t height, int32_t msaaLevel);
		void BindAsShaderResource(ITexture2D* albedoOverride=nullptr) const;
		void Clear();
		void SetAsRenderTargets(const math::Viewport& viewport);

		void RenderDebugTargets(int32_t x, int32_t y, int32_t size, GuiRenderer* renderer);

		ITexture2D* GetDiffuse() const;
		ITexture2D* GetSpecular() const;
		ITexture2D* GetNormal() const;
		ITexture2D* GetPosition() const;
		ITexture2D* GetVelocity() const;
		ITexture2D* GetDepthBuffer() const;

	private:
		ITexture2D* _diffuseTex = nullptr;
		ITexture2D* _specularTex = nullptr;
		ITexture2D* _normalTex = nullptr;
		ITexture2D* _positionTex = nullptr;
		ITexture2D* _velocityTex = nullptr;

		ITexture2D* _depthBuffer = nullptr;
	};
}
