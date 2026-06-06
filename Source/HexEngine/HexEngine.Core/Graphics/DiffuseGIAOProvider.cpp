
#include "DiffuseGIAOProvider.hpp"
#include "../HexEngine.hpp"
#include "../Scene/DiffuseGI.hpp"
#include "../Scene/SceneRenderer.hpp"
#include "IShader.hpp"
#include "IConstantBuffer.hpp"
#include "IGraphicsDevice.hpp"

namespace HexEngine
{
	HEX_API HVar r_useGIAO(
		"r_useGIAO",
		"When a plugin SSAO is loaded, also run DiffuseGIAOProvider as a second pre-lighting AO pass using the bilateral-blurred voxel-trace occlusion. Catches medium-scale ambient occlusion (overhangs, room interiors) the screen-space SSAO can't reach. No-op when no plugin SSAO is loaded (DiffuseGIAOProvider is already the primary AO in that configuration).",
		false, false, true);
	HEX_API HVar r_giAOIntensity(
		"r_giAOIntensity",
		"Strength of GI-derived AO modulation [0..1] (1 = full beauty * AO in fully-occluded pixels)",
		0.85f, 0.0f, 1.0f);
	HEX_API HVar r_giAOContrast(
		"r_giAOContrast",
		"Power applied to blurred occlusion before modulation. Higher = more contrasty crevices; with the bilateral blur in place the raw voxel grid is gone so the original 1.4 default is usable again.",
		1.4f, 0.25f, 4.0f);

	DiffuseGIAOProvider::DiffuseGIAOProvider(DiffuseGI* gi) :
		_gi(gi)
	{
	}

	DiffuseGIAOProvider::~DiffuseGIAOProvider()
	{
		Destroy();
	}

	bool DiffuseGIAOProvider::Create()
	{
		// Defer expensive object setup until first ApplyAmbientOcclusion so the
		// renderer can construct the provider before shaders / cbuffers are
		// safe to create. Both happen lazily below.
		return true;
	}

	void DiffuseGIAOProvider::Destroy()
	{
		_applyShader.reset();
		SAFE_DELETE(_constants);
	}

	void DiffuseGIAOProvider::ApplyAmbientOcclusion(
		Camera* /*camera*/,
		ITexture2D* /*depthBuffer*/,
		ITexture2D* /*normals*/,
		ITexture2D* target)
	{
		if (target == nullptr)
			return;

		auto* graphics = g_pEnv->_graphicsDevice;
		if (graphics == nullptr)
			return;

		// Lazily resolve the DiffuseGI pointer if it wasn't supplied at ctor
		// time. The provider is created in Game3DEnvironment before
		// SceneRenderer exists, so we resolve via g_pEnv->_sceneRenderer at
		// first use. Subsequent calls hit the cached pointer.
		if (_gi == nullptr)
		{
			if (g_pEnv->_sceneRenderer != nullptr)
				_gi = g_pEnv->_sceneRenderer->GetDiffuseGI();
			if (_gi == nullptr)
				return;
		}

		// Lazy-load the apply shader + a tiny constants cbuffer. Failure to
		// load just no-ops the pass - we never want to crash a frame because
		// a single shader is missing.
		if (_applyShader == nullptr)
		{
			_applyShader = IShader::Create("EngineData.Shaders/DiffuseGIAmbientOcclusion.hcs");
			if (_applyShader == nullptr)
				return;
		}
		if (_constants == nullptr)
		{
			_constants = graphics->CreateConstantBuffer(sizeof(math::Vector4));
			if (_constants == nullptr)
				return;
		}

		// The GI must have run already this frame and produced a valid
		// bilateral-blurred AO target. If GI was disabled / not yet warm we
		// just skip - leaves beauty unmodified. The raw _giResolved.a is no
		// longer sampled directly because its per-voxel grid pattern is
		// visible at screen resolution; DiffuseGI::RenderAoBlurPass wide-
		// blurs it into _giAoBlurred each frame for us to read here.
		ITexture2D* giAo = _gi->GetBlurredAOTexture();
		if (giAo == nullptr)
			return;

		// Pack the artist-facing dials into the cbuffer (params.x = intensity,
		// params.y = contrast, .z/.w reserved).
		math::Vector4 params(
			r_giAOIntensity._val.f32,
			r_giAOContrast._val.f32,
			0.0f, 0.0f);
		_constants->Write(&params, sizeof(params));
		graphics->SetConstantBufferPS(4, _constants);

		// Bind beauty as the render target with multiplicative blend so the
		// shader's output AO multiplier modulates the existing beauty colour.
		// Save existing state so the next pass doesn't inherit our quirks.
		const BlendState        prevBlend = graphics->GetBlendState();
		const DepthBufferState  prevDepth = graphics->GetDepthBufferState();
		const CullingMode       prevCull  = graphics->GetCullingMode();

		graphics->SetRenderTarget(target);
		graphics->SetBlendState(BlendState::Multiplicative);
		graphics->SetDepthBufferState(DepthBufferState::DepthNone);
		graphics->SetCullingMode(CullingMode::NoCulling);

		// Bind GBuffer at t0..t4 (the shader uses GBUFFER_DIFFUSE + GBUFFER_NORMAL
		// to identify sky pixels and skip them) and the blurred AO single-
		// channel RT at t5. Without the sky-pixel skip the GI alpha used to
		// occasionally drift above 0 on sky pixels and the multiplicative
		// blend silently faded the sky toward black; the blur shader now
		// also writes 1.0 on sky pixels for belt-and-braces.
		if (g_pEnv->_sceneRenderer != nullptr)
		{
			if (const auto* gbuffer = g_pEnv->_sceneRenderer->GetGBuffer(); gbuffer != nullptr)
			{
				graphics->SetTexture2D(0, gbuffer->GetDiffuse());
				graphics->SetTexture2D(1, gbuffer->GetSpecular());
				graphics->SetTexture2D(2, gbuffer->GetNormal());
				graphics->SetTexture2D(3, gbuffer->GetPosition());
				graphics->SetTexture2D(4, gbuffer->GetVelocity());
			}
		}
		graphics->SetTexture2D(5, giAo);

		// The GuiRenderer's fullscreen path is the right tool here - same
		// surface that Tonemap / ColourGrade / etc use. It draws a unit
		// fullscreen quad against the bound RT through the supplied shader.
		if (auto* guiRenderer = g_pEnv->GetUIManager().GetRenderer(); guiRenderer != nullptr)
		{
			guiRenderer->StartFrame();
			guiRenderer->FullScreenTexturedQuad(nullptr, _applyShader.get());
			guiRenderer->EndFrame();
		}

		// Cleanup. Unbind our t0..t5 so the next pass doesn't see stale GBuffer
		// + GI alpha, reset the implicit-slot counter to 0 so downstream
		// slot-less SRV binds start at the expected base (mirrors the same fix
		// we did for the decal pass).
		for (uint32_t slot = 0; slot <= 5; ++slot)
			graphics->SetTexture2D(slot, nullptr);
		graphics->SetConstantBufferPS(4, nullptr);
		graphics->SetBoundResourceIndex(0);

		graphics->SetBlendState(prevBlend);
		graphics->SetDepthBufferState(prevDepth);
		graphics->SetCullingMode(prevCull);
	}
}
