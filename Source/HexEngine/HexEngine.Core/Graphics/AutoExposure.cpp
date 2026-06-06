#include "AutoExposure.hpp"

#include "../Environment/IEnvironment.hpp"
#include "../Environment/LogFile.hpp"
#include "../Input/CommandManager.hpp"
#include "IGraphicsDevice.hpp"

#include <d3d11.h>
#include <algorithm>
#include <cmath>

namespace HexEngine
{
	namespace
	{
		// Tunables exposed as HVars so the user can tweak from the settings dialog without
		// rebuilding. These match the names referenced in Settings.cpp.
		HVar r_autoExposure(
			"r_autoExposure",
			"Enable automatic eye-adaptation exposure based on screen luminance",
			true, false, true);
		HVar r_autoExposureTargetLuma(
			"r_autoExposureTargetLuma",
			"Target middle-grey luminance for auto exposure. Lower = brighter scene, higher = dimmer",
			0.18f, 0.02f, 1.0f);
		HVar r_autoExposureMin(
			"r_autoExposureMin",
			"Minimum exposure multiplier the auto exposure can drive to",
			0.25f, 0.01f, 4.0f);
		HVar r_autoExposureMax(
			"r_autoExposureMax",
			"Maximum exposure multiplier the auto exposure can drive to",
			4.0f, 0.1f, 16.0f);
		HVar r_autoExposureSpeed(
			"r_autoExposureSpeed",
			"Rate at which exposure adapts toward target (1/s; lower = slower)",
			1.5f, 0.05f, 8.0f);
		HVar r_autoExposureSampleStride(
			"r_autoExposureSampleStride",
			"Pixel stride between luminance samples (higher = cheaper but coarser)",
			4, 1, 16);
		HVar r_autoExposureDebug(
			"r_autoExposureDebug",
			"Log mean luma / target / smoothed exposure every ~1 second for tuning",
			false, false, true);
		// At night the auto-exposure would otherwise drive a dark scene up toward the
		// daytime middle-grey target, defeating the look of night entirely. These two
		// HVars override the day-time target and max-multiplier when the sun is below the
		// horizon; the system blends from day to night values across the sunset band so
		// the transition reads smoothly.
		HVar r_autoExposureNightTargetLuma(
			"r_autoExposureNightTargetLuma",
			"Auto exposure target luma when the sun is below the horizon (smaller = darker night)",
			0.04f, 0.005f, 0.5f);
		HVar r_autoExposureNightMax(
			"r_autoExposureNightMax",
			"Maximum exposure multiplier the auto exposure can reach at night (caps brightening of dark scenes)",
			1.20f, 0.1f, 8.0f);

		// Log-luma window we accept in the histogram. Pixels with luminance below exp(min) or
		// above exp(min+range) get clamped to the endpoints, so very dark/bright outliers
		// don't dominate the mean. -10..+10 in natural-log space covers 4e-5 .. 22000 nits.
		constexpr float kMinLogLuma = -10.0f;
		constexpr float kLogLumaRange = 20.0f;

		struct AutoExposureConstants
		{
			uint32_t inputWidth;
			uint32_t inputHeight;
			uint32_t strideX;
			uint32_t strideY;
			float minLogLuma;
			float logLumaRange;
			uint32_t sampleCount;
			uint32_t pad;
		};
	}

	AutoExposure::~AutoExposure()
	{
		Destroy();
	}

	bool AutoExposure::Create()
	{
		_luminanceShader = IShader::Create("EngineData.Shaders/AutoExposureLuminance.hcs");
		if (!_luminanceShader)
		{
			LOG_WARN("AutoExposure: AutoExposureLuminance.hcs failed to load - auto exposure disabled");
			return false;
		}
		return true;
	}

	void AutoExposure::Destroy()
	{
		ReleaseResources();
		_luminanceShader = nullptr;
	}

	void AutoExposure::Reset()
	{
		_smoothedExposure = 1.0f;
		_hasPendingReadback = false;
	}

