
#pragma once

#include <HexEngine.Core/Graphics/IIndexBuffer.hpp>
#include <d3d12.h>
#include <wrl/client.h>
#include <vector>

class GraphicsDeviceD3D12;

/** @brief D3D12 index buffer. Same per-buffer growable upload pool as
 *  VertexBufferD3D12 - see that header for the why. */
class IndexBufferD3D12 : public HexEngine::IIndexBuffer
{
public:
	virtual ~IndexBufferD3D12() override { Destroy(); }

	virtual void  Destroy() override;
	virtual void* GetNativePtr() override { return _activeResource; }

	virtual void SetIndexData(uint8_t* data, uint32_t size, uint32_t offset = 0) override;

public:
	struct UploadEntry
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		void*    mapped = nullptr;
		uint64_t fence  = 0;
	};

	std::vector<UploadEntry>                _uploads;
	ID3D12Resource*                          _activeResource = nullptr;
	D3D12_INDEX_BUFFER_VIEW                 _view = {};
	uint32_t                                 _stride = 0;
	uint32_t                                 _byteSize = 0;
	GraphicsDeviceD3D12*                     _device = nullptr;
};
