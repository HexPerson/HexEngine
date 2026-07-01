

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

        // Clear the depth-mirror colour RT to 1.0 (= far plane) so pixels not
        // touched by any geometry read back as "no occluder" rather than
        // "occluder at the near plane". ShadowMapGeometry's PS writes the
        // rasterized NDC.z into this RT (so the volumetric's point-shadow
        // cubemap-array path has a plain R32_FLOAT source to copy from), and
        // the linear-depth inversion in SamplePointShadow treats 0 as "occluder
        // 1 metre from the light" - exactly wrong for the empty-sky directions
        // a typical point light sees. Clearing to 1.0 (in R; the others don't
        // matter, the RT is R32_FLOAT) matches the DSV depth-clear convention
        // and gives the cubemap a clean "far plane" baseline everywhere the
        // geometry pass didn't write.
        _depthMapRT->ClearRenderTargetView(math::Color(1, 1, 1, 1));
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