

#include "GraphicsDeviceD3D11.hpp"
#include "Texture3D.hpp"
#include "Shader.hpp"
#include <HexEngine.Core/HexEngine.hpp>
#include <HexEngine.Core/Entity/Component/Transform.hpp>
#include <HexEngine.Core/Entity/Component/DirectionalLight.hpp>
#include <HexEngine.Core/Entity/Component/PointLight.hpp>
#include <HexEngine.Core/Entity/Component/SpotLight.hpp>
#include <HexEngine.Core/Graphics/IStreamlineProvider.hpp>

#include <DirectXTex\DirectXTex.h>
#include <dxgi1_6.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

HexEngine::HVar r_vsync("r_vsync", "The type of V-Sync to use. 0 = Off, 1 = Full, 2 = Half", 0, 0, 2);
HexEngine::HVar r_hdrOutput("r_hdrOutput", "Enable HDR scRGB output when supported by the active display", true, false, true);

/*GraphicsSystemD3D11::GraphicsSystemD3D11() :
	_device(nullptr),
	_deviceContext(nullptr),
	_featureLevelSupported(D3D_FEATURE_LEVEL_1_0_CORE),
	_swapChain(nullptr),
	_renderTargetView(nullptr)
{}*/

const int32_t MsaaLevel = 1;

namespace
{
	DXGI_FORMAT GetPresentationFormat(bool hdrOutputActive)
	{
		return hdrOutputActive ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
	}

	bool IsHdrOutputColorSpace(DXGI_COLOR_SPACE_TYPE colorSpace)
	{
		switch (colorSpace)
		{
		case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020:
		case DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020:
			return true;
		default:
			return false;
		}
	}

	bool WindowSupportsHDR(IDXGIAdapter* adapter, HWND hwnd)
	{
		if (!adapter || !hwnd)
			return false;

		const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
		if (!monitor)
			return false;

		for (UINT outputIndex = 0;; ++outputIndex)
		{
			IDXGIOutput* output = nullptr;
			const HRESULT hr = adapter->EnumOutputs(outputIndex, &output);
			if (hr == DXGI_ERROR_NOT_FOUND)
				break;
			if (FAILED(hr) || !output)
				break;

			DXGI_OUTPUT_DESC outputDesc = {};
			const bool isMatchingOutput = SUCCEEDED(output->GetDesc(&outputDesc)) && outputDesc.Monitor == monitor;
			if (!isMatchingOutput)
			{
				SAFE_RELEASE(output);
				continue;
			}

			bool hdrSupported = false;
			IDXGIOutput6* output6 = nullptr;
			if (SUCCEEDED(output->QueryInterface(__uuidof(IDXGIOutput6), reinterpret_cast<void**>(&output6))) && output6)
			{
				DXGI_OUTPUT_DESC1 outputDesc1 = {};
				if (SUCCEEDED(output6->GetDesc1(&outputDesc1)))
				{
					hdrSupported = IsHdrOutputColorSpace(outputDesc1.ColorSpace) && outputDesc1.MaxLuminance > 0.0f;
				}
			}

			SAFE_RELEASE(output6);
			SAFE_RELEASE(output);
			return hdrSupported;
		}

		return false;
	}

	void ConfigureSwapChainColorSpace(IDXGISwapChain* swapchain, bool hdrOutputActive)
	{
		if (!swapchain)
			return;

		IDXGISwapChain3* swapchain3 = nullptr;
		if (FAILED(swapchain->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void**>(&swapchain3))) || !swapchain3)
			return;

		const DXGI_COLOR_SPACE_TYPE targetColorSpace = hdrOutputActive
			? DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709
			: DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;

		UINT colorSpaceSupport = 0;
		if (SUCCEEDED(swapchain3->CheckColorSpaceSupport(targetColorSpace, &colorSpaceSupport))
			&& (colorSpaceSupport & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT))
		{
			swapchain3->SetColorSpace1(targetColorSpace);
		}

		SAFE_RELEASE(swapchain3);
	}
}

bool GraphicsDeviceD3D11::Create()
{
	//g_pEnv->_commandManager->RegisterVar(&r_vsync);

	g_pGraphics = this;

	UINT createDeviceFlags = 0;

#ifdef _DEBUG
	createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
	D3D_FEATURE_LEVEL featureLevels[] =
	{
		//D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0,
	};

	UINT numFeatureLevels = ARRAYSIZE(featureLevels);

	HRESULT hr;

	if (HexEngine::g_pEnv->_streamlineProvider && HexEngine::g_pEnv->_streamlineProvider->IsEnabled())
	{
		hr = HexEngine::g_pEnv->_streamlineProvider->D3D11CreateDevice(
			nullptr,
			D3D_DRIVER_TYPE_HARDWARE,
			nullptr,
			createDeviceFlags,
			featureLevels,
			numFeatureLevels,
			D3D11_SDK_VERSION,
			&_device,
			&_featureLevelSupported,
			&_deviceContext);
	}
	else
	{
		hr = D3D11CreateDevice(
			nullptr,
			D3D_DRIVER_TYPE_HARDWARE,
			nullptr,
			createDeviceFlags,
			featureLevels,
			numFeatureLevels,
			D3D11_SDK_VERSION,
			&_device,
			&_featureLevelSupported,
			&_deviceContext);
	}

	if (FAILED(hr))
	{
		LOG_CRIT("D3D11CreateDevice failed: 0x%X", hr);
		return false;
	}

	if (CreateFactory() == false)
	{
		LOG_CRIT("CreateFactory() failed");
		return false;
	}

#ifdef _DEBUG
	ID3D11Debug* d3dDebug = nullptr;
	if (SUCCEEDED(_device->QueryInterface(__uuidof(ID3D11Debug), (void**)&d3dDebug)))
	{
		ID3D11InfoQueue* d3dInfoQueue = nullptr;
		if (SUCCEEDED(d3dDebug->QueryInterface(__uuidof(ID3D11InfoQueue), (void**)&d3dInfoQueue)))
		{
			d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, true);
			d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, true);
			d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_WARNING, true);


			D3D11_MESSAGE_ID hide[] =
			{
			D3D11_MESSAGE_ID_SETPRIVATEDATA_CHANGINGPARAMS,
			D3D11_MESSAGE_ID_DEVICE_DRAW_RENDERTARGETVIEW_NOT_SET,
			D3D11_MESSAGE_ID_DEVICE_DRAW_VIEW_DIMENSION_MISMATCH,
			// Add more message IDs here as needed
			};

			D3D11_INFO_QUEUE_FILTER filter = {};
			filter.DenyList.NumIDs = _countof(hide);
			filter.DenyList.pIDList = hide;
			d3dInfoQueue->AddStorageFilterEntries(&filter);
			d3dInfoQueue->Release();
		}
		d3dDebug->Release();
	}
#endif

	_engineConstantBuffers[(uint32_t)HexEngine::EngineConstantBuffer::PerFrameBuffer] = CreateConstantBuffer(sizeof(HexEngine::PerFrameConstantBuffer));
	_engineConstantBuffers[(uint32_t)HexEngine::EngineConstantBuffer::PerObjectBuffer] = CreateConstantBuffer(sizeof(HexEngine::PerObjectBuffer));
	_engineConstantBuffers[(uint32_t)HexEngine::EngineConstantBuffer::PerShadowCasterBuffer] = CreateConstantBuffer(sizeof(HexEngine::PerShadowCasterBuffer));
	_engineConstantBuffers[(uint32_t)HexEngine::EngineConstantBuffer::PerAnimationBuffer] = CreateConstantBuffer(sizeof(HexEngine::PerAnimationBuffer));

	_textureLoader = new TextureImporter;

	_states = new dx::CommonStates(_device);

	return true;
}

bool GraphicsDeviceD3D11::CreateFactory()
{


	//_dxgiFactory->EnumAdapters()

	HRESULT hr = _device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&_dxgiDevice));
	if (FAILED(hr))
	{
		LOG_CRIT("Could not obtain IDXGIDevice interface (0x%X)", hr);
		return false;
	}

	hr = _dxgiDevice->GetAdapter(&_dxgiAdapter);

	if (FAILED(hr))
	{
		LOG_CRIT("Could not obtain IDXGIAdapter interface (0x%X)", hr);
		return false;
	}

	if (HexEngine::g_pEnv->_streamlineProvider && HexEngine::g_pEnv->_streamlineProvider->IsEnabled())
	{
		CHECK_HR(HexEngine::g_pEnv->_streamlineProvider->CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&_dxgiFactory)));
	}
	else
	{
		CHECK_HR(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&_dxgiFactory)));
	}

	//hr = _dxgiAdapter->GetParent(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&_dxgiFactory));

	if (FAILED(hr))
	{
		LOG_CRIT("Could not obtain IDXGIFactory1 (0x%X)", hr);
		return false;
	}

	hr = _dxgiAdapter->EnumOutputs(0, &_dxgiOutput);

	if (FAILED(hr))
	{
		LOG_CRIT("Could not obtain IDXGIOutput (0x%X)", hr);
		return false;
	}

	return true;
}

