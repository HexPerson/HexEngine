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
		// Master adaptation scale (kept for the Settings slider); the actual
		// rate is the split up/down pair below multiplied by this / 1.5.
		HVar r_autoExposureSpeed(
			"r_autoExposureSpeed",
			"Master scale on exposure adaptation speed (1.5 = 1x)",
			1.5f, 0.05f, 8.0f);
		// Split adaptation (eye-like): darkening (scene got brighter) is fast,
		// brightening (scene got darker) is slow.
		HVar r_autoExposureSpeedUp(
			"r_autoExposureSpeedUp",
			"Adaptation rate when exposure is RISING (dark adaptation, 1/s)",
			1.0f, 0.05f, 8.0f);
		HVar r_autoExposureSpeedDown(
			"r_autoExposureSpeedDown",
			"Adaptation rate when exposure is FALLING (bright adaptation, 1/s)",
			3.0f, 0.05f, 8.0f);
		// Percentile band the histogram meter averages over. Discarding the
		// bottom/top tails is the point of the histogram: a blazing sliver
		// (sun disc, neon sign) or a black letterbox can no longer drag the
		// metered value the way it did with the plain mean.
		HVar r_autoExposureLowPercent(
			"r_autoExposureLowPercent",
			"Histogram CDF percentile below which pixels are ignored by the meter",
			40.0f, 0.0f, 90.0f);
		HVar r_autoExposureHighPercent(
			"r_autoExposureHighPercent",
			"Histogram CDF percentile above which pixels are ignored by the meter",
			95.0f, 10.0f, 100.0f);
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

		// Physical light units (Phase 2 final slice, Frostbite convention).
		// Mode 0 = the legacy target-luma multiplier steering above. Mode 1 =
		// EV100: the metered geometric-mean luminance is interpreted as
		// cd/m^2, converted to EV100, and exposure comes from the standard
		// saturation-based mapping exposure = 1 / (1.2 * 2^(EV100 - EC)).
		// DEFAULT EV100 since part 4 landed (r_legacyLightScale calibration
		// + the EV comp curve below; user-verified day+night 2026-07-31),
		// together with r_physicalLightUnits. Legacy remains the fallback
		// for A/B and for scenes whose authoring fights the physical meter.
		HVar r_exposureMode(
			"r_exposureMode",
			"Exposure steering: 0 = legacy target-luma multiplier, 1 = physical EV100",
			(int32_t)1, (int32_t)0, (int32_t)1);
		HVar r_exposureCompensation(
			"r_exposureCompensation",
			"Exposure compensation in EV stops (positive = brighter), EV100 mode only",
			0.0f, -8.0f, 8.0f);
		// EV -> auto-compensation curve (Frostbite's artist EC curve, reduced
		// to a linear ramp). Root cause it exists for: the meter is a
		// LOG-average, and a night street is bimodal - the black sky drags
		// the log-mean far below the lit street, so exposing the mean to
		// middle grey blows the lit half to white (user-verified 2026-07-31,
		// ev100 10.88 night blowout while day 15.1 looked right). Below the
		// break EV each metered stop is partially compensated back down, so
		// dim scenes render dim - which is also what makes night READ as
		// night. Slope 0 restores the plain saturation mapping; slope 1
		// freezes brightness below the break; slopes ABOVE 1 make dim scenes
		// darker than they meter - needed here because legacy night content
		// is authored ~2 stops brighter than physical night (meters EV ~11
		// vs a real night street's ~5), so a 1:1 hold can never reach a
		// night look. No feedback risk at any slope: the meter reads the
		// pre-exposure HDR buffer, so metered EV is exposure-independent.
		// Keep the break BELOW the day EV (~15) or day darkens too - the
		// user found break 20 pulling day down by the full cap.
		HVar r_autoExposureEvCompBreak(
			"r_autoExposureEvCompBreak",
			"EV100 below which the auto EC curve starts darkening (day street ~15, night street ~11)",
			13.0f, -10.0f, 30.0f);
		HVar r_autoExposureEvCompSlope(
			"r_autoExposureEvCompSlope",
			"EV of darkening applied per metered EV below the break (0 = off, 1 = hold, >1 = dimmer than metered)",
			2.0f, 0.0f, 3.0f);
		HVar r_autoExposureEvCompMax(
			"r_autoExposureEvCompMax",
			"Cap on the auto EC curve's total darkening, in EV",
			6.0f, 0.0f, 8.0f);
		// EV clamps replace the multiplier clamps in EV100 mode. Sunny-16
		// daylight sits near EV100 15; interiors ~5-8; moonlight ~ -2.
		HVar r_autoExposureMinEV100(
			"r_autoExposureMinEV100",
			"Lowest EV100 the meter may adapt to (darkest scene it will brighten for)",
			-4.0f, -10.0f, 20.0f);
		HVar r_autoExposureMaxEV100(
			"r_autoExposureMaxEV100",
			"Highest EV100 the meter may adapt to (brightest scene it will darken for)",
			17.0f, -10.0f, 30.0f);

		// Log-luma window we accept in the histogram. Pixels with luminance below exp(min) or
		// above exp(min+range) get clamped to the endpoints, so very dark/bright outliers
		// don't dominate the mean. -10..+10 in natural-log space covers 4e-5 .. 22000 nits.
		constexpr float kMinLogLuma = -10.0f;
		constexpr float kLogLumaRange = 20.0f;
		constexpr uint32_t kHistogramBins = 256;
	}

	// Units slice part 4 (defined in SceneRenderer.cpp): rendered units =
	// physical cd/m^2 / r_legacyLightScale, so the meter multiplies the
	// metered luma back up before interpreting it as absolute EV100. Outside
	// the anonymous namespace - an extern inside it would take internal
	// linkage and never find SceneRenderer's definition.
	extern HVar r_legacyLightScale;

	namespace
	{

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
		_luminanceShader = IShader::Create("EngineData.Shaders/AutoExposureHistogram.hcs");
		if (!_luminanceShader)
		{
			LOG_WARN("AutoExposure: AutoExposureHistogram.hcs failed to load - auto exposure disabled");
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
			desc.ByteWidth = kHistogramBins * sizeof(uint32_t);
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			desc.StructureByteStride = sizeof(uint32_t);
			if (FAILED(device->CreateBuffer(&desc, nullptr, &_accumBuffer)))
			{
				LOG_WARN("AutoExposure: failed to create histogram buffer");
				return false;
			}

			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = DXGI_FORMAT_UNKNOWN;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			uavDesc.Buffer.FirstElement = 0;
			uavDesc.Buffer.NumElements = kHistogramBins;
			if (FAILED(device->CreateUnorderedAccessView(_accumBuffer, &uavDesc, &_accumUav)))
			{
				LOG_WARN("AutoExposure: failed to create histogram UAV");
				return false;
			}
		}

		if (_accumStaging == nullptr)
		{
			D3D11_BUFFER_DESC desc = {};
			desc.ByteWidth = kHistogramBins * sizeof(uint32_t);
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
				uint32_t bins[kHistogramBins];
				std::memcpy(bins, mapped.pData, sizeof(bins));
				context->Unmap(_accumStaging, 0);

				// CDF percentile walk: average log-luma over the [low%, high%]
				// band only. When a bin straddles a percentile boundary, only
				// the in-band fraction of its pixels counts - keeps the metered
				// value continuous as content shifts between bins.
				uint64_t total = 0;
				for (uint32_t b = 0; b < kHistogramBins; ++b)
					total += bins[b];

				float meanLuma = 0.18f; // neutral fallback for an empty histogram
				if (total > 0)
				{
					const float lowPc = std::min(r_autoExposureLowPercent._val.f32,
						r_autoExposureHighPercent._val.f32 - 1.0f);
					const double lowCount = (double)total * lowPc * 0.01;
					const double highCount = (double)total *
						std::clamp(r_autoExposureHighPercent._val.f32, lowPc + 1.0f, 100.0f) * 0.01;

					double cdf = 0.0;
					double bandWeight = 0.0;
					double bandLogSum = 0.0;
					for (uint32_t b = 0; b < kHistogramBins; ++b)
					{
						const double binStart = cdf;
						cdf += bins[b];
						const double inBand = std::min(cdf, highCount) - std::max(binStart, lowCount);
						if (inBand > 0.0)
						{
							// Bin b holds pixels whose normalised log-luma rounded
							// to b/255 - use the bin centre.
							const double logLuma = kMinLogLuma +
								((double)b / 255.0) * kLogLumaRange;
							bandLogSum += logLuma * inBand;
							bandWeight += inBand;
						}
					}
					if (bandWeight > 0.0)
						meanLuma = (float)std::exp(bandLogSum / bandWeight);
				}

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

				float target;
				float minMul;
				float maxMul;
				float debugEv100 = 0.0f;    // true (calibrated) EV100, EV mode only
				float debugAutoComp = 0.0f; // auto EC curve contribution, EV mode only
				if (r_exposureMode._val.i32 == 1)
				{
					// EV100 (Frostbite): the metered luma is in RENDERED units,
					// which sit a factor of r_legacyLightScale below absolute
					// cd/m^2 (the static pre-exposure - see the cvar comment in
					// SceneRenderer.cpp). Multiply back up so EV100 is true:
					// the calibrated day street meters ~4500 cd/m^2 = EV100
					// ~15.1 (sunny 16), night ~177 = EV100 ~10.5. The scale
					// then rides the multiplier too (exposure is physical, the
					// buffer it multiplies is rendered), so it cancels in the
					// final image - its real effect is that the EV clamps and
					// r_exposureCompensation now operate in honest stops.
					const float unitScale = std::max(r_legacyLightScale._val.f32, 1.0f);
					const float lumaCd = std::max(meanLuma * unitScale, 1e-6f);
					const float ev100 = std::log2(lumaCd * (100.0f / 12.5f));
					debugEv100 = ev100;
					const float evMin = r_autoExposureMinEV100._val.f32;
					const float evMax = std::max(r_autoExposureMaxEV100._val.f32, evMin + 1e-3f);
					const float evClamped = std::clamp(ev100, evMin, evMax);
					// Auto EC curve (see the cvar comments): negative below the
					// break, on top of the user's manual compensation.
					const float evDeficit = std::max(r_autoExposureEvCompBreak._val.f32 - evClamped, 0.0f);
					const float autoComp = -std::min(evDeficit * r_autoExposureEvCompSlope._val.f32,
						r_autoExposureEvCompMax._val.f32);
					const float evFinal = evClamped - (r_exposureCompensation._val.f32 + autoComp);
					debugAutoComp = autoComp;
					target = unitScale / (1.2f * std::exp2(evFinal));
					// The smoothing clamp must span the whole reachable range
					// in this mode - derive it from the EV clamps.
					maxMul = unitScale / (1.2f * std::exp2(evMin - r_exposureCompensation._val.f32));
					minMul = unitScale / (1.2f * std::exp2(evMax - r_exposureCompensation._val.f32));
				}
				else
				{
					const float targetLumaDay   = r_autoExposureTargetLuma._val.f32;
					const float targetLumaNight = r_autoExposureNightTargetLuma._val.f32;
					const float targetLuma = targetLumaDay + (targetLumaNight - targetLumaDay) * nightWeight;
					minMul = r_autoExposureMin._val.f32;
					const float maxMulDay   = std::max(r_autoExposureMax._val.f32, minMul + 1e-3f);
					const float maxMulNight = std::max(r_autoExposureNightMax._val.f32, minMul + 1e-3f);
					maxMul = maxMulDay + (maxMulNight - maxMulDay) * nightWeight;
					target = targetLuma / std::max(meanLuma, 1e-6f);
				}
				target = std::clamp(target, minMul, maxMul);

				// Exponential approach: alpha = 1 - exp(-rate * dt), frame-rate
				// independent. Split rates (eye-like): rising exposure = dark
				// adaptation = slow; falling = bright adaptation = fast. The
				// legacy r_autoExposureSpeed HVar (still on the Settings slider)
				// scales both, 1.5 = neutral.
				const float masterScale = std::max(r_autoExposureSpeed._val.f32, 0.0f) / 1.5f;
				const float rate = masterScale * ((target > _smoothedExposure)
					? r_autoExposureSpeedUp._val.f32
					: r_autoExposureSpeedDown._val.f32);
				const float alpha = (rate > 0.0f && deltaTimeSeconds > 0.0f)
					? (1.0f - std::exp(-rate * deltaTimeSeconds))
					: 1.0f;
				_smoothedExposure += (target - _smoothedExposure) * alpha;
				_smoothedExposure = std::clamp(_smoothedExposure, minMul, maxMul);

				if (r_autoExposureDebug._val.b)
				{
					_debugAccum += deltaTimeSeconds;
					if (_debugAccum >= 1.0f)
					{
						_debugAccum = 0.0f;
						LOG_INFO("AutoExposure: total=%llu bandLuma=%.4f ev100=%.2f autoEC=%.2f target=%.3f smoothed=%.3f nightW=%.2f",
							(unsigned long long)total, meanLuma, debugEv100, debugAutoComp, target, _smoothedExposure, nightWeight);
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
