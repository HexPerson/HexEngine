
#include "Streamline.hpp"

#include <sl_security.h>

void LogCallback(sl::LogType type, const char* msg)
{
	LOG_DEBUG(msg);
}

bool Streamline::Create()
{
	_dlssOptions.mode = (sl::DLSSMode)-1;

	auto binaryPath = HexEngine::g_pEnv->_fileSystem->GetBinaryDirectory();

	auto interposerPath = (binaryPath / L"sl.interposer.dll").wstring();

	if (!sl::security::verifyEmbeddedSignature(interposerPath.c_str()))
	{
		LOG_CRIT("Streamline validation failed!");
		return false;
	}

	auto mod = LoadLibraryW(interposerPath.c_str());

	// Map functions from SL and use them instead of standard DXGI/D3D12 API
	_importedFuncs._createFactory = reinterpret_cast<PFunCreateDXGIFactory>(GetProcAddress(mod, "CreateDXGIFactory"));
	_importedFuncs._createFactory1 = reinterpret_cast<PFunCreateDXGIFactory1>(GetProcAddress(mod, "CreateDXGIFactory1"));
	_importedFuncs._createFactory2 = reinterpret_cast<PFunCreateDXGIFactory2>(GetProcAddress(mod, "CreateDXGIFactory2"));
	_importedFuncs._getDebugInterface1 = reinterpret_cast<PFunDXGIGetDebugInterface1>(GetProcAddress(mod, "DXGIGetDebugInterface1"));
	_importedFuncs._d3d11CreateDevice = reinterpret_cast<PFunD3D11CreateDevice>(GetProcAddress(mod, "D3D11CreateDevice"));

	wchar_t binaryPathStr[MAX_PATH];
	wcscpy_s(binaryPathStr, binaryPath.string().length() + 1, binaryPath.wstring().c_str());

	const wchar_t* binaryPathList[1] = {
		binaryPathStr
	};

	sl::Preferences pref;
	pref.showConsole = true;                        // for debugging, set to false in production
	pref.logLevel = sl::LogLevel::eVerbose;
	pref.pathsToPlugins = binaryPathList; // change this if Streamline plugins are not located next to the executable
	pref.numPathsToPlugins = 1; // change this if Streamline plugins are not located next to the executable
	pref.pathToLogsAndData = {};                    // change this to enable logging to a file
	pref.logMessageCallback = LogCallback; // highly recommended to track warning/error messages in your callback
	pref.applicationId = 531313132;
	pref.renderAPI = sl::RenderAPI::eD3D11;
	pref.engine = sl::EngineType::eCustom;

	sl::Feature myFeatures[] = {
		//sl::kFeatureNRD,
		sl::kFeatureDLSS,
		sl::kFeatureDLSS_RR
	};
	pref.featuresToLoad = myFeatures;
	pref.numFeaturesToLoad = _countof(myFeatures);

	if (SL_FAILED(result, slInit(pref)))
	{
		LOG_CRIT("Failed to initialize Streamline");
		return false;
	}

	_enabled = true;

	return true;
}

void Streamline::Destroy()
{
	slShutdown();
}

HRESULT Streamline::D3D11CreateDevice(
	IDXGIAdapter* pAdapter,
	D3D_DRIVER_TYPE DriverType,
	HMODULE Software,
	UINT Flags,
	const D3D_FEATURE_LEVEL* pFeatureLevels,
	UINT FeatureLevels,
	UINT SDKVersion,
	ID3D11Device** ppDevice,
	D3D_FEATURE_LEVEL* pFeatureLevel,
	ID3D11DeviceContext** ppImmediateContext)
{
	if (!_enabled)
		return S_FALSE;

	auto result = _importedFuncs._d3d11CreateDevice(
		pAdapter,
		DriverType,
		Software,
		Flags, 
		pFeatureLevels,
		FeatureLevels,
		SDKVersion,
		ppDevice,
		pFeatureLevel,
		ppImmediateContext);

	if (result == S_OK)
	{
		//slSetD3DDevice(*ppDevice);
	}
	else
	{
		LOG_CRIT("Failed to create D3D device via Streamline");
	}

	return result;
}

