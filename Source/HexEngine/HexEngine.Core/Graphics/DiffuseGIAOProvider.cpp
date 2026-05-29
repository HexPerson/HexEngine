
#include "DiffuseGIAOProvider.hpp"
#include "../HexEngine.hpp"
#include "../Scene/DiffuseGI.hpp"
#include "../Scene/SceneRenderer.hpp"
#include "IShader.hpp"
#include "IConstantBuffer.hpp"
#include "IGraphicsDevice.hpp"

namespace HexEngine
{
	HVar r_useGIAO(
		"r_useGIAO",
		"Force GI-derived AO via DiffuseGIAOProvider even if a plugin SSAO is loaded",
		false, false, true);
	HVar r_giAOIntensity(
		"r_giAOIntensity",
		"Strength of GI-derived AO modulation [0..1] (1 = full beauty * AO)",
		0.85f, 0.0f, 1.0f);
	HVar r_giAOContrast(
		"r_giAOContrast",
		"Power applied to AO before modulation - higher = more contrasty crevices",
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

		// The GI must have run already this frame and produced a valid resolved
		// texture with AO in its alpha. If GI was disabled / not yet warm we
		// just skip - leaves beauty unmodified.
		ITexture2D* giResolved = _gi->GetResolvedTexture();
		if (giResolved == nullptr)
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

		// Bind the GI resolved texture (AO in .a) at t0 for the shader.
		graphics->SetTexture2D(0, giResolved);

		// The GuiRenderer's fullscreen path is the right tool here - same
		// surface that Tonemap / ColourGrade / etc use. It draws a unit
		// fullscreen quad against the bound RT through the supplied shader.
		if (auto* guiRenderer = g_pEnv->GetUIManager().GetRenderer(); guiRenderer != nullptr)
		{
			guiRenderer->StartFrame();
			guiRenderer->FullScreenTexturedQuad(nullptr, _applyShader.get());
			guiRenderer->EndFrame();
		}

		// Cleanup. Unbind our t0 so the next pass doesn't see stale GI alpha,
		// reset the implicit-slot counter to 0 so downstream slot-less SRV
		// binds start at the expected base (mirrors the same fix we did for
		// the decal pass).
		graphics->SetTexture2D(0, nullptr);
		graphics->SetConstantBufferPS(4, nullptr);
		graphics->SetBoundResourceIndex(0);

		graphics->SetBlendState(prevBlend);
		graphics->SetDepthBufferState(prevDepth);
		graphics->SetCullingMode(prevCull);
	}
}