bool GraphicsDeviceD3D11::GetSupportedDisplayModes(std::vector<HexEngine::ScreenDisplayMode>& modes)
{
	uint32_t numModes = 0;

	// Get the number of modes that fit the DXGI_FORMAT_R8G8B8A8_UNORM display format for the adapter output (monitor).
	HRESULT hr = _dxgiOutput->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_ENUM_MODES_INTERLACED, &numModes, NULL);

	if (FAILED(hr))
	{
		return false;
	}

	if (numModes <= 0)
	{
		LOG_DEBUG("The graphicss device reports no supported display modes!");
		return false;
	}

	DXGI_MODE_DESC* supportedModes = new DXGI_MODE_DESC[numModes];

	hr = _dxgiOutput->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_ENUM_MODES_INTERLACED, &numModes, supportedModes);

	if (FAILED(hr))
	{
		delete[] supportedModes;
		return false;
	}

	for (uint32_t i = 0; i < numModes; ++i)
	{
		HexEngine::ScreenDisplayMode mode;

		mode.width = supportedModes[i].Width;
		mode.height = supportedModes[i].Height;
		mode.refresh.numerator = supportedModes[i].RefreshRate.Numerator;
		mode.refresh.denominator = supportedModes[i].RefreshRate.Denominator;

		modes.push_back(mode);
	}

	_supportedScreenDisplayModes = modes;

	delete[] supportedModes;

	return modes.size() > 0;
}

void GraphicsDeviceD3D11::Destroy()
{
	SAFE_DELETE(_states);

	for (int i = 0; i < (int32_t)HexEngine::EngineConstantBuffer::NumEngineConstantBuffers; ++i)
	{
		SAFE_DELETE(_engineConstantBuffers[i]);
	}

	//SAFE_RELEASE(_rasterState);
	//SAFE_RELEASE(_rasterStateCullFront);
	//SAFE_RELEASE(_rasterStateCullNone);
	SAFE_RELEASE(_subtractivetBlendState);
	//SAFE_RELEASE(_depthStencilView);

	/*for (int i = 0; i < _countof(_shadowMap); ++i)
	{
		SAFE_DELETE(_shadowMap[i]);
	}*/

	//_gbuffer.Destroy();
	//_renderTexture->Destroy();
	//SAFE_DELETE(_composedTexture);

	//SAFE_RELEASE(_texSamplerClamp);
	//SAFE_RELEASE(_texSamplerWrap);
	SAFE_RELEASE(_texSamplerComparison);

	SAFE_DELETE(_textureLoader);

	SAFE_RELEASE(_deviceContext);

	for (auto& device : _deviceData)
	{
		SAFE_DELETE(device.second.backbuffer);
		SAFE_RELEASE(device.second.swapchain);
	}

	SAFE_RELEASE(_dxgiOutput);
	SAFE_RELEASE(_dxgiFactory);
	SAFE_RELEASE(_dxgiAdapter);
	SAFE_RELEASE(_dxgiDevice);

#if 0//def _DEBUG
	ID3D11Debug* debugDev;
	_device->QueryInterface(__uuidof(ID3D11Debug), reinterpret_cast<void**>(&debugDev));

	debugDev->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL | D3D11_RLDO_IGNORE_INTERNAL);
	debugDev->Release();
#endif

	SAFE_RELEASE(_device);
}

void GraphicsDeviceD3D11::DestroyInternal()
{

}

bool GraphicsDeviceD3D11::AttachToWindow(HexEngine::Window* window)
{
	auto& device = _deviceData[window];

	_bbufferWidth = window->GetClientWidth();
	_bbufferHeight = window->GetClientHeight();

	HRESULT hr;

	const bool hdrOutputActive = r_hdrOutput._val.b && WindowSupportsHDR(_dxgiAdapter, window->GetHandle());
	const DXGI_FORMAT backbufferFormat = GetPresentationFormat(hdrOutputActive);

	UINT quality = 0;
	_device->CheckMultisampleQualityLevels(backbufferFormat, MsaaLevel, &quality);

	std::reverse(_supportedScreenDisplayModes.begin(), _supportedScreenDisplayModes.end());

	HexEngine::ScreenDisplayMode* pDm = nullptr;

	/*for (auto&& dm : _supportedScreenDisplayModes)
	{
		if (dm.width == window->GetWidth() && dm.height == window->GetHeight())
		{
			pDm = &dm;
			break;
		}
	}*/

	bool fullscreen = (window->GetDisplayMode() == HexEngine::DisplayMode::Fullscreen);

	DXGI_SWAP_CHAIN_DESC sd;
	ZeroMemory(&sd, sizeof(sd));
	sd.BufferCount = 2;
	sd.BufferDesc.Width = window->GetClientWidth();
	sd.BufferDesc.Height = window->GetClientHeight();
	sd.BufferDesc.Format = backbufferFormat;
	sd.BufferDesc.RefreshRate.Numerator = pDm ? pDm->refresh.numerator : 0;
	sd.BufferDesc.RefreshRate.Denominator = pDm ? pDm->refresh.denominator : 1;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT;
	sd.OutputWindow = window->GetHandle();
	sd.SampleDesc.Count = hdrOutputActive ? 1 : MsaaLevel;
	sd.SampleDesc.Quality = hdrOutputActive || quality == 0 ? 0 : quality - 1;
	sd.Windowed = !fullscreen;
	sd.SwapEffect = hdrOutputActive ? DXGI_SWAP_EFFECT_FLIP_DISCARD : DXGI_SWAP_EFFECT_DISCARD;
	sd.Flags = fullscreen ? DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH : 0;

	CHECK_HR(_dxgiFactory->CreateSwapChain(_device, &sd, &device.swapchain));

	if (sd.Windowed == FALSE)
	{
		device.swapchain->SetFullscreenState(TRUE, NULL);
	}

	memcpy(&device.swapchainDesc, &sd, sizeof(sd));
	device.backbufferFormat = backbufferFormat;
	device.hdrOutputActive = hdrOutputActive;

	ConfigureSwapChainColorSpace(device.swapchain, hdrOutputActive);

	CHECK_HR(_dxgiFactory->MakeWindowAssociation(window->GetHandle(), DXGI_MWA_NO_ALT_ENTER));

	ID3D11Texture2D* pBackBuffer = nullptr;
	hr = device.swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&pBackBuffer));
	if (FAILED(hr))
	{
		return hr;
	}

	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = device.backbufferFormat;
	rtvDesc.ViewDimension = sd.SampleDesc.Count > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D;

	ID3D11RenderTargetView* renderTargetView = nullptr;
	CHECK_HR(_device->CreateRenderTargetView(pBackBuffer, &rtvDesc, &renderTargetView));

	device.backbuffer = new Texture2D;
	device.backbuffer->_renderTargetView = renderTargetView;
	renderTargetView->AddRef();

	device.backbuffer->_format = rtvDesc.Format;
	device.backbuffer->_width = sd.BufferDesc.Width;
	device.backbuffer->_height = sd.BufferDesc.Height;

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = device.backbufferFormat;
	srvDesc.ViewDimension = sd.SampleDesc.Count > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D;
	if (srvDesc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D)
	{
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;
	}

	ID3D11ShaderResourceView* renderTargetSRV = nullptr;
	CHECK_HR(_device->CreateShaderResourceView(pBackBuffer, &srvDesc, &renderTargetSRV));

	device.backbuffer->_shaderResourceView = renderTargetSRV;
	renderTargetSRV->AddRef();

	pBackBuffer->Release();

	return CreateInternal();
}