HRESULT Streamline::CreateDXGIFactory1(REFIID riid, _COM_Outptr_ void** ppFactory)
{
	if (!_enabled)
		return S_FALSE;

	auto res = _importedFuncs._createFactory1(riid, ppFactory);

	if (res != S_OK)
		return res;

	IDXGIFactory1* pFactory = reinterpret_cast<IDXGIFactory1*>(*ppFactory);

	IDXGIAdapter* adapter;
	uint32_t i = 0;
	while (pFactory->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND)
	{
		DXGI_ADAPTER_DESC desc{};
		if (SUCCEEDED(adapter->GetDesc(&desc)))
		{
			sl::AdapterInfo adapterInfo{};
			adapterInfo.deviceLUID = (uint8_t*)&desc.AdapterLuid;
			adapterInfo.deviceLUIDSizeInBytes = sizeof(LUID);

			if (SL_FAILED(result, slIsFeatureSupported(sl::kFeatureDLSS, adapterInfo)))
			{
				LOG_WARN("DLSS Is NOT supported on adapter %s. Code: %d", desc.Description, result);
			}
			else
			{
				LOG_WARN("DLSS IS supported on adapter %s", desc.Description);
				_supportedFeatures |= HexEngine::StreamlineFeature::DLSS;
			}

			if (SL_FAILED(result, slIsFeatureSupported(sl::kFeatureNRD, adapterInfo)))
			{
				LOG_WARN("NRD Is NOT supported on adapter %s. Code: %d", desc.Description, result);
			}
			else
			{
				LOG_WARN("NRD IS supported on adapter %s", desc.Description);
				_supportedFeatures |= HexEngine::StreamlineFeature::NRD;
			}
		}
		i++;
	}

	return res;
}

bool Streamline::QueryOptimalDLSSSettings(
	int32_t desiredWidth,
	int32_t desiredHeight,
	HexEngine::DLSSMode mode,
	int32_t& optimalWidth,
	int32_t& optimalHeight)
{
	sl::DLSSOptimalSettings dlssSettings;

	sl::DLSSOptions opts;
	memcpy(&opts, &_dlssOptions, sizeof(sl::DLSSOptions));

	// These are populated based on user selection in the UI
	opts.mode = (sl::DLSSMode)mode;
	opts.outputWidth = desiredWidth;    // e.g 1920;
	opts.outputHeight = desiredHeight; // e.g. 1080;
	opts.colorBuffersHDR = sl::Boolean::eFalse;



	// Now let's check what should our rendering resolution be
	if (SL_FAILED(result, slDLSSGetOptimalSettings(opts, dlssSettings)))
	{
		LOG_CRIT("Failed to get optimal DLSS settings");
		return false;
	}
	
	optimalWidth = dlssSettings.optimalRenderWidth;
	optimalHeight = dlssSettings.optimalRenderHeight;

	_renderExtent.width = optimalWidth;
	_renderExtent.height = optimalHeight;

	_outputExtent.width = desiredWidth;
	_outputExtent.height = desiredHeight;

	return true;
}

uint32_t Streamline::GetSupportedFeaturesMask()
{
	return _supportedFeatures;
}

void Streamline::SetDLSSOptions(float sharpness, bool hdr, bool autoExposure, HexEngine::DLSSMode mode, int32_t outputWidth, int32_t outputHeight)
{
	if (_dlssOptions.mode == (sl::DLSSMode)mode)
		return;
	
	_dlssOptions.mode = (sl::DLSSMode)mode;
	_dlssOptions.sharpness = sharpness;
	_dlssOptions.colorBuffersHDR = hdr ? sl::Boolean::eTrue : sl::Boolean::eFalse; // assuming HDR pipeline
	_dlssOptions.useAutoExposure = autoExposure ? sl::Boolean::eTrue : sl::Boolean::eFalse; // autoexposure is not to be used if a proper exposure texture is available
	_dlssOptions.outputWidth = outputWidth;
	_dlssOptions.outputHeight = outputHeight;

	_dlssOptions.dlaaPreset = sl::DLSSPreset::ePresetA;
	_dlssOptions.qualityPreset = sl::DLSSPreset::ePresetD;
	_dlssOptions.balancedPreset = sl::DLSSPreset::ePresetD;
	_dlssOptions.performancePreset = sl::DLSSPreset::ePresetD;
	_dlssOptions.ultraPerformancePreset = sl::DLSSPreset::ePresetA;

	if (SL_FAILED(result, slDLSSSetOptions(_viewport, _dlssOptions)))
	{
		LOG_CRIT("Failed to set DLSS options");
	}

	if (mode == HexEngine::DLSSMode::Off)
	{
		slFreeResources(sl::kFeatureDLSS, _viewport);
	}
}

void Streamline::PrepareFrameResources(void* colourIn, void* colourOut, void* motionVectors, void* depthBuffer, void* cmdList)
{
	// Prepare resources (assuming d3d11/d3d12 integration so leaving Vulkan view and device memory as null pointers)
	sl::Resource colorIn = { sl::ResourceType::eTex2d, colourIn, 0, 0, 0};
	sl::Resource colorOut = { sl::ResourceType::eTex2d, colourOut, 0, 0, 0 };
	sl::Resource depth = { sl::ResourceType::eTex2d, depthBuffer, 0, 0, 0 };
	sl::Resource mvec = { sl::ResourceType::eTex2d, motionVectors, 0, 0, 0 };
	//sl::Resource exposure = { sl::ResourceType::eTex2d, myExposureBuffer, nullptr, nullptr, nullptr };

	sl::ResourceTag colorInTag = sl::ResourceTag{ &colorIn, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eOnlyValidNow , &_renderExtent };
	sl::ResourceTag colorOutTag = sl::ResourceTag{ &colorOut, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eOnlyValidNow , &_outputExtent };
	sl::ResourceTag depthTag = sl::ResourceTag{ &depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eOnlyValidNow , &_renderExtent };
	sl::ResourceTag mvecTag = sl::ResourceTag{ &mvec, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eOnlyValidNow , &_renderExtent };
	//sl::ResourceTag exposureTag = sl::ResourceTag{ &exposure, sl::kBufferTypeExposure, sl::ResourceLifecycle::eOnlyValidNow, &my1x1Extent };

	// Tag in group
	sl::ResourceTag inputs[] = { colorInTag, colorOutTag, depthTag, mvecTag };
	slSetTag(_viewport, inputs, _countof(inputs), cmdList);
}

