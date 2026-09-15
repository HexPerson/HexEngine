

#include "Bloom.hpp"
#include "../Environment/IEnvironment.hpp"
#include "IGraphicsDevice.hpp"
#include "../GUI/GuiRenderer.hpp"
#include "../GUI/UIManager.hpp"
#include "../Entity/Component/Camera.hpp"
#include "../Environment/LogFile.hpp"
#include "../Input/CommandManager.hpp"

#include <algorithm>

namespace HexEngine
{
	// Phase 4 bloom cvars. Intensity/threshold/clamp keep their existing
	// names + upload path (g_bloom in the per-frame cbuffer); these two are
	// new with the chain.
	HVar r_bloom("r_bloom", "Enable the bloom pass", true, false, true);
	HVar r_bloomRadius("r_bloomRadius", "Bloom spread multiplier on the tent upsample", 1.0f, 0.25f, 3.0f);

	namespace
	{
		// Per-hop constants at b6 (BokehDoF pattern): two float4s.
		struct BloomPassConstants
		{
			math::Vector4 pass;   // x = 1/srcW, y = 1/srcH, z = firstHop, w = radius
			math::Vector4 pass2;  // x = 1/levelCount (composite), yzw reserved
		};
	}

	HVar r_bloomTemporal("r_bloomTemporal", "Bloom chain temporal EMA keep factor (0 = off); stabilises flicker from thin bright features", 0.6f, 0.0f, 0.95f);

	void Bloom::Create(int32_t width, int32_t height)
	{
		// Chain: half res down to a floor of ~16px on the short side, max 6
		// levels. At both 1080p and 4K this lands on 6.
		int32_t w = std::max(1, width / 2);
		int32_t h = std::max(1, height / 2);
		constexpr int32_t kMaxLevels = 6;
		constexpr int32_t kMinDim = 8;

		for (int32_t i = 0; i < kMaxLevels && w >= kMinDim && h >= kMinDim; ++i)
		{
			_chain.push_back(g_pEnv->_graphicsDevice->CreateTexture2D(
				w, h,
				DXGI_FORMAT_R16G16B16A16_FLOAT,
				1,
				D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
				0, 1, 0,
				nullptr,
				(D3D11_CPU_ACCESS_FLAG)0,
				D3D11_RTV_DIMENSION_TEXTURE2D,
				D3D11_UAV_DIMENSION_UNKNOWN,
				D3D11_SRV_DIMENSION_TEXTURE2D));
			w = std::max(1, w / 2);
			h = std::max(1, h / 2);
		}

		if (!_chain.empty())
		{
			_bloomHistory = g_pEnv->_graphicsDevice->CreateTexture2D(
				_chain[0]->GetWidth(), _chain[0]->GetHeight(),
				DXGI_FORMAT_R16G16B16A16_FLOAT,
				1,
				D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
				0, 1, 0,
				nullptr,
				(D3D11_CPU_ACCESS_FLAG)0,
				D3D11_RTV_DIMENSION_TEXTURE2D,
				D3D11_UAV_DIMENSION_UNKNOWN,
				D3D11_SRV_DIMENSION_TEXTURE2D);
			if (_bloomHistory)
				_bloomHistory->SetDebugName("BloomHistory");
		}
		_bloomHistoryValid = false;

		_temporalShader = IShader::Create("EngineData.Shaders/BloomTemporal.hcs");
		_downsampleShader = IShader::Create("EngineData.Shaders/BloomDownsample.hcs");
		_upsampleShader = IShader::Create("EngineData.Shaders/BloomUpsample.hcs");
		_compositeShader = IShader::Create("EngineData.Shaders/BloomComposite.hcs");
		if (!_downsampleShader || !_upsampleShader || !_compositeShader)
		{
			LOG_WARN("Bloom: chain shaders missing (BloomDownsample/BloomUpsample/BloomComposite.hcs) - bloom disabled.");
		}

		if (_paramsBuffer == nullptr)
		{
			_paramsBuffer = g_pEnv->_graphicsDevice->CreateConstantBuffer(sizeof(BloomPassConstants));
		}
	}

	Bloom::~Bloom()
	{
		Destroy();
	}

	void Bloom::Destroy()
	{
		SAFE_DELETE(_bloomHistory);
		_bloomHistoryValid = false;
		_temporalShader = nullptr;
		for (auto* level : _chain)
		{
			SAFE_DELETE(level);
		}
		_chain.clear();
		SAFE_DELETE(_paramsBuffer);
	}

