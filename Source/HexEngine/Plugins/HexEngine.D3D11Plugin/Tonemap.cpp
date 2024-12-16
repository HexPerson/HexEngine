

#include "Tonemap.hpp"
#include "GraphicsDeviceD3D11.hpp"
#include <HexEngine.Core/HexEngine.hpp>

namespace HexEngine
{
	void Tonemap::Create(int32_t width, int32_t height)
	{
		_renderTarget = g_pGraphics->CreateTexture2D(
			width, height,
			DXGI_FORMAT_R16G16B16A16_UNORM,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0, 1, 0,
			D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			D3D11_SRV_DIMENSION_TEXTURE2D
		);

		_renderShader = (IShader*)g_pEnv->_resourceSystem->LoadResource("EngineData.Shaders/Tonemap.hcs");
	}

	void Tonemap::Destroy()
	{
		SAFE_DELETE(_renderTarget);

		SAFE_UNLOAD(_renderShader);
	}

	void Tonemap::Render(ID3D11ShaderResourceView* bbSRV)
	{
		ID3D11DeviceContext* deviceContext = (ID3D11DeviceContext*)g_pGraphics->GetNativeDeviceContext();
		GuiRenderer* renderer = g_pEnv->_uiManager->GetRenderer();

		renderer->StartFrame();

		deviceContext->OMSetRenderTargets(1, &_renderTarget->_renderTargetView, nullptr);
		deviceContext->ClearRenderTargetView(_renderTarget->_renderTargetView, math::Color(0, 0, 0, 1));

		g_pGraphics->SetPixelShaderResource(bbSRV);

		renderer->FullScreenTexturedQuad(nullptr);
	}

	void Tonemap::Blit(ID3D11RenderTargetView* backBufferRTV)
	{
		ID3D11DeviceContext* deviceContext = (ID3D11DeviceContext*)g_pGraphics->GetNativeDeviceContext();

		ID3D11RenderTargetView* rtv[] = { backBufferRTV };
		deviceContext->OMSetRenderTargets(1, rtv, nullptr);

		GuiRenderer* renderer = g_pEnv->_uiManager->GetRenderer();

		renderer->StartFrame();

		renderer->FullScreenTexturedQuad(_renderTarget, _renderShader);
	}
}