void Streamline::SetCommonConstants(const HexEngine::StreamlineConstants& constants)
{
	sl::Constants consts = {};
	// Set motion vector scaling based on your setup
	//consts.mvecScale = { 1,1 }; // Values in eMotionVectors are in [-1,1] range
	//consts.mvecScale = { 1,1 }; // Values in eMotionVectors are in [-1,1] range
	consts.mvecScale = { 1.0f / _renderExtent.width, 1.0f / _renderExtent.height }; // Values in eMotionVectors are in pixel space
	//consts.mvecScale = myCustomScaling; // Custom scaling to ensure values end up in [-1,1] range

	//newPos.xy = (newPos.xy * 2) + 0.5;

	consts.cameraViewToClip = *(sl::float4x4*)&constants.cameraViewToClip.m[0][0];
	consts.clipToCameraView = *(sl::float4x4*)&constants.clipToCameraView.m[0][0];
	consts.clipToLensClip = *(sl::float4x4*)&constants.clipToLensClip.m[0][0];
	consts.clipToPrevClip = *(sl::float4x4*)&constants.clipToPrevClip.m[0][0];
	consts.prevClipToClip = *(sl::float4x4*)&constants.prevClipToClip.m[0][0];

	consts.jitterOffset = *(sl::float2*)&constants.jitterOffset.x;
	consts.cameraPinholeOffset = *(sl::float2*)&constants.cameraPinholeOffset.x;

	consts.cameraPos = *(sl::float3*)&constants.cameraPos.x;
	consts.cameraUp = *(sl::float3*)&constants.cameraUp.x;
	consts.cameraRight = *(sl::float3*)&constants.cameraRight.x;
	consts.cameraFwd = *(sl::float3*)&constants.cameraFwd.x;

	consts.cameraNear = constants.cameraNear;
	consts.cameraFar = constants.cameraFar;
	consts.cameraFOV = constants.cameraFOV;
	consts.cameraAspectRatio = constants.cameraAspectRatio;
	consts.motionVectorsInvalidValue = constants.motionVectorsInvalidValue;

	consts.depthInverted = constants.depthInverted ? sl::Boolean::eTrue : sl::Boolean::eFalse;
	consts.cameraMotionIncluded = constants.cameraMotionIncluded ? sl::Boolean::eTrue : sl::Boolean::eFalse;
	consts.motionVectors3D = constants.motionVectors3D ? sl::Boolean::eTrue : sl::Boolean::eFalse;
	consts.reset = constants.reset ? sl::Boolean::eTrue : sl::Boolean::eFalse;
	consts.orthographicProjection = constants.orthographicProjection ? sl::Boolean::eTrue : sl::Boolean::eFalse;
	consts.motionVectorsDilated = constants.motionVectorsDilated ? sl::Boolean::eTrue : sl::Boolean::eFalse;
	consts.motionVectorsJittered = constants.motionVectorsJittered ? sl::Boolean::eTrue : sl::Boolean::eFalse;

	consts.minRelativeLinearDepthObjectSeparation = constants.minRelativeLinearDepthObjectSeparation;

	if (SL_FAILED(result, slSetConstants(consts, *_frameToken, _viewport))) // constants are changing per frame so frame index is required
	{
		LOG_CRIT("Failed to set Streamline constants: %d", result);
	}
}

void Streamline::BeginFrame()
{
	slGetNewFrameToken(_frameToken, nullptr);
}

void Streamline::EvaluateFeature(HexEngine::StreamlineFeature feature, void* cmdList)
{
	sl::ViewportHandle view(_viewport);
	const sl::BaseStructure* inputs[] = { &view };

	/*if (SL_FAILED(result, slEvaluateFeature(sl::kFeatureDLSS_RR, *_frameToken, inputs, _countof(inputs), cmdList)))
	{
		LOG_CRIT("Failed to evaluate Streamline feature: %d", result);
	}*/

	if (SL_FAILED(result, slEvaluateFeature(sl::kFeatureDLSS, *_frameToken, inputs, _countof(inputs), cmdList)))
	{
		LOG_CRIT("Failed to evaluate Streamline feature: %d", result);
	}
}