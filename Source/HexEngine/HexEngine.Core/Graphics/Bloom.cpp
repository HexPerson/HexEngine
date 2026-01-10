

#include "Bloom.hpp"
#include "../Environment/IEnvironment.hpp"
#include "IGraphicsDevice.hpp"
#include "../GUI/GuiRenderer.hpp"
#include "../GUI/UIManager.hpp"
#include "../Entity/Component/Camera.hpp"

namespace HexEngine
{
	void Bloom::Create(int32_t width, int32_t height)
	{
		_renderTarget = g_pEnv->_graphicsDevice->CreateTexture2D(
			width, height,
			DXGI_FORMAT_R8G8B8A8_UNORM,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0, 1, 0,
			nullptr,
			(D3D11_CPU_ACCESS_FLAG)0,
			D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			D3D11_SRV_DIMENSION_TEXTURE2D
		);

		_renderShader = IShader::Create("EngineData.Shaders/Bloom.hcs");

		_viewport = CD3D11_VIEWPORT(0.0f, 0.0f, (float)width, (float)height);

		_blur = new BlurEffect(_renderTarget, BlurType::Gaussian, 8);
	}

	Bloom::~Bloom()
	{
		Destroy();
	}

	void Bloom::Destroy()
	{
		SAFE_DELETE(_renderTarget);

		SAFE_DELETE(_blur);
	}

	void Bloom::Render(Camera* camera, ITexture2D* bloomInput, ITexture2D* bloomOutput)
	{
		GuiRenderer* renderer = g_pEnv->_uiManager->GetRenderer();

		const auto& bbvp = camera->GetViewport();

		renderer->StartFrame((uint32_t)bbvp.width, (uint32_t)bbvp.height);

		GFX_PERF_BEGIN(0xFFFFFFFF, L"BloomStart");

		g_pEnv->_graphicsDevice->SetRenderTarget(_renderTarget);
		_renderTarget->ClearRenderTargetView(math::Color(0, 0, 0, 0));

		

		g_pEnv->_graphicsDevice->SetViewport({ _viewport });

		// generate the luminosity texture
		//
		renderer->FullScreenTexturedQuad(bloomInput, _renderShader.get());

		_blur->Render(renderer);

		
		g_pEnv->_graphicsDevice->SetViewport(*bbvp.Get11());

		_renderTarget->BlendTo_Additive(bloomOutput);

		GFX_PERF_END();

		renderer->EndFrame();
		
	}

	/*void Bloom::Blit()
	{
		ID3D11DeviceContext* deviceContext = (ID3D11DeviceContext*)g_pGraphics->GetNativeDeviceContext();

		ID3D11RenderTargetView* rtv[] = { backBufferRTV };
		deviceContext->OMSetRenderTargets(1, rtv, nullptr);

		GuiRenderer* renderer = g_pEnv->_guiSystem->GetRenderer();

		renderer->StartFrame();

		float blend[4] = { 1.0f };
		float blend2[4] = { 0.0f };
		deviceContext->OMSetBlendState(g_pGraphics->_states->Additive(), blend, 0xffffffff);

		renderer->FullScreenTexturedQuad(_blurTarget);

		deviceContext->OMSetBlendState(g_pGraphics->_states->Opaque(), blend2, 0xffffffff);
	}*/
}