void GraphicsDeviceD3D11::Resize(HexEngine::Window* window, uint32_t width, uint32_t height)
{
	auto it = _deviceData.find(window);

	if (it == _deviceData.end())
	{
		LOG_CRIT("Trying to resize and unknown device");
		return;
	}

	auto& device = it->second;

	_bbufferWidth = width;
	_bbufferHeight = height;

	SAFE_DELETE(device.backbuffer);

	LOG_DEBUG("Resizing graphics device to %dx%d", width, height);

	device.swapchainDesc.BufferDesc.Width = width;
	device.swapchainDesc.BufferDesc.Height = height;

	CHECK_HR(device.swapchain->ResizeBuffers(device.swapchainDesc.BufferCount, width, height, device.swapchainDesc.BufferDesc.Format, device.swapchainDesc.Flags));
	ConfigureSwapChainColorSpace(device.swapchain, device.hdrOutputActive);

	ID3D11Texture2D* pBackBuffer = nullptr;
	HRESULT hr = device.swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&pBackBuffer));
	if (FAILED(hr))
	{
		return;
	}

	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = device.backbufferFormat;
	rtvDesc.ViewDimension = device.swapchainDesc.SampleDesc.Count > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D;

	ID3D11RenderTargetView* renderTargetView = nullptr;
	CHECK_HR(_device->CreateRenderTargetView(pBackBuffer, &rtvDesc, &renderTargetView));

	device.backbuffer = new Texture2D;
	device.backbuffer->_renderTargetView = renderTargetView;
	renderTargetView->AddRef();

	device.backbuffer->_format = rtvDesc.Format;
	device.backbuffer->_width = width;
	device.backbuffer->_height = height;

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = device.backbufferFormat;
	srvDesc.ViewDimension = device.swapchainDesc.SampleDesc.Count > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D;
	if (srvDesc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D)
	{
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;
	}

	ID3D11ShaderResourceView* renderTargetSRV = nullptr;
	CHECK_HR(_device->CreateShaderResourceView(pBackBuffer, &srvDesc, &renderTargetSRV));

	device.backbuffer->_shaderResourceView = renderTargetSRV;
	renderTargetSRV->AddRef();

	pBackBuffer->Release();

	_bbufferViewport.Width = (FLOAT)_bbufferWidth;
	_bbufferViewport.Height = (FLOAT)_bbufferHeight;
	_bbufferViewport.MinDepth = 0.0f;
	_bbufferViewport.MaxDepth = 1.0f;
	_bbufferViewport.TopLeftX = 0;
	_bbufferViewport.TopLeftY = 0;
	_deviceContext->RSSetViewports(1, &_bbufferViewport);
}

bool GraphicsDeviceD3D11::CreateInternal()
{
	HRESULT hr;

	_bbufferViewport.Width = (FLOAT)_bbufferWidth;
	_bbufferViewport.Height = (FLOAT)_bbufferHeight;
	_bbufferViewport.MinDepth = 0.0f;
	_bbufferViewport.MaxDepth = 1.0f;
	_bbufferViewport.TopLeftX = 0;
	_bbufferViewport.TopLeftY = 0;
	_deviceContext->RSSetViewports(1, &_bbufferViewport);

	D3D11_RASTERIZER_DESC rasterDesc;
	rasterDesc.AntialiasedLineEnable = false;
	rasterDesc.CullMode = D3D11_CULL_BACK;
	rasterDesc.DepthBias = -1000;
	rasterDesc.DepthBiasClamp = 0.0f;
	rasterDesc.DepthClipEnable = false;
	rasterDesc.FillMode = D3D11_FILL_SOLID;
	rasterDesc.FrontCounterClockwise = true;
	rasterDesc.MultisampleEnable = true;
	rasterDesc.ScissorEnable = false;
	rasterDesc.SlopeScaledDepthBias = 0.01f;

	// Create the rasterizer state from the description we just filled out.
	//hr = _device->CreateRasterizerState(&rasterDesc, &_rasterState);
	//_deviceContext->RSSetState(_rasterState);

	//rasterDesc.CullMode = D3D11_CULL_FRONT;
	//hr = _device->CreateRasterizerState(&rasterDesc, &_rasterStateCullFront);

	//rasterDesc.CullMode = D3D11_CULL_NONE;
	//hr = _device->CreateRasterizerState(&rasterDesc, &_rasterStateCullNone);

	CD3D11_DEFAULT def;
	CD3D11_BLEND_DESC transparentDesc(def);
	transparentDesc.RenderTarget[0].BlendEnable = true;
	transparentDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	transparentDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
	transparentDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	transparentDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	transparentDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_SUBTRACT;

	_device->CreateBlendState(&transparentDesc, &_subtractivetBlendState);

	D3D11_SAMPLER_DESC sampDesc;
	ZeroMemory(&sampDesc, sizeof(sampDesc));
	sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sampDesc.MaxAnisotropy = 16;
	//sampDesc.MipLODBias = 0;
	sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
	//sampDesc.MinLOD = 0;
	sampDesc.MaxLOD = D3D11_FLOAT32_MAX;

	//hr = _device->CreateSamplerState(&sampDesc, &_texSamplerClamp);

	sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;

	//hr = _device->CreateSamplerState(&sampDesc, &_texSamplerWrap);

	sampDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
	sampDesc.ComparisonFunc = D3D11_COMPARISON_LESS;
	sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_MIRROR;
	sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_MIRROR;

	hr = _device->CreateSamplerState(&sampDesc, &_texSamplerComparison);

	sampDesc.Filter = D3D11_FILTER_ANISOTROPIC;
	sampDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
	sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_MIRROR;
	sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_MIRROR;
	sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_MIRROR;

	hr = _device->CreateSamplerState(&sampDesc, &_texSamplerMirrored);


	//D3D11_FILTER_MIN_MAG_MIP_POINT


	_clearColour = math::Color(HEX_RGBA_TO_FLOAT4(44, 44, 45, 255));

	ClearScissorRect();

	return true;
}

//void GraphicsDeviceD3D11::BeginScene()
//{
//	//_deviceContext->OMSetDepthStencilState(_depthStencilState, 1);
//	//_deviceContext->OMSetRenderTargets(1, &_renderTargetView, _depthStencilView);

//	float blendFactor[4] = { 1.0f };
//	_deviceContext->OMSetBlendState(_transparentBlendState, 0/*blendFactor*/, 0xFFFFFFFF);

//	float clearCol[] = { 0.5f, 0.63f, 0.78f, 1.0f };

//	//_deviceContext->ClearRenderTargetView(_renderTargetView, clearCol);
//	//_deviceContext->ClearDepthStencilView(_depthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
//}

//void GraphicsDeviceD3D11::EndScene()
//{
//	bool halfVSync = false;// g_pEnv->_timeManager->_frameCount % 2 == 0;

//	HRESULT hr = _swapChain->Present(0/*halfVSync ? 1 : 0*/, 0/*DXGI_PRESENT_DO_NOT_WAIT*/);

//	if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
//	{
//		//rfx::gResourceManager.OnLostDevice();
//	}
//}

Texture2D* GraphicsDeviceD3D11::GetBackBuffer(HexEngine::Window* window)
{
	// if no window is specified use the first back buffer that was created
	if (window == nullptr)
	{
		if (_deviceData.size() > 0)
			return _deviceData.begin()->second.backbuffer;
	}
	else
	{
		auto it = _deviceData.find(window);

		if (it == _deviceData.end())
			return nullptr;

		return it->second.backbuffer;
	}
	return nullptr;
}

Texture2D* GraphicsDeviceD3D11::CreateTexture(HexEngine::ITexture2D* clone)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	auto d3d11Clone = reinterpret_cast<Texture2D*>(clone);

	ID3D11Texture2D* nativeClone = reinterpret_cast<ID3D11Texture2D*>(d3d11Clone->GetNativePtr());

	assert(nativeClone != nullptr && "Texture to clone from doesn't have a valid native pointer");

	D3D11_TEXTURE2D_DESC desc;
	nativeClone->GetDesc(&desc);

	ID3D11Texture2D* nativeTex;
	CHECK_HR(_device->CreateTexture2D(&desc, nullptr, &nativeTex));

	Texture2D* texture = new Texture2D;

	texture->_width = d3d11Clone->_width;
	texture->_height = d3d11Clone->_height;
	texture->_texture = nativeTex;
	texture->_format = d3d11Clone->_format;
	texture->_rtvDimension = d3d11Clone->_rtvDimension;
	texture->_srvDimension = d3d11Clone->_srvDimension;
	texture->_dsvDimension = d3d11Clone->_dsvDimension;

	// If its a render target
	//
	if (desc.BindFlags & D3D11_BIND_RENDER_TARGET)
	{
		for (auto i = 0; i < desc.ArraySize; ++i)
		{
			CD3D11_RENDER_TARGET_VIEW_DESC rtvDesc(
				(D3D11_RTV_DIMENSION)d3d11Clone->_rtvDimension,
				(DXGI_FORMAT)desc.Format,
				0,
				i,
				1);

			ID3D11RenderTargetView* renderTargetView;
			CHECK_HR(_device->CreateRenderTargetView(texture->_texture, &rtvDesc, &renderTargetView));

			texture->_renderTargetView = renderTargetView;
		}
	}

	if (desc.BindFlags & D3D11_BIND_SHADER_RESOURCE)
	{
		for (auto i = 0; i < desc.ArraySize; ++i)
		{
			CD3D11_SHADER_RESOURCE_VIEW_DESC rtvDesc(
				(D3D11_SRV_DIMENSION)d3d11Clone->_srvDimension,
				(DXGI_FORMAT)desc.Format,
				0,
				1,
				i,
				1);

			ID3D11ShaderResourceView* shaderResourceView;
			CHECK_HR(_device->CreateShaderResourceView(texture->_texture, &rtvDesc, &shaderResourceView));

			texture->_shaderResourceView = shaderResourceView;
		}
	}

	if (desc.BindFlags & D3D11_BIND_DEPTH_STENCIL)
	{
		for (auto i = 0; i < desc.ArraySize; ++i)
		{
			CD3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc(
				(D3D11_DSV_DIMENSION)d3d11Clone->_dsvDimension,
				(DXGI_FORMAT)desc.Format,
				0,
				i,
				1,
				0);

			ID3D11DepthStencilView* depthStencilView;
			CHECK_HR(_device->CreateDepthStencilView(texture->_texture, &dsvDesc, &depthStencilView));

			texture->_depthStencilView = depthStencilView;
		}
	}

	return texture;
}

