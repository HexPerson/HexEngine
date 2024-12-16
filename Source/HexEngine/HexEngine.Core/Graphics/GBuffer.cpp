

#include "GBuffer.hpp"
#include "../HexEngine.hpp"

namespace HexEngine
{
	bool GBuffer::Create(IGraphicsDevice* device, int msaaLevel)
	{
		uint32_t width, height;
		device->GetBackBufferDimensions(width, height);

		// Create the textures at the current screen resolution
		Resize(width, height, msaaLevel);

		return true;
	}

	void GBuffer::Resize(int32_t width, int32_t height, int32_t msaaLevel)
	{
		SAFE_DELETE(_diffuseTex);
		SAFE_DELETE(_normalTex);
		SAFE_DELETE(_specularTex);
		SAFE_DELETE(_positionTex);
		SAFE_DELETE(_depthBuffer);

		_diffuseTex = g_pEnv->_graphicsDevice->CreateTexture2D(
			width,
			height,
			(DXGI_FORMAT)g_pEnv->_graphicsDevice->GetDesiredBackBufferFormat(),
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0,
			msaaLevel,
			0,
			msaaLevel > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			msaaLevel > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D,
			D3D11_DSV_DIMENSION_UNKNOWN,
			D3D11_USAGE_DEFAULT,
			D3D11_RESOURCE_MISC_SHARED);

		_specularTex = g_pEnv->_graphicsDevice->CreateTexture2D(
			width,
			height,
			DXGI_FORMAT_R32G32B32A32_FLOAT,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0,
			msaaLevel,
			0,
			msaaLevel > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			msaaLevel > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D);

		_normalTex = g_pEnv->_graphicsDevice->CreateTexture2D(
			width,
			height,
			DXGI_FORMAT_R32G32B32A32_FLOAT,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0,
			msaaLevel,
			0,
			msaaLevel > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			msaaLevel > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D,
			D3D11_DSV_DIMENSION_UNKNOWN,
			D3D11_USAGE_DEFAULT,
			D3D11_RESOURCE_MISC_SHARED);

		_positionTex = g_pEnv->_graphicsDevice->CreateTexture2D(
			width,
			height,
			DXGI_FORMAT_R32G32B32A32_FLOAT,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0,
			msaaLevel,
			0,
			msaaLevel > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			msaaLevel > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D);

		_velocityTex = g_pEnv->_graphicsDevice->CreateTexture2D(
			width,
			height,
			DXGI_FORMAT_R32G32_FLOAT,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0,
			msaaLevel,
			0,
			msaaLevel > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			msaaLevel > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D);

		_depthBuffer = g_pEnv->_graphicsDevice->CreateTexture2D(
			width,
			height,
			DXGI_FORMAT_R32_TYPELESS,//DXGI_FORMAT_D24_UNORM_S8_UINT,
			1,
			D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE,
			0, msaaLevel, 0,
			D3D11_RTV_DIMENSION_UNKNOWN,
			D3D11_UAV_DIMENSION_UNKNOWN,
			msaaLevel > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D,
			msaaLevel > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D);

#ifdef _DEBUG
		_diffuseTex->SetDebugName("GBuffer_Diffuse");
		_specularTex->SetDebugName("GBuffer_Specular");
		_normalTex->SetDebugName("GBuffer_NormalAndDepth");
		_positionTex->SetDebugName("GBuffer_WorldPosition");
		_velocityTex->SetDebugName("GBuffer_Velocity");
#endif
	}

	void GBuffer::Destroy()
	{
		SAFE_DELETE(_diffuseTex);
		SAFE_DELETE(_normalTex);
		SAFE_DELETE(_specularTex);
		SAFE_DELETE(_positionTex);
		SAFE_DELETE(_velocityTex);
		SAFE_DELETE(_depthBuffer);
		//SAFE_DELETE(_waterMaskTexture);
	}

	void GBuffer::BindAsShaderResource(ITexture2D* albedoOverride) const
	{
		g_pEnv->_graphicsDevice->SetTexture2DArray({
			albedoOverride ? albedoOverride : _diffuseTex,
			_specularTex,
			_normalTex,
			_positionTex,
			_velocityTex
			});
	}

	void GBuffer::Clear()
	{
		auto clearColour = math::Color(0.0f, 0.0f, 0.0f, 0.0f);

		_diffuseTex->ClearRenderTargetView(math::Color(0.2f, 0.2f, 0.2f, 0.0f));
		_normalTex->ClearRenderTargetView(clearColour);
		_specularTex->ClearRenderTargetView(clearColour);
		_positionTex->ClearRenderTargetView(clearColour);
		_velocityTex->ClearRenderTargetView(clearColour);
		_depthBuffer->ClearDepth(D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL);
	}

	void GBuffer::SetAsRenderTargets(const math::Viewport& viewport)
	{
		g_pEnv->_graphicsDevice->SetRenderTargets({
			_diffuseTex,
			_specularTex,
			_normalTex,
			_positionTex,
			_velocityTex
			},
			_depthBuffer);

		// Clear the depth stencil
		

		// Set the viewport
		//auto& vp = g_pEnv->_graphicsDevice->GetBackBufferViewport();

		g_pEnv->_graphicsDevice->SetViewport(*viewport.Get11());
	}

	void GBuffer::RenderDebugTargets(int32_t x, int32_t y, int32_t size, GuiRenderer* renderer)
	{
		renderer->FillTexturedQuad(_diffuseTex, x, y, size, size, math::Color(0xFFFFFFFF)); x += size + 10;
		renderer->FillTexturedQuad(_specularTex, x, y, size, size, math::Color(0xFFFFFFFF)); x += size + 10;
		renderer->FillTexturedQuad(_normalTex, x, y, size, size, math::Color(0xFFFFFFFF)); x += size + 10;
		renderer->FillTexturedQuad(_positionTex, x, y, size, size, math::Color(0xFFFFFFFF)); x += size + 10;
		renderer->FillTexturedQuad(_velocityTex, x, y, size, size, math::Color(0xFFFFFFFF)); x += size + 10;
	}

	ITexture2D* GBuffer::GetDiffuse() const
	{
		return _diffuseTex;
	}

	ITexture2D* GBuffer::GetSpecular() const
	{
		return _specularTex;
	}

	ITexture2D* GBuffer::GetNormal() const
	{
		return _normalTex;
	}

	ITexture2D* GBuffer::GetPosition() const
	{
		return _positionTex;
	}

	ITexture2D* GBuffer::GetVelocity() const
	{
		return _velocityTex;
	}

	ITexture2D* GBuffer::GetDepthBuffer() const
	{
		return _depthBuffer;
	}
}