	bool AutoExposure::EnsureResources(uint32_t inputWidth, uint32_t inputHeight)
	{
		auto* device = reinterpret_cast<ID3D11Device*>(g_pEnv->_graphicsDevice->GetNativeDevice());
		if (device == nullptr)
			return false;

		const bool sizeChanged = (inputWidth != _inputWidth) || (inputHeight != _inputHeight);
		_inputWidth = inputWidth;
		_inputHeight = inputHeight;

		if (_accumBuffer == nullptr)
		{
			D3D11_BUFFER_DESC desc = {};
			desc.ByteWidth = sizeof(uint32_t);
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			desc.StructureByteStride = sizeof(uint32_t);
			if (FAILED(device->CreateBuffer(&desc, nullptr, &_accumBuffer)))
			{
				LOG_WARN("AutoExposure: failed to create accumulator buffer");
				return false;
			}

			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = DXGI_FORMAT_UNKNOWN;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			uavDesc.Buffer.FirstElement = 0;
			uavDesc.Buffer.NumElements = 1;
			if (FAILED(device->CreateUnorderedAccessView(_accumBuffer, &uavDesc, &_accumUav)))
			{
				LOG_WARN("AutoExposure: failed to create accumulator UAV");
				return false;
			}
		}

		if (_accumStaging == nullptr)
		{
			D3D11_BUFFER_DESC desc = {};
			desc.ByteWidth = sizeof(uint32_t);
			desc.Usage = D3D11_USAGE_STAGING;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			if (FAILED(device->CreateBuffer(&desc, nullptr, &_accumStaging)))
			{
				LOG_WARN("AutoExposure: failed to create staging buffer");
				return false;
			}
		}

		if (_constantBuffer == nullptr)
		{
			D3D11_BUFFER_DESC desc = {};
			desc.ByteWidth = (sizeof(AutoExposureConstants) + 15u) & ~15u;
			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			if (FAILED(device->CreateBuffer(&desc, nullptr, &_constantBuffer)))
			{
				LOG_WARN("AutoExposure: failed to create constant buffer");
				return false;
			}
		}

		// Beauty SRV gets rebuilt when the input texture identity changes (e.g. on resize).
		if (sizeChanged && _beautySrv != nullptr)
		{
			_beautySrv->Release();
			_beautySrv = nullptr;
			_beautyTrackedTex = nullptr;
		}

		return true;
	}

	void AutoExposure::ReleaseResources()
	{
		if (_beautySrv != nullptr) { _beautySrv->Release(); _beautySrv = nullptr; }
		if (_accumUav != nullptr) { _accumUav->Release(); _accumUav = nullptr; }
		if (_accumBuffer != nullptr) { _accumBuffer->Release(); _accumBuffer = nullptr; }
		if (_accumStaging != nullptr) { _accumStaging->Release(); _accumStaging = nullptr; }
		if (_constantBuffer != nullptr) { _constantBuffer->Release(); _constantBuffer = nullptr; }
		_beautyTrackedTex = nullptr;
		_inputWidth = 0;
		_inputHeight = 0;
		_hasPendingReadback = false;
		_lastDispatchSampleCount = 0;
	}

