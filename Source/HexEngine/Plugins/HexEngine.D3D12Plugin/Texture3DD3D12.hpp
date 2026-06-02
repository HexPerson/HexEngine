
#pragma once

#include <HexEngine.Core/Graphics/ITexture3D.hpp>
#include <d3d12.h>
#include <wrl/client.h>

class Texture3DD3D12 : public HexEngine::ITexture3D
{
public:
	virtual ~Texture3DD3D12() override;

	virtual int32_t GetWidth()  override { return _width;  }
	virtual int32_t GetHeight() override { return _height; }
	virtual int32_t GetDepth()  override { return _depth;  }

	virtual void  Destroy() override;
	virtual void* GetNativePtr() override { return _resource.Get(); }
	virtual uint32_t GetFormat() override { return (uint32_t)_format; }

	virtual void SetPixels(uint8_t* data, uint32_t size, int32_t slice = 0) override;
	virtual void SaveToFile(const fs::path&)                                  override {}
	virtual void ClearDepth(uint32_t)                                         override {}
	virtual void CopyTo(ITexture3D*)                                          override {}
	virtual void ClearRenderTargetView(const math::Color& colour)             override;
	virtual void GetPixels(std::vector<uint8_t>&)                             override {}
	virtual void GetPixels(std::vector<float>&)                               override {}
	virtual bool SupportsRandomWrite() const                                  override { return _uavIndex != UINT32_MAX; }
	virtual void* GetSharedHandle()                                           override { return nullptr; }
	virtual void* LockPixels(int32_t*)                                        override { return nullptr; }
	virtual void  UnlockPixels()                                              override {}

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
	D3D12_CPU_DESCRIPTOR_HANDLE             _srv = {};
	D3D12_CPU_DESCRIPTOR_HANDLE             _uav = {};
	uint32_t                                _rtvIndex = UINT32_MAX;
	uint32_t                                _srvIndex = UINT32_MAX;
	uint32_t                                _uavIndex = UINT32_MAX;
	D3D12_RESOURCE_STATES                   _currentState = D3D12_RESOURCE_STATE_COMMON;
	int32_t                                 _width  = 0;
	int32_t                                 _height = 0;
	int32_t                                 _depth  = 0;
	int32_t                                 _mipLevels = 1;
};
