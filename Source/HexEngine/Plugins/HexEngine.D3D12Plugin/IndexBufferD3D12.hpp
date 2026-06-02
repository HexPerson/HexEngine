
#pragma once

#include <HexEngine.Core/Graphics/IIndexBuffer.hpp>
#include <d3d12.h>
#include <wrl/client.h>

/** @brief D3D12 index buffer. Same upload-heap design as VertexBufferD3D12. */
class IndexBufferD3D12 : public HexEngine::IIndexBuffer
{
public:
	virtual ~IndexBufferD3D12() override { Destroy(); }

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

	virtual void SetIndexData(uint8_t* data, uint32_t size, uint32_t offset = 0) override
	{
		if (_mapped == nullptr || data == nullptr || size == 0) return;
		if (offset + size > _byteSize) return;
		memcpy(static_cast<uint8_t*>(_mapped) + offset, data, size);
	}

public:
	Microsoft::WRL::ComPtr<ID3D12Resource> _resource;
	D3D12_INDEX_BUFFER_VIEW                 _view = {};
	void*                                    _mapped = nullptr;
	uint32_t                                 _stride = 0; ///< 2 for 16-bit, 4 for 32-bit
	uint32_t                                 _byteSize = 0;
};
