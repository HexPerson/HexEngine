
#include "TAA.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../Environment/TimeManager.hpp"
#include "../Graphics/IGraphicsDevice.hpp"
#include "../GUI/UIManager.hpp"

namespace HexEngine
{
	static const math::Vector2 gJitterOffset[] = {
		math::Vector2(0.500000f, 0.333333f),
		math::Vector2(0.250000f, 0.666667f),
		math::Vector2(0.750000f, 0.111111f),
		math::Vector2(0.125000f, 0.444444f),
		math::Vector2(0.625000f, 0.777778f),
		math::Vector2(0.375000f, 0.222222f),
		math::Vector2(0.875000f, 0.555556f),
		math::Vector2(0.062500f, 0.888889f),
		math::Vector2(0.562500f, 0.037037f),
		math::Vector2(0.312500f, 0.370370f),
		math::Vector2(0.812500f, 0.703704f),
		math::Vector2(0.187500f, 0.148148f),
		math::Vector2(0.687500f, 0.481481f),
		math::Vector2(0.437500f, 0.814815f),
		math::Vector2(0.937500f, 0.259259f),
		math::Vector2(0.031250f, 0.592593f),
	};

	bool TAA::Create(ITexture2D* buffer)
	{
		_history = g_pEnv->_graphicsDevice->CreateTexture(buffer);
		_renderTarget = g_pEnv->_graphicsDevice->CreateTexture(buffer);

		_resolveShader = IShader::Create("EngineData.Shaders/TAAResolve.hcs");

		return true;
	}

	void TAA::Destroy()
	{
		SAFE_DELETE(_history);
		SAFE_DELETE(_renderTarget);
		SAFE_UNLOAD(_resolveShader);
	}

	TAA::~TAA()
	{
		Destroy();
	}

	math::Vector2 TAA::GetJitterOffset(float screenWidth, float screenHeight) const
	{
		math::Vector2 baseJitter = gJitterOffset[g_pEnv->_timeManager->_frameCount % ARRAYSIZE(gJitterOffset)];

		baseJitter.x = ((baseJitter.x - 0.5f) / screenWidth) * 2.0f;
		baseJitter.y = ((baseJitter.y - 0.5f) / screenHeight) * 2.0f;

		return baseJitter;
	}

	void TAA::Resolve(ITexture2D* output, ITexture2D* buffer, ITexture2D* velocity, ITexture2D* normalAndDepth, GuiRenderer* renderer)
	{
		renderer->StartFrame();

		g_pEnv->_graphicsDevice->SetRenderTarget(_renderTarget);
		_renderTarget->ClearRenderTargetView(math::Color(0, 0, 0, 0));

		g_pEnv->_graphicsDevice->SetTexture2D(_history);
		g_pEnv->_graphicsDevice->SetTexture2D(velocity);
		g_pEnv->_graphicsDevice->SetTexture2D(normalAndDepth);
		renderer->FullScreenTexturedQuad(buffer, _resolveShader);

		// copy the resolved buffer to the 
		_renderTarget->CopyTo(_history);
		_renderTarget->CopyTo(output);

		renderer->EndFrame();
	}
}