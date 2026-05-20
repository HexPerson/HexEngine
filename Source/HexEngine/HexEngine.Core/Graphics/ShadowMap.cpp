

#include "ShadowMap.hpp"
#include "../HexEngine.hpp"

namespace HexEngine
{
	ShadowMap::ShadowMap(uint32_t width, uint32_t height)
	{
		_viewport.x = 0.0f;
		_viewport.y = 0.0f;
		_viewport.width = (float)width;
		_viewport.height = (float)height;
		_viewport.minDepth = 0.0f;
		_viewport.maxDepth = 1.0f;

       
	}

    ShadowMap::~ShadowMap()
    {
        Destroy();
    }

    void ShadowMap::Destroy()
    {
        SAFE_DELETE(_depthMap);
        SAFE_DELETE(_depthMapRT);
    }

	void ShadowMap::Create()
	{
        _depthMap = (ITexture2D*)g_pEnv->_graphicsDevice->CreateTexture2D(
            (int32_t)_viewport.width,
            (int32_t)_viewport.height,
            DXGI_FORMAT_R32_TYPELESS, //DXGI_FORMAT_R24G8_TYPELESS
            1,
            D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_DEPTH_STENCIL,
            0, 1, 0,
            nullptr,
            (D3D11_CPU_ACCESS_FLAG)0,
            D3D11_RTV_DIMENSION_UNKNOWN,
            D3D11_UAV_DIMENSION_UNKNOWN,
            D3D11_SRV_DIMENSION_TEXTURE2D,
            D3D11_DSV_DIMENSION_TEXTURE2D);

        _depthMapRT = (ITexture2D*)g_pEnv->_graphicsDevice->CreateTexture2D(
            (int32_t)_viewport.width,
            (int32_t)_viewport.height,
            DXGI_FORMAT_R32_FLOAT,
            1,
            D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,
            0, 1, 0,
            nullptr,
            (D3D11_CPU_ACCESS_FLAG)0,
            D3D11_RTV_DIMENSION_TEXTURE2D,
            D3D11_UAV_DIMENSION_UNKNOWN,
            D3D11_SRV_DIMENSION_TEXTURE2D,
            D3D11_DSV_DIMENSION_UNKNOWN);

        //_taa.Create(_depthMapRT);
	}

    void ShadowMap::SetRenderTarget()
    {
        g_pEnv->_graphicsDevice->SetViewports({ _viewport });

        _depthMapRT->ClearRenderTargetView(math::Color(0, 0, 0, 1));
        g_pEnv->_graphicsDevice->SetRenderTargets(1, { _depthMapRT }, _depthMap);

        _depthMap->ClearDepth(D3D11_CLEAR_DEPTH);
    }

    void ShadowMap::Resolve()
    {
        //_taa.Resolve(_)
    }

    void ShadowMap::BindAsShaderResource() const
    {
        g_pEnv->_graphicsDevice->SetTexture2D(_depthMap);
    }

    void ShadowMap::RenderDebugTargets(int32_t x, int32_t y, int32_t size, GuiRenderer* renderer)
    {
        renderer->FillTexturedQuad(_depthMapRT, x, y, size, size, math::Color(0xFFFFFFFF));
    }

    const math::Viewport& ShadowMap::GetViewport() const
    {
        return _viewport;
    }
}