Texture2D* GraphicsDeviceD3D11::CreateTexture2D(
	int32_t width,
	int32_t height,
	DXGI_FORMAT format,
	int32_t arraySize,
	uint32_t bindFlags,
	int32_t mipLevels,
	int32_t sampleCount,
	int32_t sampleQuality,
	D3D11_SUBRESOURCE_DATA* initialData,
	D3D11_CPU_ACCESS_FLAG access,
	D3D11_RTV_DIMENSION rtvDimension,
	D3D11_UAV_DIMENSION uavDimension,
	D3D11_SRV_DIMENSION srvDimension,
	D3D11_DSV_DIMENSION dsvDimension,
	D3D11_USAGE usage,
	uint32_t miscFlags)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	// Create the texture2D description
	//
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Format = format;
	desc.BindFlags = bindFlags;
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = mipLevels != D3D11_RESOURCE_MISC_GENERATE_MIPS ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
	desc.ArraySize = arraySize;
	desc.SampleDesc.Count = sampleCount;
	desc.SampleDesc.Quality = sampleQuality;
	desc.Usage = usage;
	desc.CPUAccessFlags = access;//(usage == D3D11_USAGE_STAGING ? (D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE) : 0);
	desc.MiscFlags = miscFlags;// D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;// (usage == D3D11_USAGE_STAGING ? (D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX) : 0);

	ID3D11Texture2D* d3dTexture;
	CHECK_HR(_device->CreateTexture2D(&desc, initialData, &d3dTexture));

	Texture2D* texture = new Texture2D;

	// Set the initial texture data
	//
	texture->_width = width;
	texture->_height = height;
	texture->_texture = d3dTexture;
	texture->_format = format;
	texture->_rtvDimension = rtvDimension;
	texture->_srvDimension = srvDimension;
	texture->_dsvDimension = dsvDimension;

	// If its a render target
	//
	if (bindFlags & D3D11_BIND_RENDER_TARGET)
	{
		for (auto i = 0; i < arraySize; ++i)
		{
			CD3D11_RENDER_TARGET_VIEW_DESC rtvDesc((D3D11_RTV_DIMENSION)rtvDimension, (DXGI_FORMAT)format, 0, i, 1);

			ID3D11RenderTargetView* renderTargetView;
			CHECK_HR(_device->CreateRenderTargetView(texture->_texture, &rtvDesc, &renderTargetView));

			texture->_renderTargetView = renderTargetView;
		}
	}

	if (bindFlags & D3D11_BIND_SHADER_RESOURCE)
	{
		for (auto i = 0; i < arraySize; ++i)
		{
			auto srvFormat = format;

			if (srvFormat == DXGI_FORMAT_R32_TYPELESS)
				srvFormat = DXGI_FORMAT_R32_FLOAT;
			else if (srvFormat == DXGI_FORMAT_R16_TYPELESS)
				srvFormat = DXGI_FORMAT_R16_FLOAT;
			else if (srvFormat == DXGI_FORMAT_R24G8_TYPELESS)
				srvFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
			else if (srvFormat == DXGI_FORMAT_R32G8X24_TYPELESS)
				srvFormat = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;

			CD3D11_SHADER_RESOURCE_VIEW_DESC rtvDesc((D3D11_SRV_DIMENSION)srvDimension, srvFormat, 0, 1, i, 1);

			ID3D11ShaderResourceView* shaderResourceView;
			CHECK_HR(_device->CreateShaderResourceView(texture->_texture, &rtvDesc, &shaderResourceView));

			texture->_shaderResourceView = shaderResourceView;
		}
	}

	if (bindFlags & D3D11_BIND_DEPTH_STENCIL)
	{
		for (auto i = 0; i < arraySize; ++i)
		{
			auto dsvFormat = format;

			/*if (dsvFormat == DXGI_FORMAT_R32_TYPELESS)
				dsvFormat = DXGI_FORMAT_D32_FLOAT;*/

			if (dsvFormat == DXGI_FORMAT_R32_TYPELESS)
				dsvFormat = DXGI_FORMAT_D32_FLOAT;
			else if (dsvFormat == DXGI_FORMAT_R16_TYPELESS)
				dsvFormat = DXGI_FORMAT_D16_UNORM;
			else if (dsvFormat == DXGI_FORMAT_R24G8_TYPELESS)
				dsvFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
			else if (dsvFormat == DXGI_FORMAT_R32G8X24_TYPELESS)
				dsvFormat = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;


			CD3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc(
				(D3D11_DSV_DIMENSION)dsvDimension,
				dsvFormat,
				0,
				i,
				1,
				0);

			ID3D11DepthStencilView* depthStencilView;
			CHECK_HR(_device->CreateDepthStencilView(texture->_texture, &dsvDesc, &depthStencilView));

			texture->_depthStencilView = depthStencilView;
		}
	}

	return texture;
}

//Texture2D* GraphicsDeviceD3D11::CreateTexture2D(
//	int32_t width,
//	int32_t height,
//	DXGI_FORMAT format,
//	int32_t arraySize,
//	uint32_t bindFlags,
//	int32_t mipLevels,
//	int32_t sampleCount,
//	int32_t sampleQuality,
//	D3D11_SUBRESOURCE_DATA* initialData,
//	D3D11_RTV_DIMENSION rtvDimension,
//	D3D11_UAV_DIMENSION uavDimension,
//	D3D11_SRV_DIMENSION srvDimension,
//	D3D11_DSV_DIMENSION dsvDimension)
//{
//	std::lock_guard<std::recursive_mutex> lock(_lock);

//	// Create the texture2D description
//	//
//	D3D11_TEXTURE2D_DESC desc = {};
//	desc.Format = static_cast<DXGI_FORMAT>(format); // the format structure is 1:1
//	desc.BindFlags = bindFlags;
//	desc.Width = width;
//	desc.Height = height;
//	desc.MipLevels = mipLevels != 1 ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
//	desc.ArraySize = arraySize;
//	desc.SampleDesc.Count = sampleCount;
//	desc.SampleDesc.Quality = sampleQuality;
//	desc.Usage = D3D11_USAGE_DEFAULT;
//	desc.CPUAccessFlags = 0;
//	desc.MiscFlags = 0;

//	/*D3D11_SUBRESOURCE_DATA data;
//	data.pSysMem = initialData.data();
//	data.SysMemPitch = initialData.size();
//	data.SysMemSlicePitch = 0;*/

//	ID3D11Texture2D* d3dTexture;
//	CHECK_HR(_device->CreateTexture2D(&desc, initialData, &d3dTexture));

//	Texture2D* texture = new Texture2D;

//	// Set the initial texture data
//	//
//	texture->_width = width;
//	texture->_height = height;
//	texture->_texture = d3dTexture;
//	texture->_format = format;
//	texture->_rtvDimension = rtvDimension;
//	texture->_srvDimension = srvDimension;
//	texture->_dsvDimension = dsvDimension;

//	// If its a render target
//	//
//	if (bindFlags & D3D11_BIND_RENDER_TARGET)
//	{
//		for (auto i = 0; i < arraySize; ++i)
//		{
//			CD3D11_RENDER_TARGET_VIEW_DESC rtvDesc((D3D11_RTV_DIMENSION)rtvDimension, (DXGI_FORMAT)format, 0, i, 1);

