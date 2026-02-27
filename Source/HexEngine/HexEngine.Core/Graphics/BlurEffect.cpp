
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
			_shaders[Direction::Horizontal] = IShader::Create("EngineData.Shaders/GaussianBlur.hcs");
			_shaders[Direction::Vertical] = IShader::Create("EngineData.Shaders/GaussianBlurVert.hcs");
		}
		else
		{
			assert("Radial blur not yet implemented");
		}

		_blurCompositionRT[Direction::Horizontal] = g_pEnv->_graphicsDevice->CreateTexture(textureToBlur);
		_blurCompositionRT[Direction::Vertical] = g_pEnv->_graphicsDevice->CreateTexture(textureToBlur);
	}

	BlurEffect::~BlurEffect()
	{
		SAFE_DELETE(_blurCompositionRT[Direction::Horizontal]);
		SAFE_DELETE(_blurCompositionRT[Direction::Vertical]);
	}

	void BlurEffect::Render(GuiRenderer* renderer, bool alpha)
	{
		//for (int32_t x = 0; x < _blurSize; ++x)
		{
			// ----------------------------------------------- //
			// ---------------- HORIZONTAL BLUR -------------- //
			// ----------------------------------------------- //
			// 
			// set the render target to the blur composition
			g_pEnv->_graphicsDevice->SetRenderTarget(_blurCompositionRT[Direction::Horizontal], nullptr);

			// clear it out
			_blurCompositionRT[Direction::Horizontal]->ClearRenderTargetView(math::Color(0, 0, 0, 0));

			// copy the current pixels that we want to blur into the composition
			//_blurTarget->CopyTo(_blurCompositionRT);

			// then start the blur

			{
				//g_pEnv->_sceneRenderer->GetGBuffer()->BindAsShaderResource();

				// render the horizontal blur
				renderer->FullScreenTexturedQuad(_blurTarget, _shaders[Direction::Horizontal].get());

				//if (alpha)
				//{
				//	_blurCompositionRT[Direction::Horizontal]->BlendTo_NonPremultiplied(_blurTarget);
				//	g_pEnv->_graphicsDevice->SetRenderTarget(_blurCompositionRT[Direction::Horizontal], nullptr);
				//}
				//else
				//	_blurCompositionRT[Direction::Horizontal]->CopyTo(_blurTarget);



				////g_pEnv->_sceneRenderer->GetGBuffer()->BindAsShaderResource();

				//renderer->FullScreenTexturedQuad(_blurTarget, _shaders[1].get());

				/*if (alpha)
				{
					_blurCompositionRT[Direction::Horizontal]->BlendTo_NonPremultiplied(_blurTarget);
					g_pEnv->_graphicsDevice->SetRenderTarget(_blurCompositionRT[Direction::Horizontal], nullptr);
				}
				else
					_blurCompositionRT[Direction::Horizontal]->CopyTo(_blurTarget);*/
			}

			// ----------------------------------------------- //
			// ---------------- VERTICAL BLUR ---------------- //
			// ----------------------------------------------- //
			g_pEnv->_graphicsDevice->SetRenderTarget(_blurTarget, nullptr);

			// clear it out
			_blurCompositionRT[Direction::Vertical]->ClearRenderTargetView(math::Color(0, 0, 0, 0));

			// copy the current pixels that we want to blur into the composition
			//_blurTarget->CopyTo(_blurCompositionRT);

			// then start the blur
			//for (int32_t x = 0; x < _blurSize; ++x)
			{
				//g_pEnv->_sceneRenderer->GetGBuffer()->BindAsShaderResource();

				// render the vertical blur
				renderer->FullScreenTexturedQuad(_blurCompositionRT[Direction::Horizontal], _shaders[Direction::Vertical].get());

				/*if (alpha)
				{
					_blurCompositionRT[Direction::Horizontal]->BlendTo_NonPremultiplied(_blurTarget);
					g_pEnv->_graphicsDevice->SetRenderTarget(_blurCompositionRT[Direction::Horizontal], nullptr);
				}
				else
					_blurCompositionRT[Direction::Horizontal]->CopyTo(_blurTarget);*/



					//g_pEnv->_sceneRenderer->GetGBuffer()->BindAsShaderResource();

					//renderer->FullScreenTexturedQuad(_blurTarget, _shaders[1].get());

					/*if (alpha)
					{
						_blurCompositionRT[Direction::Horizontal]->BlendTo_NonPremultiplied(_blurTarget);
						g_pEnv->_graphicsDevice->SetRenderTarget(_blurCompositionRT[Direction::Horizontal], nullptr);
					}
					else
						_blurCompositionRT[Direction::Horizontal]->CopyTo(_blurTarget);*/
			}
		}
	}
}