	void AutoExposure::Update(ITexture2D* beauty, float deltaTimeSeconds, float sunElevation)
	{
		// User can disable at runtime - just clamp to 1.0 and bail. The user-set r_exposure
		// HVar then drives the colour grade exposure directly.
		if (!r_autoExposure._val.b || beauty == nullptr || !_luminanceShader)
		{
			_smoothedExposure = 1.0f;
			_hasPendingReadback = false;
			return;
		}

		// AutoExposure has direct D3D11 dependencies (raw ID3D11Buffer / UAV /
		// staging-readback / CSSetShader calls below). Under non-D3D11
		// backends the reinterpret_cast of GetNativeDevice() to ID3D11Device*
		// lands on the wrong vtable slot and the debug layer flags
		// CORRUPTED_PARAMETER. Bail to a neutral 1.0 exposure until a
		// per-backend port lands.
		if (g_pEnv->_graphicsDevice->GetBackend() != GraphicsBackend::D3D11)
		{
			_smoothedExposure = 1.0f;
			_hasPendingReadback = false;
			return;
		}

		auto* device = reinterpret_cast<ID3D11Device*>(g_pEnv->_graphicsDevice->GetNativeDevice());
		auto* context = reinterpret_cast<ID3D11DeviceContext*>(g_pEnv->_graphicsDevice->GetNativeDeviceContext());
		if (device == nullptr || context == nullptr)
			return;

		const uint32_t width = static_cast<uint32_t>(std::max(1, beauty->GetWidth()));
		const uint32_t height = static_cast<uint32_t>(std::max(1, beauty->GetHeight()));
		if (!EnsureResources(width, height))
			return;

		auto* beautyTex = reinterpret_cast<ID3D11Texture2D*>(beauty->GetNativePtr());
		if (beautyTex == nullptr)
			return;

		// Re-create SRV when the beauty texture pointer changes (resize, scene switch).
		if (beautyTex != _beautyTrackedTex)
		{
			if (_beautySrv != nullptr) { _beautySrv->Release(); _beautySrv = nullptr; }

			D3D11_TEXTURE2D_DESC td = {};
			beautyTex->GetDesc(&td);

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = td.Format;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.MipLevels = 1;
			if (FAILED(device->CreateShaderResourceView(beautyTex, &srvDesc, &_beautySrv)))
			{
				LOG_WARN("AutoExposure: failed to create SRV for beauty texture");
				return;
			}
			_beautyTrackedTex = beautyTex;
		}

		// === Step 1: read back the PREVIOUS frame's result before kicking off this frame's
		// dispatch. Reading from staging requires the GPU to be done with the previous CopyResource,
		// so we Map it before issuing new work; this still serialises slightly but the buffer is
		// 4 bytes so the wait is minimal.
		if (_hasPendingReadback && _lastDispatchSampleCount > 0)
		{
			D3D11_MAPPED_SUBRESOURCE mapped = {};
			if (SUCCEEDED(context->Map(_accumStaging, 0, D3D11_MAP_READ, 0, &mapped)))
			{
				const uint32_t encoded = *reinterpret_cast<const uint32_t*>(mapped.pData);
				context->Unmap(_accumStaging, 0);

				// Decode: encoded = sum(normalised_logLuma * 1024). Divide by 1024 and by
				// the sample count to get average normalised log-luma.
				const float sumNormalised = static_cast<float>(encoded) / 1024.0f;
				const float meanNormalised = sumNormalised / static_cast<float>(_lastDispatchSampleCount);
				const float meanLogLuma = kMinLogLuma + meanNormalised * kLogLumaRange;
				const float meanLuma = std::exp(meanLogLuma);

				// Reinhard-style: targetExposure = targetLuma / meanLuma. Clamp to user-set
				// range so a totally dark frame doesn't blow exposure to infinity (and a totally
				// bright frame doesn't sink it to zero).
				//
				// Time-of-day blend: at night we want the meter to AIM for a much darker image
				// and to be PREVENTED from pushing exposure all the way up to the daytime max
				// (otherwise a moonlit scene reads as overcast dusk). nightWeight goes 0->1 as
				// the sun descends through the sunset band; both the target luma and the max
				// multiplier lerp toward their night counterparts as the sun sets. The minimum
				// multiplier is preserved so the meter can still pull down on a bright moon
				// disc or window light pocket.
				const float nightWeight = std::clamp((0.06f - sunElevation) / 0.20f, 0.0f, 1.0f);
				const float targetLumaDay   = r_autoExposureTargetLuma._val.f32;
				const float targetLumaNight = r_autoExposureNightTargetLuma._val.f32;
				const float targetLuma = targetLumaDay + (targetLumaNight - targetLumaDay) * nightWeight;
				const float minMul = r_autoExposureMin._val.f32;
				const float maxMulDay   = std::max(r_autoExposureMax._val.f32, minMul + 1e-3f);
				const float maxMulNight = std::max(r_autoExposureNightMax._val.f32, minMul + 1e-3f);
				const float maxMul = maxMulDay + (maxMulNight - maxMulDay) * nightWeight;
				float target = targetLuma / std::max(meanLuma, 1e-6f);
				target = std::clamp(target, minMul, maxMul);

				// Exponential approach: alpha = 1 - exp(-speed * dt). This is frame-rate
				// independent and approaches the target asymptotically.
				const float speed = std::max(r_autoExposureSpeed._val.f32, 0.0f);
				const float alpha = (speed > 0.0f && deltaTimeSeconds > 0.0f)
					? (1.0f - std::exp(-speed * deltaTimeSeconds))
					: 1.0f;
				_smoothedExposure += (target - _smoothedExposure) * alpha;
				_smoothedExposure = std::clamp(_smoothedExposure, minMul, maxMul);

				if (r_autoExposureDebug._val.b)
				{
					_debugAccum += deltaTimeSeconds;
					if (_debugAccum >= 1.0f)
					{
						_debugAccum = 0.0f;
						LOG_INFO("AutoExposure: encoded=%u samples=%u meanLuma=%.4f target=%.3f smoothed=%.3f nightW=%.2f",
							encoded, _lastDispatchSampleCount, meanLuma, target, _smoothedExposure, nightWeight);
					}
				}
			}
		}

		// === Step 2: kick off this frame's dispatch. Clear the accumulator first so we don't
		// add to a stale sum.
		const uint32_t zero = 0;
		const UINT clearValues[4] = { 0, 0, 0, 0 };
		context->ClearUnorderedAccessViewUint(_accumUav, clearValues);
		(void)zero;

		const uint32_t stride = static_cast<uint32_t>(
			std::clamp(r_autoExposureSampleStride._val.i32, 1, 16));
		// Total sampled width/height in pixels = ceil(input / stride). Each thread group is 16x16
		// and one thread handles one sample, so groups = ceil(sampledDim / 16).
		const uint32_t sampledWidth = (width + stride - 1u) / stride;
		const uint32_t sampledHeight = (height + stride - 1u) / stride;
		const uint32_t groupsX = (sampledWidth + 15u) / 16u;
		const uint32_t groupsY = (sampledHeight + 15u) / 16u;
		// Some of the threads in the last partial groups may read out-of-bounds pixels - the
		// shader saturates them to 0, which contributes 0 to the sum after the normalisation
		// clamp. The denominator we use for the mean is the count of in-bounds samples.
		const uint32_t sampleCount = sampledWidth * sampledHeight;

		// Update constants.
		D3D11_MAPPED_SUBRESOURCE mappedCb = {};
		if (SUCCEEDED(context->Map(_constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedCb)))
		{
			AutoExposureConstants c = {};
			c.inputWidth = width;
			c.inputHeight = height;
			c.strideX = stride;
			c.strideY = stride;
			c.minLogLuma = kMinLogLuma;
			c.logLumaRange = kLogLumaRange;
			c.sampleCount = sampleCount;
			std::memcpy(mappedCb.pData, &c, sizeof(c));
			context->Unmap(_constantBuffer, 0);
		}

		// Save state we're about to clobber on the compute pipeline so we don't leave dangling
		// bindings for whatever runs next.
		ID3D11ShaderResourceView* prevSrvs[1] = {};
		ID3D11UnorderedAccessView* prevUavs[1] = {};
		ID3D11Buffer* prevCbs[1] = {};
		context->CSGetShaderResources(0, 1, prevSrvs);
		context->CSGetUnorderedAccessViews(0, 1, prevUavs);
		context->CSGetConstantBuffers(5, 1, prevCbs);

		// The beauty texture is likely still bound as an OM render target from the prior pass
		// (bloom writes back to it). Reading it as an SRV while it's an RTV trips
		// DEVICE_CSSETSHADERRESOURCES_HAZARD and forces the binding to NULL, breaking the
		// dispatch. Save the OM state, null it out, dispatch, then restore.
		ID3D11RenderTargetView* prevRtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
		ID3D11DepthStencilView* prevDsv = nullptr;
		context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, prevRtvs, &prevDsv);
		context->OMSetRenderTargets(0, nullptr, nullptr);