//			ID3D11RenderTargetView* renderTargetView;
//			CHECK_HR(_device->CreateRenderTargetView(texture->_texture, &rtvDesc, &renderTargetView));

//			texture->_renderTargetView = renderTargetView;
//		}
//	}

//	if (bindFlags & D3D11_BIND_SHADER_RESOURCE)
//	{
//		for (auto i = 0; i < arraySize; ++i)
//		{
//			CD3D11_SHADER_RESOURCE_VIEW_DESC rtvDesc((D3D11_SRV_DIMENSION)srvDimension, (DXGI_FORMAT)format, 0, 1, i, 1);

//			ID3D11ShaderResourceView* shaderResourceView;
//			CHECK_HR(_device->CreateShaderResourceView(texture->_texture, &rtvDesc, &shaderResourceView));

//			texture->_shaderResourceView = shaderResourceView;
//		}
//	}

//	if (bindFlags & D3D11_BIND_DEPTH_STENCIL)
//	{
//		for (auto i = 0; i < arraySize; ++i)
//		{
//			CD3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc(
//				(D3D11_DSV_DIMENSION)dsvDimension,
//				(DXGI_FORMAT)format,
//				0,
//				i,
//				1,
//				0);

//			ID3D11DepthStencilView* depthStencilView;
//			CHECK_HR(_device->CreateDepthStencilView(texture->_texture, &dsvDesc, &depthStencilView));

//			texture->_depthStencilView = depthStencilView;
//		}
//	}

//	return texture;
//}

HexEngine::ITexture3D* GraphicsDeviceD3D11::CreateTexture3D(
	int32_t width,
	int32_t height,
	int32_t depth,
	DXGI_FORMAT format,
	int32_t arraySize,
	uint32_t bindFlags,
	int32_t mipLevels,
	int32_t sampleCount,
	int32_t sampleQuality,
	D3D11_SUBRESOURCE_DATA* initialData,
	D3D11_RTV_DIMENSION rtvDimension,
	D3D11_UAV_DIMENSION uavDimension,
	D3D11_SRV_DIMENSION srvDimension,
	D3D11_DSV_DIMENSION dsvDimension)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	// Create the texture2D description
	//
	D3D11_TEXTURE3D_DESC desc = {};
	desc.Format = static_cast<DXGI_FORMAT>(format); // the format structure is 1:1
	desc.BindFlags = bindFlags;
	desc.Width = width;
	desc.Height = height;
	desc.Depth = depth;
	desc.MipLevels = mipLevels != 1 ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.CPUAccessFlags = 0;
	desc.MiscFlags = 0;

	/*D3D11_SUBRESOURCE_DATA data;
	data.pSysMem = initialData.data();
	data.SysMemPitch = initialData.size();
	data.SysMemSlicePitch = 0;*/

	ID3D11Texture3D* d3dTexture;
	CHECK_HR(_device->CreateTexture3D(&desc, initialData, &d3dTexture));

	Texture3D* texture = new Texture3D;

	// Set the initial texture data
	//
	texture->_width = width;
	texture->_height = height;
	texture->_depth = depth;
	texture->_texture = d3dTexture;
	texture->_format = format;
	texture->_rtvDimension = rtvDimension;
	texture->_srvDimension = srvDimension;
	texture->_dsvDimension = dsvDimension;

	// If its a render target
	//
	if (bindFlags & D3D11_BIND_RENDER_TARGET)
	{
		for (auto i = 0; i < arraySize; ++i)
		{
			CD3D11_RENDER_TARGET_VIEW_DESC rtvDesc((D3D11_RTV_DIMENSION)rtvDimension, (DXGI_FORMAT)format, 0, i, 1);

			ID3D11RenderTargetView* renderTargetView;
			CHECK_HR(_device->CreateRenderTargetView(texture->_texture, &rtvDesc, &renderTargetView));

			texture->_renderTargetView = renderTargetView;
		}
	}

	if (bindFlags & D3D11_BIND_SHADER_RESOURCE)
	{
		for (auto i = 0; i < arraySize; ++i)
		{
			CD3D11_SHADER_RESOURCE_VIEW_DESC rtvDesc((D3D11_SRV_DIMENSION)srvDimension, (DXGI_FORMAT)format, 0, 1, i, 1);

			ID3D11ShaderResourceView* shaderResourceView;
			CHECK_HR(_device->CreateShaderResourceView(texture->_texture, &rtvDesc, &shaderResourceView));

			texture->_shaderResourceView = shaderResourceView;
		}
	}

	if (bindFlags & D3D11_BIND_DEPTH_STENCIL)
	{
		for (auto i = 0; i < arraySize; ++i)
		{
			CD3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc(
				(D3D11_DSV_DIMENSION)dsvDimension,
				(DXGI_FORMAT)format,
				0,
				i,
				1,
				0);

			ID3D11DepthStencilView* depthStencilView;
			CHECK_HR(_device->CreateDepthStencilView(texture->_texture, &dsvDesc, &depthStencilView));

			texture->_depthStencilView = depthStencilView;
		}
	}

	return texture;
}

void GraphicsDeviceD3D11::GetBackBufferDimensions(uint32_t& width, uint32_t& height)
{
	width = _bbufferWidth;
	height = _bbufferHeight;
}

VertexBuffer* GraphicsDeviceD3D11::CreateVertexBuffer(int32_t byteWidth, uint32_t byteStride, D3D11_USAGE usage, uint32_t cpuAccessFlags)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	D3D11_BUFFER_DESC desc;
	desc.ByteWidth = byteWidth;
	desc.StructureByteStride = byteStride;
	desc.Usage = usage;
	desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	desc.CPUAccessFlags = cpuAccessFlags;
	desc.MiscFlags = 0;

	ID3D11Buffer* d3dBuffer;
	CHECK_HR(_device->CreateBuffer(&desc, nullptr, &d3dBuffer));

	VertexBuffer* buffer = new VertexBuffer;

	buffer->_buffer = d3dBuffer;
	buffer->_stride = byteStride;

	return buffer;
}

VertexBuffer* GraphicsDeviceD3D11::CreateVertexBuffer(int32_t byteWidth, uint32_t byteStride, D3D11_USAGE usage, uint32_t cpuAccessFlags, void* vertices)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	D3D11_BUFFER_DESC desc;
	desc.ByteWidth = byteWidth;
	desc.StructureByteStride = byteStride;
	desc.Usage = usage;
	desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	desc.CPUAccessFlags = cpuAccessFlags;
	desc.MiscFlags = 0;

	D3D11_SUBRESOURCE_DATA vertexData = { 0 };
	vertexData.pSysMem = vertices;
	vertexData.SysMemPitch = 0;
	vertexData.SysMemSlicePitch = 0;

	ID3D11Buffer* d3dBuffer;
	CHECK_HR(_device->CreateBuffer(&desc, &vertexData, &d3dBuffer));

	VertexBuffer* buffer = new VertexBuffer;

	buffer->_buffer = d3dBuffer;
	buffer->_stride = byteStride;

	return buffer;
}

IndexBuffer* GraphicsDeviceD3D11::CreateIndexBuffer(int32_t byteWidth, uint32_t byteStride, D3D11_USAGE usage, uint32_t cpuAccessFlags)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	D3D11_BUFFER_DESC desc;
	desc.ByteWidth = byteWidth;
	desc.StructureByteStride = byteStride;
	desc.Usage = usage;
	desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	desc.CPUAccessFlags = cpuAccessFlags;
	desc.MiscFlags = 0;

	ID3D11Buffer* d3dBuffer;
	CHECK_HR(_device->CreateBuffer(&desc, nullptr, &d3dBuffer));

	IndexBuffer* buffer = new IndexBuffer;
	buffer->_buffer = d3dBuffer;

	return buffer;
}

IndexBuffer* GraphicsDeviceD3D11::CreateIndexBuffer(int32_t byteWidth, uint32_t byteStride, D3D11_USAGE usage, uint32_t cpuAccessFlags, void* indices)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	if (cpuAccessFlags != 0)
	{
		LOG_CRIT("An index buffer must have CPU access flags with 0 when created with initial data. CPUAccessFlags = 0x%X", cpuAccessFlags);
		return nullptr;
	}

	D3D11_BUFFER_DESC desc;
	desc.ByteWidth = byteWidth;
	desc.StructureByteStride = byteStride;
	desc.Usage = usage;
	desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	desc.CPUAccessFlags = cpuAccessFlags;
	desc.MiscFlags = 0;

	D3D11_SUBRESOURCE_DATA indexData = { 0 };
	indexData.pSysMem = indices;

	ID3D11Buffer* d3dBuffer;
	CHECK_HR(_device->CreateBuffer(&desc, &indexData, &d3dBuffer));

	IndexBuffer* buffer = new IndexBuffer;
	buffer->_buffer = d3dBuffer;

	return buffer;
}

