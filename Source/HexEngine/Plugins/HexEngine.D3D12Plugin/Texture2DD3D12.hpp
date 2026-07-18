#pragma once

#include <HexEngine.Core/Graphics/ITexture2D.hpp>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

/**
 * @brief D3D12 backing for HexEngine::ITexture2D.
 *
 * Doubles as the backbuffer wrapper (created in AttachToWindow) and as the
 * general-purpose 2D texture (created from a TextureDesc).
 *
 * Each view handle is a CPU-only descriptor allocated from the device's
 * per-type DescriptorHeapAllocator. The integer index alongside lets us
 * Free() the slot at Destroy time. UINT32_MAX = "no view of this type".
 *
 * `_currentState` tracks the last-issued ResourceBarrier state so the device
 * can insert transitions automatically without each caller threading the
 * state through every API surface. Backbuffers start in PRESENT; general
 * textures start at whatever InitialStateFromBindFlags picked.
 */
class Texture2DD3D12 : public HexEngine::ITexture2D
{
public:
	virtual ~Texture2DD3D12() override;

	virtual int32_t GetWidth()  override { return _width; }
	virtual int32_t GetHeight() override { return _height; }

	virtual void  Destroy() override;
	virtual void* GetNativePtr() override { return _resource.Get(); }
	virtual uint32_t GetFormat() override { return (uint32_t)_format; }

	// Pixel upload via temporary upload-heap stage + CopyTextureRegion on the
	// device's open command list.
	virtual void SetPixels(uint8_t* data, uint32_t size) override;

	// Deferred readback: forwards to GraphicsDeviceD3D12::RequestTextureCapture,
	// which executes at the next frame boundary (see that method's doc).
	virtual void SaveToFile(const fs::path& path)                                           override;
	virtual void ClearDepth(uint32_t)                                                       override;
	virtual void CopyTo(ITexture2D*)                                                        override;
	virtual void CopyTo(ITexture2D*, const RECT&, const RECT&)                              override {}
	virtual void BlendTo_Additive(ITexture2D*, HexEngine::IShader*)                         override {}
	virtual void BlendTo_Additive_Double(ITexture2D*, HexEngine::IShader*)                  override {}
	virtual void BlendTo_Alpha(ITexture2D*, HexEngine::IShader*)                            override {}
	virtual void BlendTo_NonPremultiplied(ITexture2D*, HexEngine::IShader*)                 override {}
	virtual void GetPixels(std::vector<uint8_t>&)                                           override {}
	virtual void GetPixels(std::vector<float>&)                                             override {}
	virtual void* GetSharedHandle()                                                         override { return nullptr; }
	virtual void* LockPixels(int32_t*)                                                      override { return nullptr; }
	virtual void  UnlockPixels()                                                            override {}

	virtual void ClearRenderTargetView(const math::Color& colour) override;

	virtual void SetDebugName(const std::string& name) override
	{
#ifdef _DEBUG
		if (_resource)
		{
			std::wstring wname(name.begin(), name.end());
			_resource->SetName(wname.c_str());
		}
#endif
	}

public:
	Microsoft::WRL::ComPtr<ID3D12Resource> _resource;
	DXGI_FORMAT                             _format = DXGI_FORMAT_UNKNOWN;

	D3D12_CPU_DESCRIPTOR_HANDLE             _rtv = {};
	D3D12_CPU_DESCRIPTOR_HANDLE             _dsv = {};
	D3D12_CPU_DESCRIPTOR_HANDLE             _srv = {};
	D3D12_CPU_DESCRIPTOR_HANDLE             _uav = {};
	uint32_t                                _rtvIndex = UINT32_MAX;
	uint32_t                                _dsvIndex = UINT32_MAX;
	uint32_t                                _srvIndex = UINT32_MAX;
	uint32_t                                _uavIndex = UINT32_MAX;

	D3D12_RESOURCE_STATES                   _currentState = D3D12_RESOURCE_STATE_COMMON;
	int32_t                                 _width  = 0;
	int32_t                                 _height = 0;
	int32_t                                 _arraySize  = 1;
	int32_t                                 _sampleCount = 1;
	int32_t                                 _mipLevels   = 1;
	// Backbuffer instances opt out of releasing the underlying resource - the
	// swap chain owns it. Regular textures own theirs.
	bool                                    _ownsResource = true;
};