		ID3D11ShaderResourceView* srvs[] = { _beautySrv };
		ID3D11UnorderedAccessView* uavs[] = { _accumUav };
		ID3D11Buffer* cbs[] = { _constantBuffer };
		context->CSSetShaderResources(0, 1, srvs);
		context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
		context->CSSetConstantBuffers(5, 1, cbs);

		auto* stage = _luminanceShader->GetShaderStage(ShaderStage::ComputeShader);
		if (stage != nullptr)
		{
			context->CSSetShader(reinterpret_cast<ID3D11ComputeShader*>(stage->GetNativePtr()), nullptr, 0);
			context->Dispatch(groupsX, groupsY, 1);
		}

		// Unbind to release the UAV hazard before the CopyResource.
		ID3D11ShaderResourceView* nullSrvs[1] = { nullptr };
		ID3D11UnorderedAccessView* nullUavs[1] = { nullptr };
		ID3D11Buffer* nullCbs[1] = { nullptr };
		context->CSSetShaderResources(0, 1, nullSrvs);
		context->CSSetUnorderedAccessViews(0, 1, nullUavs, nullptr);
		context->CSSetConstantBuffers(5, 1, nullCbs);
		context->CSSetShader(nullptr, nullptr, 0);

		// Copy GPU accumulator -> staging buffer. We Map() this on the NEXT frame's Update()
		// call to read the value with minimal wait.
		context->CopyResource(_accumStaging, _accumBuffer);
		_hasPendingReadback = true;
		_lastDispatchSampleCount = sampleCount;

		// Restore the previously-bound resources so we don't disrupt later compute work.
		ID3D11ShaderResourceView* restoreSrvs[1] = { prevSrvs[0] };
		ID3D11UnorderedAccessView* restoreUavs[1] = { prevUavs[0] };
		ID3D11Buffer* restoreCbs[1] = { prevCbs[0] };
		context->CSSetShaderResources(0, 1, restoreSrvs);
		context->CSSetUnorderedAccessViews(0, 1, restoreUavs, nullptr);
		context->CSSetConstantBuffers(5, 1, restoreCbs);
		if (prevSrvs[0] != nullptr) prevSrvs[0]->Release();
		if (prevUavs[0] != nullptr) prevUavs[0]->Release();
		if (prevCbs[0] != nullptr) prevCbs[0]->Release();

		// Restore the OM render targets so the next post-process pass sees the same state it
		// would have without auto-exposure running.
		UINT numRtvs = 0;
		for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
		{
			if (prevRtvs[i] != nullptr) numRtvs = i + 1;
		}
		context->OMSetRenderTargets(numRtvs, numRtvs > 0 ? prevRtvs : nullptr, prevDsv);
		for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
		{
			if (prevRtvs[i] != nullptr) prevRtvs[i]->Release();
		}
		if (prevDsv != nullptr) prevDsv->Release();
	}
}