ShaderStageImpl<ID3D11VertexShader>* GraphicsDeviceD3D11::CreateVertexShader(std::vector<uint8_t>& shaderCode)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	ID3D11VertexShader* d3dShader;
	CHECK_HR(_device->CreateVertexShader(shaderCode.data(), shaderCode.size(), nullptr, &d3dShader));

	ShaderStageImpl<ID3D11VertexShader>* shader = new ShaderStageImpl<ID3D11VertexShader>;
	shader->_shader = d3dShader;
	shader->_shaderCode = shaderCode;

	return shader;
}

ShaderStageImpl<ID3D11PixelShader>* GraphicsDeviceD3D11::CreatePixelShader(std::vector<uint8_t>& shaderCode)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	ID3D11PixelShader* d3dShader;
	CHECK_HR(_device->CreatePixelShader((void*)shaderCode.data(), shaderCode.size(), nullptr, &d3dShader));

	ShaderStageImpl<ID3D11PixelShader>* shader = new ShaderStageImpl<ID3D11PixelShader>;
	shader->_shader = d3dShader;
	shader->_shaderCode = shaderCode;

	return shader;
}

ShaderStageImpl<ID3D11ComputeShader>* GraphicsDeviceD3D11::CreateComputeShader(std::vector<uint8_t>& shaderCode)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	ID3D11ComputeShader* d3dShader = nullptr;
	CHECK_HR(_device->CreateComputeShader(shaderCode.data(), shaderCode.size(), nullptr, &d3dShader));

	auto* shader = new ShaderStageImpl<ID3D11ComputeShader>;
	shader->_shader = d3dShader;
	shader->_shaderCode = shaderCode;
	return shader;
}

InputLayout* GraphicsDeviceD3D11::CreateInputLayout(D3D11_INPUT_ELEMENT_DESC* desc, uint32_t numElements, const std::vector<uint8_t>& vertexShaderBinary)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	ID3D11InputLayout* d3dLayout;
	CHECK_HR(_device->CreateInputLayout(desc, numElements, vertexShaderBinary.data(), vertexShaderBinary.size(), &d3dLayout));

	InputLayout* layout = new InputLayout;
	layout->_inputLayout = d3dLayout;

	return layout;
}

ConstantBuffer* GraphicsDeviceD3D11::CreateConstantBuffer(uint32_t size)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	D3D11_BUFFER_DESC desc = {};
	desc.ByteWidth = size;
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	desc.MiscFlags = 0;
	desc.StructureByteStride = 0;

	ID3D11Buffer* d3dBuffer;
	CHECK_HR(_device->CreateBuffer(&desc, nullptr, &d3dBuffer));

	ConstantBuffer* buffer = new ConstantBuffer(size);
	buffer->_buffer = d3dBuffer;

	return buffer;
}

ConstantBuffer* GraphicsDeviceD3D11::GetEngineConstantBuffer(HexEngine::EngineConstantBuffer buffer)
{
	uint32_t idx = static_cast<uint32_t>(buffer);

	if (idx < 0 || idx >= (uint32_t)HexEngine::EngineConstantBuffer::NumEngineConstantBuffers)
		return nullptr;

	return _engineConstantBuffers[idx];
}

/*ID3D11Device*/void* GraphicsDeviceD3D11::GetNativeDevice()
{
	return _device;
}

/*ID3D11DeviceContext*/void* GraphicsDeviceD3D11::GetNativeDeviceContext()
{
	return _deviceContext;
}

void GraphicsDeviceD3D11::SetVertexBuffer(uint32_t slot, HexEngine::IVertexBuffer* buffer)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	if (buffer)
	{
		if (buffer != _prevRenderState._vbuffer)
		{
			ID3D11Buffer* nativeBuffer = reinterpret_cast<ID3D11Buffer*>(buffer->GetNativePtr());
			uint32_t stride = buffer->GetStride();
			uint32_t offset = 0;

			_deviceContext->IASetVertexBuffers(slot, 1, &nativeBuffer, &stride, &offset);

			_prevRenderState._vbuffer = buffer;
		}
	}
	else
	{
		ID3D11Buffer* nativeBuffer = nullptr;
		uint32_t stride = 0;
		uint32_t offset = 0;

		_deviceContext->IASetVertexBuffers(slot, 1, &nativeBuffer, &stride, &offset);
	}
}

void GraphicsDeviceD3D11::SetIndexBuffer(HexEngine::IIndexBuffer* buffer)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	assert(buffer != nullptr && "IIndexBuffer cannot be null");

	if (buffer != _prevRenderState._ibuffer)
	{
		ID3D11Buffer* nativeBuffer = reinterpret_cast<ID3D11Buffer*>(buffer->GetNativePtr());

		auto dxgiFormat = sizeof(HexEngine::MeshIndexFormat) == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;

		_deviceContext->IASetIndexBuffer(nativeBuffer, dxgiFormat, 0);

		_prevRenderState._ibuffer = buffer;
	}
}

void GraphicsDeviceD3D11::SetTopology(D3D_PRIMITIVE_TOPOLOGY topology)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	if (topology != _prevRenderState._topology)
	{
		_deviceContext->IASetPrimitiveTopology(topology);

		_prevRenderState._topology = topology;
	}
}

void GraphicsDeviceD3D11::SetVertexShader(HexEngine::IShaderStage* shader)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	if (shader != _prevRenderState._vertexShader)
	{
		if (shader)
		{
			_deviceContext->VSSetShader(reinterpret_cast<ID3D11VertexShader*>(shader->GetNativePtr()), nullptr, 0);
		}
		else
		{
			_deviceContext->VSSetShader(nullptr, nullptr, 0);
		}

		_prevRenderState._vertexShader = shader;
	}
}

void GraphicsDeviceD3D11::SetPixelShader(HexEngine::IShaderStage* shader)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	if (shader != _prevRenderState._pixelShader)
	{
		if (shader)
		{
			_deviceContext->PSSetShader(reinterpret_cast<ID3D11PixelShader*>(shader->GetNativePtr()), nullptr, 0);
		}
		else
		{
			_deviceContext->PSSetShader(nullptr, nullptr, 0);
		}

		_prevRenderState._pixelShader = shader;
	}
}

void GraphicsDeviceD3D11::SetInputLayout(HexEngine::IInputLayout* layout)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	if (layout == nullptr)
		return;

	_deviceContext->IASetInputLayout(reinterpret_cast<ID3D11InputLayout*>(layout->GetNativePtr()));
}

void GraphicsDeviceD3D11::SetConstantBufferVS(uint32_t slot, HexEngine::IConstantBuffer* buffer)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	if (_prevRenderState._vsConstant != buffer)
	{
		ID3D11Buffer* bufferArray[] = { reinterpret_cast<ID3D11Buffer*>(buffer->GetNativePtr()) };

		_deviceContext->VSSetConstantBuffers(slot, 1, bufferArray);

		_prevRenderState._vsConstant = buffer;
	}
}

void GraphicsDeviceD3D11::SetConstantBufferPS(uint32_t slot, HexEngine::IConstantBuffer* buffer)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	if (_prevRenderState._psConstant != buffer)
	{
		ID3D11Buffer* bufferArray[] = { reinterpret_cast<ID3D11Buffer*>(buffer->GetNativePtr()) };

		_deviceContext->PSSetConstantBuffers(slot, 1, bufferArray);

		_prevRenderState._psConstant = buffer;
	}
}

void GraphicsDeviceD3D11::SetTexture2D(uint32_t slot, HexEngine::ITexture2D* texture)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	if (texture)
	{
		auto tex = reinterpret_cast<Texture2D*>(texture);

		SetPixelShaderResource(slot, tex->_shaderResourceView);
	}
	else
	{
		SetPixelShaderResource(slot, nullptr);
	}

	//if (texture)
	//{
	//	auto tex = reinterpret_cast<Texture2D*>(texture);

	//	if (tex->_shaderResourceView.size() > 0)
	//	{
	//		ID3D11ShaderResourceView* srv[] = { tex->_shaderResourceView.at(0)/*, _gbuffer._depth->_shaderResourceView.at(0)*/ };
	//		_deviceContext->PSSetShaderResources(slot, 1, srv);
	//	}
	//}
	/*else
	{
		ID3D11ShaderResourceView* null = nullptr;
		_deviceContext->PSSetShaderResources(slot, 1, &null);
	}*/
}

