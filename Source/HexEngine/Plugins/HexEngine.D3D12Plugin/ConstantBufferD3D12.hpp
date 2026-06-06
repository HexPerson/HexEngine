
#pragma once

#include <HexEngine.Core/Graphics/IConstantBuffer.hpp>
#include <d3d12.h>
#include <wrl/client.h>
#include <vector>

class GraphicsDeviceD3D12;

/**
 * @brief D3D12 constant buffer with a per-buffer growable upload pool.
 *
 * Same problem as VertexBufferD3D12 / IndexBufferD3D12: a single underlying
 * resource gets rewritten many times per frame and the GPU reads garbage.
 * Each Write picks a free upload-heap entry (or allocates a new one) and
 * recreates the CBV in place to point at it - the CBV lives in a CPU-only
 * heap so updating it doesn't disturb already-submitted shader-visible
 * heap copies.
 */
class ConstantBufferD3D12 : public HexEngine::IConstantBuffer
{
public:
	virtual ~ConstantBufferD3D12() { Destroy(); }

	virtual void  Destroy() override;
	virtual void* GetNativePtr() override { return _activeResource; }

	virtual bool Write(void* data, uint32_t size) override;

public:
	struct UploadEntry
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		void*    mapped = nullptr;
		uint64_t fence  = 0;
	};

	std::vector<UploadEntry>                _uploads;
	ID3D12Resource*                          _activeResource = nullptr;
	D3D12_CPU_DESCRIPTOR_HANDLE             _cbv = {};
	uint32_t                                 _cbvIndex = UINT32_MAX;
	uint32_t                                 _logicalSize = 0;
	uint32_t                                 _byteSize    = 0;     ///< 256-byte aligned size of each upload entry
	GraphicsDeviceD3D12*                     _device      = nullptr;
};
