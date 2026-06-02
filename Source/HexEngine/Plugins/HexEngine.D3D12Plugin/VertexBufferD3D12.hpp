
#pragma once

#include <HexEngine.Core/Graphics/IVertexBuffer.hpp>
#include <d3d12.h>
#include <wrl/client.h>

/**
 * @brief D3D12 vertex buffer (upload-heap-backed, persistent-mapped).
 *
 * Simple and works for both static and per-frame-updated buffers without a
 * separate copy queue / staging buffer. Trade-off vs default-heap: upload
 * memory is system RAM accessed by the GPU over PCIe. B5 can promote large
 * static VBs to a default-heap copy if profiling shows it matters.
 */
class VertexBufferD3D12 : public HexEngine::IVertexBuffer
{
public:
	virtual ~VertexBufferD3D12() override { Destroy(); }

	virtual void  Destroy() override
	{
		if (_resource && _mapped)
		{
			_resource->Unmap(0, nullptr);
			_mapped = nullptr;
		}
		_resource.Reset();
	}

	virtual void* GetNativePtr() override { return _resource.Get(); }
	virtual uint32_t GetStride() override { return _stride; }

	virtual void SetVertexData(uint8_t* data, uint32_t size, uint32_t offset = 0) override
	{
		if (_mapped == nullptr || data == nullptr || size == 0) return;
		if (offset + size > _byteSize) return;
		memcpy(static_cast<uint8_t*>(_mapped) + offset, data, size);
	}

public:
	Microsoft::WRL::ComPtr<ID3D12Resource> _resource;
	D3D12_VERTEX_BUFFER_VIEW                _view = {};
	void*                                    _mapped = nullptr;
	uint32_t                                 _stride = 0;
	uint32_t                                 _byteSize = 0;
};