void GraphicsDeviceD3D11::SetTexture2D(HexEngine::ITexture2D* texture)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	if (texture == nullptr)
	{
		SetPixelShaderResource(nullptr);
		return;
	}
	auto tex = reinterpret_cast<Texture2D*>(texture);

	if (!tex->_shaderResourceView)
	{
		LOG_CRIT("tex->_shaderResourceView cannot be NULL!");
		return;
	}

	SetPixelShaderResource(tex->_shaderResourceView);
}

void GraphicsDeviceD3D11::SetTexture3D(HexEngine::ITexture3D* texture)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	if (texture == nullptr)
	{
		SetPixelShaderResource(nullptr);
		return;
	}
	auto tex = reinterpret_cast<Texture3D*>(texture);

	if (!tex->_shaderResourceView)
	{
		LOG_CRIT("tex->_shaderResourceView cannot be NULL!");
		return;
	}

	SetPixelShaderResource(tex->_shaderResourceView);
}

void GraphicsDeviceD3D11::SetTexture2DArray(uint32_t slot, const std::vector<HexEngine::ITexture2D*>& textures)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	HEX_ASSERT(textures.size() < 32);

	const auto hasOutputHazard = [this, &textures]()
	{
		for (HexEngine::ITexture2D* texture : textures)
		{
			if (!texture)
			{
				continue;
			}

			if (texture == _boundDepthStencil)
			{
				return true;
			}

			for (HexEngine::ITexture2D* boundTarget : _boundRenderTargets)
			{
				if (texture == boundTarget)
				{
					return true;
				}
			}
		}

		return false;
	};

	if (hasOutputHazard())
	{
		// SRV/RTV-DSV overlap is invalid in D3D11; unbind outputs first.
		_deviceContext->OMSetRenderTargets(0, nullptr, nullptr);
		_boundRenderTargets.clear();
		_boundDepthStencil = nullptr;
	}

	ID3D11ShaderResourceView* array[32]; // this is a bad idea, what if there are more than 32 textures??!!

	for (auto i = 0; i < textures.size(); ++i)
	{
		auto tex = reinterpret_cast<Texture2D*>(textures[i]);

		array[i] = tex ? tex->_shaderResourceView : nullptr;
	}
	_deviceContext->PSSetShaderResources(slot, textures.size(), array);

	_currentlyBoundSRVIndex += textures.size();
}

void GraphicsDeviceD3D11::SetTexture2DArray(const std::vector<HexEngine::ITexture2D*>& textures)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	HEX_ASSERT(textures.size() < 32);

	const auto hasOutputHazard = [this, &textures]()
	{
		for (HexEngine::ITexture2D* texture : textures)
		{
			if (!texture)
			{
				continue;
			}

			if (texture == _boundDepthStencil)
			{
				return true;
			}

			for (HexEngine::ITexture2D* boundTarget : _boundRenderTargets)
			{
				if (texture == boundTarget)
				{
					return true;
				}
			}
		}

		return false;
	};

	if (hasOutputHazard())
	{
		// SRV/RTV-DSV overlap is invalid in D3D11; unbind outputs first.
		_deviceContext->OMSetRenderTargets(0, nullptr, nullptr);
		_boundRenderTargets.clear();
		_boundDepthStencil = nullptr;
	}

	ID3D11ShaderResourceView* array[32]; // this is a bad idea, what if there are more than 32 textures??!!

	for (auto i = 0; i < textures.size(); ++i)
	{
		auto tex = reinterpret_cast<Texture2D*>(textures[i]);

		array[i] = tex ? tex->_shaderResourceView : nullptr;
	}
	_deviceContext->PSSetShaderResources(_currentlyBoundSRVIndex, textures.size(), array);

	_currentlyBoundSRVIndex += textures.size();
}

void GraphicsDeviceD3D11::SetRenderTargets(uint32_t numTargets, const std::vector<HexEngine::ITexture2D*>& targets, HexEngine::ITexture2D* depthStencil)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	std::vector<ID3D11RenderTargetView*> rtv;

	for (auto& target : targets)
	{
		ID3D11RenderTargetView* view = nullptr;

		if (target)
		{
			view = ((Texture2D*)target)->_renderTargetView;
		}
		rtv.push_back(view);
	}
	_deviceContext->OMSetRenderTargets(numTargets, rtv.data(), depthStencil ? ((Texture2D*)depthStencil)->_depthStencilView : nullptr);
}

void GraphicsDeviceD3D11::SetRenderTarget(HexEngine::ITexture2D* renderTarget, HexEngine::ITexture2D* depthStencil)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	_boundRenderTargets.clear();
	_boundRenderTargets.push_back(renderTarget);

	_boundDepthStencil = depthStencil;

	ID3D11RenderTargetView* rtv[] = {
		((Texture2D*)renderTarget)->_renderTargetView
	};

	_deviceContext->OMSetRenderTargets(1, rtv, depthStencil ? ((Texture2D*)depthStencil)->_depthStencilView : nullptr);
}

void GraphicsDeviceD3D11::SetRenderTargets(const std::vector<HexEngine::ITexture2D*>& renderTargets, HexEngine::ITexture2D* depthStencil)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	_boundRenderTargets.clear();
	_boundRenderTargets = renderTargets;

	_boundDepthStencil = depthStencil;

	ID3D11RenderTargetView** rtv = new ID3D11RenderTargetView * [renderTargets.size()];

	for (auto i = 0; i < renderTargets.size(); ++i)
	{
		rtv[i] = ((Texture2D*)renderTargets[i])->_renderTargetView;
	}

	_deviceContext->OMSetRenderTargets(renderTargets.size(), rtv, depthStencil ? ((Texture2D*)depthStencil)->_depthStencilView : nullptr);

	delete[] rtv;
}

void GraphicsDeviceD3D11::GetRenderTargets(std::vector<HexEngine::ITexture2D*>& renderTargets, HexEngine::ITexture2D** depthStencil)
{
	renderTargets = _boundRenderTargets;

	if (depthStencil)
		*depthStencil = _boundDepthStencil;
}

uint32_t GraphicsDeviceD3D11::GetBoundResourceIndex()
{
	return _currentlyBoundSRVIndex;
}

void GraphicsDeviceD3D11::SetPixelShaderResource(uint32_t slot, ID3D11ShaderResourceView* resource)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	_deviceContext->PSSetShaderResources(slot, 1, &resource);

	_currentlyBoundSRVIndex = slot + 1;
}

void GraphicsDeviceD3D11::SetPixelShaderResource(ID3D11ShaderResourceView* resource)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	_deviceContext->PSSetShaderResources(_currentlyBoundSRVIndex, 1, &resource);
	++_currentlyBoundSRVIndex;
}

void GraphicsDeviceD3D11::UnbindAllPixelShaderResources()
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	if (_currentlyBoundSRVIndex == 0)
		return;

	if (!_emptyShaderResources || _currentlyBoundSRVIndex > _maxEmptyResources)
	{
		delete[] _emptyShaderResources;
		_emptyShaderResources = new ID3D11ShaderResourceView * [_currentlyBoundSRVIndex] {nullptr};

		_maxEmptyResources = _currentlyBoundSRVIndex;
	}

	_deviceContext->PSSetShaderResources(0, _currentlyBoundSRVIndex, _emptyShaderResources);

	_currentlyBoundSRVIndex = 0;
}

/*void GraphicsDeviceD3D11::BindGBuffer()
{
	_gbuffer.Bind();
}

void GraphicsDeviceD3D11::BindShadowMaps()
{
	if (_isInShadowMapGeneration)
		return;

	for (int32_t i = 0; i < _countof(_shadowMap); ++i)
	{
		_shadowMap[i]->Bind();
	}
}*/

void GraphicsDeviceD3D11::SetPixelShaderResources(uint32_t slot, const std::vector<ID3D11ShaderResourceView*>& resources)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	_deviceContext->PSSetShaderResources(slot, resources.size(), resources.data());

	_currentlyBoundSRVIndex = slot + resources.size();
}

void GraphicsDeviceD3D11::SetPixelShaderResources(const std::vector<ID3D11ShaderResourceView*>& resources)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	_deviceContext->PSSetShaderResources(_currentlyBoundSRVIndex, resources.size(), resources.data());

	_currentlyBoundSRVIndex += resources.size();
}

void GraphicsDeviceD3D11::DrawIndexed(uint32_t numIndices)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	SetConstantBufferVS(1, _engineConstantBuffers[(uint32_t)HexEngine::EngineConstantBuffer::PerObjectBuffer]);
	SetConstantBufferPS(1, _engineConstantBuffers[(uint32_t)HexEngine::EngineConstantBuffer::PerObjectBuffer]);

	_deviceContext->DrawIndexed(numIndices, 0, 0);
	HexEngine::g_pEnv->_graphicsDevice->UnbindAllPixelShaderResources();
}

