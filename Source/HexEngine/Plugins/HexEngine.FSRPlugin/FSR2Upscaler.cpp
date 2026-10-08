#include "FSR2Upscaler.hpp"

#include <d3d11_3.h>
#include <cmath>

#include "ffx_fsr2_dx11.h"

using namespace HexEngine;

namespace
{
	void FsrLog(const char* message)
	{
		LOG_WARN("%s", message);
	}

	void FsrMessage(FfxFsr2MsgType type, const wchar_t* message)
	{
		if (type == FFX_FSR2_MESSAGE_TYPE_ERROR)
		{
			LOG_WARN("FSR2: %S", message);
		}
		else
		{
			LOG_INFO("FSR2: %S", message);
		}
	}

	ID3D11Resource* NativeOf(ITexture2D* texture)
	{
		return texture != nullptr ? reinterpret_cast<ID3D11Resource*>(texture->GetNativePtr()) : nullptr;
	}
}

FSR2Upscaler::~FSR2Upscaler()
{
	DestroyContext();
}

bool FSR2Upscaler::Create()
{
	// GPU state is built lazily on the first Evaluate, once the renderer knows its sizes.
	ffxFsr2SetLogCallbackDX11(&FsrLog);
	return true;
}

void FSR2Upscaler::Destroy()
{
	DestroyContext();
}

bool FSR2Upscaler::IsSupported()
{
	if (_supported >= 0)
		return _supported == 1;

	_supported = 0;
	auto* graphics = g_pEnv != nullptr ? g_pEnv->_graphicsDevice : nullptr;
	if (graphics == nullptr || graphics->GetBackend() != GraphicsBackend::D3D11)
	{
		LOG_INFO("FSR2: not available - needs the D3D11 backend");
		return false;
	}

	auto* device = reinterpret_cast<ID3D11Device*>(graphics->GetNativeDevice());
	if (device == nullptr || device->GetFeatureLevel() < D3D_FEATURE_LEVEL_11_0)
	{
		LOG_INFO("FSR2: not available - needs feature level 11_0");
		return false;
	}

	// The pass shaders read R16G16_FLOAT / RGBA8 / R11G11B10 UAVs directly, which
	// D3D11 only allows with the 11.3 "typed UAV load additional formats" cap.
	D3D11_FEATURE_DATA_D3D11_OPTIONS2 options2 = {};
	if (FAILED(device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS2, &options2, sizeof(options2))) ||
		!options2.TypedUAVLoadAdditionalFormats)
	{
		LOG_WARN("FSR2: not available - the GPU/driver lacks typed UAV loads of additional formats (D3D11.3)");
		return false;
	}

	_supported = 1;
	return true;
}

bool FSR2Upscaler::GetRenderResolution(UpscalerQuality quality, uint32_t displayWidth, uint32_t displayHeight,
	uint32_t& renderWidth, uint32_t& renderHeight)
{
	if (displayWidth == 0 || displayHeight == 0)
		return false;

	if (quality == UpscalerQuality::NativeAA)
	{
		renderWidth = displayWidth;
		renderHeight = displayHeight;
		return true;
	}

	const int32_t mode = std::clamp((int32_t)quality, (int32_t)FFX_FSR2_QUALITY_MODE_QUALITY, (int32_t)FFX_FSR2_QUALITY_MODE_ULTRA_PERFORMANCE);
	return ffxFsr2GetRenderResolutionFromQualityMode(&renderWidth, &renderHeight, displayWidth, displayHeight,
		(FfxFsr2QualityMode)mode) == FFX_OK;
}

math::Vector2 FSR2Upscaler::GetJitterOffset(uint64_t frameIndex, uint32_t renderWidth, uint32_t displayWidth)
{
	// AMD's recommended Halton(2,3) sequence, 8 * (display/render)^2 phases long.
	const int32_t phases = ffxFsr2GetJitterPhaseCount((int32_t)std::max(1u, renderWidth), (int32_t)std::max(1u, displayWidth));
	float x = 0.0f, y = 0.0f;
	ffxFsr2GetJitterOffset(&x, &y, (int32_t)(frameIndex % (uint64_t)std::max(1, phases)), std::max(1, phases));
	return math::Vector2(x, y);
}

void FSR2Upscaler::DestroyContext()
{
	if (_context != nullptr)
	{
		ffxFsr2ContextDestroy(_context);
		delete _context;
		_context = nullptr;
	}
	if (_scratch != nullptr)
	{
		free(_scratch);
		_scratch = nullptr;
	}
	_displayWidth = _displayHeight = _maxRenderWidth = _maxRenderHeight = 0;
}

