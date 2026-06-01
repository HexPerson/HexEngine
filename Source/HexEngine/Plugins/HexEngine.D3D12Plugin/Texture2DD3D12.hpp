#pragma once

#include <HexEngine.Core/Graphics/ITexture2D.hpp>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

// Phase B2/B3: D3D12 backing for HexEngine::ITexture2D.
//
// The class doubles as the engine's backbuffer wrapper - GraphicsDeviceD3D12
// creates one of these per swap-chain frame, pointing at the swap chain's
// backbuffer ID3D12Resource. Phase B3 will extend it to back regular
// textures too.
//
// `_currentState` tracks the resource's last-issued ResourceBarrier state so
// the device can insert transitions automatically without each caller having
// to thread the state through every API surface. Backbuffers start in
// D3D12_RESOURCE_STATE_PRESENT immediately after swap-chain creation.
class Texture2DD3D12 : public HexEngine::ITexture2D
{
public:
	virtual ~Texture2DD3D12() override = default;

	virtual int32_t GetWidth()  override { return _width; }
	virtual int32_t GetHeight() override { return _height; }

	virtual void  Destroy()                                    override { _resource.Reset(); }
	virtual void* GetNativePtr()                               override { return _resource.Get(); }
	virtual uint32_t GetFormat()                               override { return (uint32_t)_format; }

	// The following are unused in B2 (no rendering happens beyond the clear);
	// B3 fills them in with real implementations.
	virtual void SetPixels(uint8_t*, uint32_t)                                              override {}
	virtual void SaveToFile(const fs::path&)                                                override {}
	virtual void ClearDepth(uint32_t)                                                       override {}
	virtual void CopyTo(ITexture2D*)                                                        override {}
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

	// The actual clear runs on the device's command list because clears are
	// command-list operations in D3D12 (not resource-level). GraphicsDeviceD3D12
	// implements this by transitioning to RENDER_TARGET if needed and issuing
	// ClearRenderTargetView on the bound RTV.
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
	D3D12_CPU_DESCRIPTOR_HANDLE             _rtv = {};
	DXGI_FORMAT                             _format = DXGI_FORMAT_UNKNOWN;
	D3D12_RESOURCE_STATES                   _currentState = D3D12_RESOURCE_STATE_COMMON;
	int32_t                                 _width = 0;
	int32_t                                 _height = 0;
	// Backbuffer instances opt out of releasing the underlying resource - the
	// swap chain owns it. Regular textures (B3+) own theirs.
	bool                                    _ownsResource = true;
};
