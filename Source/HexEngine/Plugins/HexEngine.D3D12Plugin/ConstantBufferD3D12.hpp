
#pragma once

#include <HexEngine.Core/Graphics/IConstantBuffer.hpp>
#include <d3d12.h>
#include <wrl/client.h>

/**
 * @brief D3D12 constant buffer.
 *
 * Upload-heap, persistent-mapped. D3D12 requires CB size to be a 256-byte
 * multiple - rounded up at creation time. _byteSize is the rounded size;
 * _logicalSize is what the caller asked for (used to clamp Write()).
 *
 * Single CB instance per game-side IConstantBuffer for now. A frame-rotating
 * upload ring would be cheaper for per-frame updates but adds complexity;
 * B5 can add one if profiling justifies it.
 */
class ConstantBufferD3D12 : public HexEngine::IConstantBuffer
{
public:
	// IConstantBuffer (and INativeGraphicsResource) doesn't declare a virtual
	// destructor, so `override` here would fail. Plain virtual dtor + manual
	// Destroy() call is enough since the COM ComPtr also cleans up if Destroy
	// wasn't reached.
	virtual ~ConstantBufferD3D12() { Destroy(); }

	virtual void  Destroy() override
	{
		if (_resource && _mapped)
		{
			_resource->Unmap(0, nullptr);
			_mapped = nullptr;
		}
		_resource.Reset();
		_cbvIndex = UINT32_MAX;
	}

	virtual void* GetNativePtr() override { return _resource.Get(); }

	virtual bool Write(void* data, uint32_t size) override
	{
		if (_mapped == nullptr || data == nullptr || size == 0)
			return false;
		// Clamp to what the caller requested (the buffer may be bigger due to
		// the 256-byte rounding).
		const uint32_t toCopy = size > _logicalSize ? _logicalSize : size;
		memcpy(_mapped, data, toCopy);
		return true;
	}

public:
	Microsoft::WRL::ComPtr<ID3D12Resource> _resource;
	D3D12_CPU_DESCRIPTOR_HANDLE             _cbv = {};
	uint32_t                                 _cbvIndex = UINT32_MAX; ///< slot in the device's CBV/SRV/UAV heap
	void*                                    _mapped = nullptr;
	uint32_t                                 _logicalSize = 0;
	uint32_t                                 _byteSize    = 0;     ///< rounded to 256
};