bool FSR2Upscaler::EnsureContext(uint32_t displayWidth, uint32_t displayHeight, uint32_t renderWidth, uint32_t renderHeight)
{
	if (_context != nullptr && displayWidth == _displayWidth && displayHeight == _displayHeight &&
		renderWidth == _maxRenderWidth && renderHeight == _maxRenderHeight)
	{
		return true;
	}

	const bool sizesChanged = displayWidth != _displayWidth || displayHeight != _displayHeight ||
		renderWidth != _maxRenderWidth || renderHeight != _maxRenderHeight;
	if (_failed && !sizesChanged)
		return false;

	DestroyContext();
	_failed = true;

	auto* device = reinterpret_cast<ID3D11Device*>(g_pEnv->_graphicsDevice->GetNativeDevice());
	if (device == nullptr)
		return false;

	const size_t scratchSize = ffxFsr2GetScratchMemorySizeDX11();
	_scratch = calloc(1, scratchSize);
	_context = new FfxFsr2Context();

	FfxFsr2ContextDescription desc = {};
	// HDR linear input with FSR2's own exposure estimate. Depth is standard and
	// finite, motion vectors render-res and unjittered - the flags the backend's
	// embedded shader permutation was compiled for (FFX_FSR2_DX11_*_FLAGS).
	desc.flags = FFX_FSR2_ENABLE_HIGH_DYNAMIC_RANGE | FFX_FSR2_ENABLE_AUTO_EXPOSURE;
#ifdef _DEBUG
	desc.flags |= FFX_FSR2_ENABLE_DEBUG_CHECKING;
#endif
	desc.maxRenderSize = { renderWidth, renderHeight };
	desc.displaySize = { displayWidth, displayHeight };
	desc.device = device;
	desc.fpMessage = &FsrMessage;

	FfxErrorCode error = ffxFsr2GetInterfaceDX11(&desc.callbacks, device, _scratch, scratchSize);
	if (error == FFX_OK)
		error = ffxFsr2ContextCreate(_context, &desc);

	_displayWidth = displayWidth;
	_displayHeight = displayHeight;
	_maxRenderWidth = renderWidth;
	_maxRenderHeight = renderHeight;

	if (error != FFX_OK)
	{
		LOG_WARN("FSR2: context creation failed (0x%x) for %ux%u -> %ux%u", (uint32_t)error, renderWidth, renderHeight, displayWidth, displayHeight);
		// Keep the sizes so the same failure isn't retried every frame.
		delete _context;
		_context = nullptr;
		free(_scratch);
		_scratch = nullptr;
		return false;
	}

	LOG_INFO("FSR2: context created, %ux%u -> %ux%u", renderWidth, renderHeight, displayWidth, displayHeight);
	_failed = false;
	_resetNext = true;
	return true;
}

bool FSR2Upscaler::Evaluate(const UpscalerFrameInputs& inputs)
{
	if (!IsSupported())
		return false;

	if (inputs.color == nullptr || inputs.depth == nullptr || inputs.motionVectors == nullptr || inputs.output == nullptr ||
		inputs.renderWidth == 0 || inputs.renderHeight == 0 || inputs.displayWidth == 0 || inputs.displayHeight == 0)
	{
		return false;
	}

	if (!EnsureContext(inputs.displayWidth, inputs.displayHeight, inputs.renderWidth, inputs.renderHeight))
		return false;

	auto* context = reinterpret_cast<ID3D11DeviceContext*>(g_pEnv->_graphicsDevice->GetNativeDeviceContext());
	if (context == nullptr)
		return false;

	FfxFsr2DispatchDescription dispatch = {};
	dispatch.commandList = context;
	dispatch.color = ffxGetResourceDX11(NativeOf(inputs.color), L"FSR2_InputColor");
	dispatch.depth = ffxGetResourceDX11(NativeOf(inputs.depth), L"FSR2_InputDepth");
	dispatch.motionVectors = ffxGetResourceDX11(NativeOf(inputs.motionVectors), L"FSR2_InputMotionVectors");
	dispatch.exposure = ffxGetResourceDX11(nullptr, L"FSR2_InputExposure");
	dispatch.reactive = ffxGetResourceDX11(nullptr, L"FSR2_EmptyInputReactiveMap");
	dispatch.transparencyAndComposition = ffxGetResourceDX11(nullptr, L"FSR2_EmptyTransparencyAndCompositionMap");
	dispatch.output = ffxGetResourceDX11(NativeOf(inputs.output), L"FSR2_OutputUpscaledColor");

	dispatch.jitterOffset.x = inputs.jitterPixels.x;
	dispatch.jitterOffset.y = inputs.jitterPixels.y;

	// The engine's velocity is previous-minus-current in [0,1]-mapped NDC with +y
	// UP (CalcVelocity). FSR2 wants a UV-space (y down) current->previous offset
	// and divides this scale by the render size, so (w, -h) yields exactly (1, -1).
	dispatch.motionVectorScale.x = (float)inputs.renderWidth;
	dispatch.motionVectorScale.y = -(float)inputs.renderHeight;

	dispatch.renderSize = { inputs.renderWidth, inputs.renderHeight };
	dispatch.enableSharpening = inputs.sharpen;
	dispatch.sharpness = std::clamp(inputs.sharpness, 0.0f, 1.0f);
	dispatch.frameTimeDelta = std::max(inputs.frameTimeDeltaMs, 1.0f);
	dispatch.preExposure = 1.0f;
	dispatch.reset = inputs.reset || _resetNext;
	dispatch.cameraNear = inputs.cameraNear;
	dispatch.cameraFar = inputs.cameraFar;
	dispatch.cameraFovAngleVertical = inputs.cameraFovYRadians;
	dispatch.viewSpaceToMetersFactor = 1.0f;	// engine units are metres

	const FfxErrorCode error = ffxFsr2ContextDispatch(_context, &dispatch);
	if (error != FFX_OK)
	{
		static uint64_t s_lastWarnFrame = 0;
		const uint64_t frame = g_pEnv->_timeManager ? (uint64_t)g_pEnv->_timeManager->_frameCount : 0;
		if (frame - s_lastWarnFrame > 300)
		{
			s_lastWarnFrame = frame;
			LOG_WARN("FSR2: dispatch failed (0x%x)", (uint32_t)error);
		}
		return false;
	}

	_resetNext = false;
	return true;
}
