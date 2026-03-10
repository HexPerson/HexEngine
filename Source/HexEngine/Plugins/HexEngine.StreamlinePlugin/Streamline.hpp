
#pragma once

#include <HexEngine.Core\HexEngine.hpp>

#include <sl_helpers.h>


// These are the exports from SL library
typedef HRESULT(WINAPI* PFunCreateDXGIFactory)(REFIID, void**);
typedef HRESULT(WINAPI* PFunCreateDXGIFactory1)(REFIID, void**);
typedef HRESULT(WINAPI* PFunCreateDXGIFactory2)(UINT, REFIID, void**);
typedef HRESULT(WINAPI* PFunDXGIGetDebugInterface1)(UINT, REFIID, void**);
typedef HRESULT(WINAPI* PFunD3D11CreateDevice)(
	IDXGIAdapter* pAdapter,
	D3D_DRIVER_TYPE DriverType,
	HMODULE Software,
	UINT Flags,
	const D3D_FEATURE_LEVEL* pFeatureLevels,
	UINT FeatureLevels,
	UINT SDKVersion,
	ID3D11Device** ppDevice,
	D3D_FEATURE_LEVEL* pFeatureLevel,
	ID3D11DeviceContext** ppImmediateContext);

class Streamline : public HexEngine::IStreamlineProvider
{
public:
	virtual bool Create() override;

	virtual void Destroy() override;

	virtual bool IsEnabled() override { return _enabled; }

	virtual HRESULT D3D11CreateDevice(
		IDXGIAdapter* pAdapter,
		D3D_DRIVER_TYPE DriverType,
		HMODULE Software,
		UINT Flags,
		const D3D_FEATURE_LEVEL* pFeatureLevels,
		UINT FeatureLevels,
		UINT SDKVersion,
		ID3D11Device** ppDevice,
		D3D_FEATURE_LEVEL* pFeatureLevel,
		ID3D11DeviceContext** ppImmediateContext) override;

	virtual HRESULT CreateDXGIFactory1(REFIID riid, _COM_Outptr_ void** ppFactory) override;

	virtual bool QueryOptimalDLSSSettings(
		int32_t desiredWidth,
		int32_t desiredHeight,
		HexEngine::DLSSMode mode,
		int32_t& optimalWidth,
		int32_t& optimalHeight) override;

	virtual uint32_t GetSupportedFeaturesMask() override;

	virtual void SetDLSSOptions(float sharpness, bool hdr, bool autoExposure, HexEngine::DLSSMode mode, int32_t outputWidth, int32_t outputHeight) override;

	virtual void PrepareFrameResources(void* colourIn, void* colourOut, void* motionVectors, void* depth, void* cmdList) override;

	virtual void SetCommonConstants(const HexEngine::StreamlineConstants& constants) override;

	virtual void BeginFrame() override;

	virtual void EndFrame() override {}

	virtual void EvaluateFeature(HexEngine::StreamlineFeature feature, void* cmdList) override;

private:
	struct ImportedFuncs
	{
		PFunCreateDXGIFactory _createFactory;
		PFunCreateDXGIFactory1 _createFactory1;
		PFunCreateDXGIFactory2 _createFactory2;
		PFunDXGIGetDebugInterface1 _getDebugInterface1;
		PFunD3D11CreateDevice _d3d11CreateDevice;
	} _importedFuncs;

	bool _enabled = false;
	sl::ViewportHandle _viewport = { 0 };
	uint32_t _supportedFeatures = 0;
	sl::DLSSOptions _dlssOptions;
	sl::Extent _renderExtent;
	sl::Extent _outputExtent;
	sl::FrameToken* _frameToken = nullptr;
};