void GraphicsDeviceD3D11::DrawIndexedInstanced(uint32_t numIndices, uint32_t instanceCount)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	SetConstantBufferVS(1, _engineConstantBuffers[(uint32_t)HexEngine::EngineConstantBuffer::PerObjectBuffer]);
	SetConstantBufferPS(1, _engineConstantBuffers[(uint32_t)HexEngine::EngineConstantBuffer::PerObjectBuffer]);

	_deviceContext->DrawIndexedInstanced(numIndices, instanceCount, 0, 0, 0);
	HexEngine::g_pEnv->_graphicsDevice->UnbindAllPixelShaderResources();
}

void GraphicsDeviceD3D11::Draw(uint32_t vertexCount, int32_t startVertexLocation)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	SetConstantBufferVS(1, _engineConstantBuffers[(uint32_t)HexEngine::EngineConstantBuffer::PerObjectBuffer]);
	SetConstantBufferPS(1, _engineConstantBuffers[(uint32_t)HexEngine::EngineConstantBuffer::PerObjectBuffer]);

	_deviceContext->Draw(vertexCount, startVertexLocation);
	HexEngine::g_pEnv->_graphicsDevice->UnbindAllPixelShaderResources();
}

TextureImporter* GraphicsDeviceD3D11::GetTextureLoader()
{
	return _textureLoader;
}

void GraphicsDeviceD3D11::SetDepthBufferState(HexEngine::DepthBufferState state)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	if (_prevRenderState._depthState == state)
		return;

	switch (state)
	{
	case HexEngine::DepthBufferState::DepthNone:
		_deviceContext->OMSetDepthStencilState(_states->DepthNone(), 1);
		break;

	case HexEngine::DepthBufferState::DepthDefault:
		_deviceContext->OMSetDepthStencilState(_states->DepthDefault(), 1);
		break;

	case HexEngine::DepthBufferState::DepthRead:
		_deviceContext->OMSetDepthStencilState(_states->DepthRead(), 1);
		break;

	case HexEngine::DepthBufferState::DepthReverseZ:
		_deviceContext->OMSetDepthStencilState(_states->DepthReverseZ(), 1);
		break;

	case HexEngine::DepthBufferState::DepthReadReverseZ:
		_deviceContext->OMSetDepthStencilState(_states->DepthReadReverseZ(), 1);
		break;
	}

	_prevRenderState._depthState = state;
}

HexEngine::DepthBufferState GraphicsDeviceD3D11::GetDepthBufferState() const
{
	return _prevRenderState._depthState;
}

void GraphicsDeviceD3D11::SetClearColour(const math::Color& colour)
{
	_clearColour = colour;
}

void GraphicsDeviceD3D11::SetCullingMode(HexEngine::CullingMode mode)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	//if (_prevRenderState._cullMode == mode)
	//	return;

	switch (mode)
	{
	case HexEngine::CullingMode::NoCulling:
		_deviceContext->RSSetState(_states->CullNone());
		break;

	case HexEngine::CullingMode::BackFace:
		_deviceContext->RSSetState(_states->CullClockwise());
		break;

	case HexEngine::CullingMode::FrontFace:
		_deviceContext->RSSetState(_states->CullCounterClockwise());
		break;
	}

	_prevRenderState._cullMode = mode;
}

HexEngine::CullingMode GraphicsDeviceD3D11::GetCullingMode() const
{
	return _prevRenderState._cullMode;
}

void GraphicsDeviceD3D11::ResetState()
{
	_prevRenderState.Reset();
	_deviceContext->ClearState();

	ID3D11SamplerState* samplers[] = { _states->AnisotropicWrap(), _texSamplerComparison, _states->PointWrap(), _texSamplerMirrored, _states->LinearWrap() };
	_deviceContext->PSSetSamplers(0, 5, samplers);
}

void GraphicsDeviceD3D11::BeginFrame(HexEngine::Window* window, HexEngine::ITexture2D* depthBuffer)
{
	auto it = _deviceData.find(window);

	if (it == _deviceData.end())
		return;


	//std::lock_guard<std::recursive_mutex> lock(_lock);

	assert(_currentlyBoundSRVIndex == 0 && "There are unbound SRVs from the previous frame!");

	_currentlyBoundSRVIndex = 0;

	SetDepthBufferState(HexEngine::DepthBufferState::DepthDefault);

	ID3D11SamplerState* samplers[] = { _states->AnisotropicWrap(), _texSamplerComparison, _states->PointWrap(), _texSamplerMirrored, _states->LinearWrap() };
	_deviceContext->PSSetSamplers(0, 5, samplers);

	it->second.backbuffer->ClearRenderTargetView(math::Color(HEX_RGBA_TO_FLOAT4(83, 92, 111, 255)));

	SetRenderTarget(it->second.backbuffer, depthBuffer);
}


void GraphicsDeviceD3D11::EndFrame(HexEngine::Window* window)
{
	auto it = _deviceData.find(window);

	if (it == _deviceData.end())
		return;

	//std::lock_guard<std::recursive_mutex> lock(_lock);

	// Finally present the scene
	//
	if (r_vsync._val.i32 == 0)
	{
		CHECK_HR(it->second.swapchain->Present(0, 0));
	}
	else if (r_vsync._val.i32 == 1)
	{
		CHECK_HR(it->second.swapchain->Present(1, 0));
	}
	else if (r_vsync._val.i32 == 2)
	{
		CHECK_HR(it->second.swapchain->Present(HexEngine::g_pEnv->_timeManager->_frameCount % 2 == 0, 0));
	}
	UnbindAllPixelShaderResources();
}

void GraphicsDeviceD3D11::SetViewports(const std::vector<D3D11_VIEWPORT>& viewports)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	_deviceContext->RSSetViewports(viewports.size(), viewports.data());

	std::vector<RECT> rects;
	for (auto& vp : viewports)
	{
		rects.push_back({ (long)vp.TopLeftX, (long)vp.TopLeftY, (long)vp.TopLeftX + (long)vp.Width, (long)vp.TopLeftY + (long)vp.Height });
	}
	SetScissorRects(rects);
}

void GraphicsDeviceD3D11::SetViewport(const D3D11_VIEWPORT& viewport)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	_deviceContext->RSSetViewports(1, &viewport);
}

const D3D11_VIEWPORT& GraphicsDeviceD3D11::GetBackBufferViewport() const
{
	return _bbufferViewport;
}

void GraphicsDeviceD3D11::SetBlendState(HexEngine::BlendState state)
{
	std::lock_guard<std::recursive_mutex> lock(_lock);

	//if (_prevRenderState._blendState == state)
	//	return;

	float blend[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

	switch (state)
	{
	case HexEngine::BlendState::Opaque:
		_deviceContext->OMSetBlendState(_states->Opaque(), blend, 0xFFFFFFFF);
		break;

	case HexEngine::BlendState::Additive:
		_deviceContext->OMSetBlendState(_states->Additive(), blend, 0xFFFFFFFF);
		break;

	case HexEngine::BlendState::Subtractive:
		_deviceContext->OMSetBlendState(_subtractivetBlendState, blend, 0xFFFFFFFF);
		break;

	case HexEngine::BlendState::Transparency:
		_deviceContext->OMSetBlendState(_states->NonPremultiplied(), blend, 0xFFFFFFFF);
		break;
	}

	_prevRenderState._blendState = state;
}

HexEngine::BlendState GraphicsDeviceD3D11::GetBlendState() const
{
	return _prevRenderState._blendState;
}

int32_t GraphicsDeviceD3D11::GetCurrentMSAALevel() const
{
	return MsaaLevel;
}

void GraphicsDeviceD3D11::Lock()
{
	_lock.lock();
}

void GraphicsDeviceD3D11::Unlock()
{
	_lock.unlock();
}

void GraphicsDeviceD3D11::SetScissorRect(const RECT& rect)
{
	_deviceContext->RSSetScissorRects(1, &rect);
}

void GraphicsDeviceD3D11::SetScissorRects(const std::vector<RECT>& rects)
{
	_deviceContext->RSSetScissorRects(rects.size(), rects.data());
}

void GraphicsDeviceD3D11::ClearScissorRect()
{
	const auto& vp = _bbufferViewport;

	SetScissorRect({ (long)vp.TopLeftX, (long)vp.TopLeftY, (long)vp.TopLeftX + (long)vp.Width, (long)vp.TopLeftY + (long)vp.Height });
}