	void Bloom::Render(Camera* camera, ITexture2D* sceneHdr, ITexture2D* compositeScratch)
	{
		if (!r_bloom._val.b ||
			!_downsampleShader || !_upsampleShader || !_compositeShader ||
			_chain.empty() || _paramsBuffer == nullptr ||
			sceneHdr == nullptr || compositeScratch == nullptr || camera == nullptr)
			return;

		auto* graphics = g_pEnv->_graphicsDevice;
		GuiRenderer* renderer = g_pEnv->GetUIManager().GetRenderer();
		if (renderer == nullptr)
			return;

		const auto& bbvp = camera->GetViewport();
		const int32_t levelCount = (int32_t)_chain.size();

		renderer->StartFrame((uint32_t)bbvp.width, (uint32_t)bbvp.height);
		GFX_PERF_BEGIN(0xFFFFFFFF, L"Bloom chain");

		BloomPassConstants constants = {};

		const auto uploadConstants = [&]()
		{
			BloomPassConstants copy = constants; // Write takes void*
			_paramsBuffer->Write(&copy, sizeof(copy));
			graphics->SetConstantBufferPS(6, _paramsBuffer);
		};

		// --- Downsample: scene -> chain[0] (prefilter + Karis) -> ... -> [N-1]
		ITexture2D* src = sceneHdr;
		for (int32_t i = 0; i < levelCount; ++i)
		{
			constants.pass = math::Vector4(
				1.0f / (float)std::max(1, src->GetWidth()),
				1.0f / (float)std::max(1, src->GetHeight()),
				(i == 0) ? 1.0f : 0.0f,
				r_bloomRadius._val.f32);
			uploadConstants();

			graphics->SetRenderTarget(_chain[i]);
			graphics->SetViewport(Viewport(0.0f, 0.0f,
				(float)_chain[i]->GetWidth(), (float)_chain[i]->GetHeight()));
			renderer->FullScreenTexturedQuad(src, _downsampleShader.get());
			src = _chain[i];
		}

		// --- Upsample: additive tent accumulation back up the chain.
		graphics->SetBlendState(BlendState::Additive);
		for (int32_t i = levelCount - 1; i >= 1; --i)
		{
			constants.pass = math::Vector4(
				1.0f / (float)std::max(1, _chain[i]->GetWidth()),
				1.0f / (float)std::max(1, _chain[i]->GetHeight()),
				0.0f,
				r_bloomRadius._val.f32);
			uploadConstants();

			graphics->SetRenderTarget(_chain[i - 1]);
			graphics->SetViewport(Viewport(0.0f, 0.0f,
				(float)_chain[i - 1]->GetWidth(), (float)_chain[i - 1]->GetHeight()));
			renderer->FullScreenTexturedQuad(_chain[i], _upsampleShader.get());
		}
		graphics->SetBlendState(BlendState::Opaque);

		// --- Temporal EMA of the accumulated chain (flicker stabiliser).
		// history = lerp(history, chain, 1-keep) via constant-alpha
		// Transparency blending - no second read needed. TAA leaves residual
		// shimmer on thin ultra-bright features; the bloom threshold and the
		// flare knee turn that shimmer into intermittent pops at grazing
		// angles. Bloom is a low-frequency veil, so the EMA latency is
		// invisible while the flicker averages away.
		ITexture2D* compositeChain = _chain[0];
		const float temporalKeep = std::clamp(r_bloomTemporal._val.f32, 0.0f, 0.95f);
		auto temporalStage = _temporalShader ? _temporalShader->GetShaderStage(ShaderStage::PixelShader) : nullptr;
		if (temporalKeep > 0.001f && _bloomHistory != nullptr && temporalStage != nullptr &&
			_bloomHistory->GetWidth() == _chain[0]->GetWidth() &&
			_bloomHistory->GetHeight() == _chain[0]->GetHeight())
		{
			if (!_bloomHistoryValid)
			{
				_chain[0]->CopyTo(_bloomHistory);
				_bloomHistoryValid = true;
			}
			else
			{
				constants.pass = math::Vector4(
					1.0f / (float)std::max(1, _chain[0]->GetWidth()),
					1.0f / (float)std::max(1, _chain[0]->GetHeight()),
					1.0f - temporalKeep, 0.0f);
				uploadConstants();
				graphics->SetBlendState(BlendState::Transparency);
				graphics->SetRenderTarget(_bloomHistory);
				graphics->SetViewport(Viewport(0.0f, 0.0f,
					(float)_bloomHistory->GetWidth(), (float)_bloomHistory->GetHeight()));
				renderer->FullScreenTexturedQuad(_chain[0], _temporalShader.get());
				graphics->SetBlendState(BlendState::Opaque);
			}
			compositeChain = _bloomHistory;
		}
		else
		{
			_bloomHistoryValid = false;
		}

		// --- Composite: scene + normalised chain top -> scratch -> scene.
		// (Can't sample and write sceneHdr in one draw - hazard check.)
		constants.pass = math::Vector4(
			1.0f / (float)std::max(1, compositeChain->GetWidth()),
			1.0f / (float)std::max(1, compositeChain->GetHeight()),
			0.0f, 0.0f);
		constants.pass2 = math::Vector4(1.0f / (float)levelCount, 0.0f, 0.0f, 0.0f);
		uploadConstants();

		graphics->SetRenderTarget(compositeScratch);
		graphics->SetViewport(*bbvp.Get11());
		graphics->SetTexture2D(0, sceneHdr);
		graphics->SetTexture2D(1, compositeChain);
		renderer->FullScreenTexturedQuad(nullptr, _compositeShader.get());

		compositeScratch->CopyTo(sceneHdr);

		graphics->SetConstantBufferPS(6, nullptr);

		GFX_PERF_END();
		renderer->EndFrame();
	}
}
