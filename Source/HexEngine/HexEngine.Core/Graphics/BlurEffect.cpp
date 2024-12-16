
#include "BlurEffect.hpp"
#include "../HexEngine.hpp"

namespace HexEngine
{
	BlurEffect::BlurEffect(ITexture2D* textureToBlur, BlurType type, int32_t blurSize) :
		_blurTarget(textureToBlur),
		_type(type),
		_blurSize(blurSize)
	{
		if (type == BlurType::Gaussian)
		{
			_shaders[0] = (IShader*)g_pEnv->_resourceSystem->LoadResource("EngineData.Shaders/GaussianBlur.hcs");
			_shaders[1] = (IShader*)g_pEnv->_resourceSystem->LoadResource("EngineData.Shaders/GaussianBlurVert.hcs");
		}
		else
		{
			assert("Radial blur not yet implemented");
		}

		_blurCompositionRT = g_pEnv->_graphicsDevice->CreateTexture(textureToBlur);
	}

	BlurEffect::~BlurEffect()
	{
		for (auto i = 0; i < ARRAYSIZE(_shaders); ++i)
		{
			SAFE_UNLOAD(_shaders[i]);
		}

		SAFE_DELETE(_blurCompositionRT);
	}

	void BlurEffect::Render(GuiRenderer* renderer, bool alpha)
	{
		// set the render target to the blur composition
		g_pEnv->_graphicsDevice->SetRenderTarget(_blurCompositionRT, nullptr);

		// clear it out
		_blurCompositionRT->ClearRenderTargetView(math::Color(0, 0, 0, 0));

		// copy the current pixels that we want to blur into the composition
		//_blurTarget->CopyTo(_blurCompositionRT);

		// then start the blur
		for (int32_t x = 0; x < _blurSize; ++x)
		{
			//g_pEnv->_sceneRenderer->GetGBuffer()->BindAsShaderResource();

			// render the horizontal blur
			renderer->FullScreenTexturedQuad(_blurTarget, _shaders[0]);

			if (alpha)
			{
				_blurCompositionRT->BlendTo_NonPremultiplied(_blurTarget);
				g_pEnv->_graphicsDevice->SetRenderTarget(_blurCompositionRT, nullptr);
			}
			else
				_blurCompositionRT->CopyTo(_blurTarget);



			//g_pEnv->_sceneRenderer->GetGBuffer()->BindAsShaderResource();

			renderer->FullScreenTexturedQuad(_blurTarget, _shaders[1]);

			if (alpha)
			{
				_blurCompositionRT->BlendTo_NonPremultiplied(_blurTarget);
				g_pEnv->_graphicsDevice->SetRenderTarget(_blurCompositionRT, nullptr);
			}
			else
				_blurCompositionRT->CopyTo(_blurTarget);
		}
	}
}