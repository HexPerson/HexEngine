
#pragma once

#include <HexEngine.Core/Graphics/IVertexBuffer.hpp>
#include <d3d12.h>
#include <wrl/client.h>
#include <vector>

class GraphicsDeviceD3D12;

/**
 * @brief D3D12 vertex buffer with a per-buffer growable upload pool.
 *
 * Problem: D3D11's MAP_WRITE_DISCARD silently renames the underlying storage
 * on every Map() so the GPU's queued read of the old data is unaffected by
 * the CPU's new write. D3D12 has no equivalent. HexEngine's GuiRenderer
 * shares ONE 4-vertex VB and rewrites it for every UI quad via SetVertexData
 * - hundreds of writes per frame. A single underlying resource means each
 * write trashes the data the GPU is still about to read from the previous
 * draw. Visual result: most quads degenerate, only a handful happen to
 * survive.
 *
 * Fix: each SetVertexData picks an upload-heap sub-resource whose last-write
 * fence has already completed (i.e. GPU is done with it), or allocates a new
 * one if none are free. The view's BufferLocation rotates to whichever
 * sub-resource we just wrote, so the in-flight draws keep their own
 * snapshots.
 *
 * Static buffers (written once at init, never updated) keep exactly one
 * sub-resource forever. Dynamic buffers grow on demand to whatever depth
 * the worst-case-per-frame write count requires. Sub-resources are sized
 * to the requested _byteSize, so a 4-vertex VB grows to N tiny 4-vertex
 * allocations rather than one giant ring.
 */
class VertexBufferD3D12 : public HexEngine::IVertexBuffer
{
public:
	virtual ~VertexBufferD3D12() override { Destroy(); }

	virtual void  Destroy() override;
	virtual void* GetNativePtr() override { return _activeResource; }
	virtual uint32_t GetStride() override { return _stride; }

	virtual void SetVertexData(uint8_t* data, uint32_t size, uint32_t offset = 0) override;

public:
	struct UploadEntry
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		void*    mapped = nullptr;
		uint64_t fence  = 0; ///< value of the EndFrame signal after this entry was last written; reuse once GPU completes it
	};

	std::vector<UploadEntry>                _uploads;
	ID3D12Resource*                          _activeResource = nullptr; ///< raw ptr to the entry whose BufferLocation the view currently points at
	D3D12_VERTEX_BUFFER_VIEW                 _view = {};
	uint32_t                                 _stride = 0;
	uint32_t                                 _byteSize = 0;
	GraphicsDeviceD3D12*                     _device = nullptr;
	// B5 visual-parity diagnostics: extent of the LAST SetVertexData write into
	// the CURRENTLY-VIEWED entry ([offset, offset+size)). A draw that fetches
	// beyond this on a rotated (fresh, zero-filled) entry reads zeroed
	// vertices/instance rows - the "fans to screen centre" candidate. Creation
	// with initialData counts as a full write; creation without data leaves 0.
	uint32_t                                 _lastWriteBegin = 0;
	uint32_t                                 _lastWriteEnd   = 0;
};
