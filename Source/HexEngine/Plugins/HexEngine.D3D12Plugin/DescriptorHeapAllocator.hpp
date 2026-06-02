
#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <vector>
#include <cstdint>

/**
 * @brief CPU-only fixed-capacity descriptor heap with a free-list.
 *
 * Used for the per-resource descriptor slots that GraphicsDeviceD3D12 hands
 * out via Texture2DD3D12::_rtv, ::_dsv, ::_srv etc. The slots produced here
 * are NOT shader-visible - B4 will add a separate GPU-visible heap and copy
 * descriptors from these CPU heaps into it at draw time.
 *
 * Allocation is O(1): a free-list of indices backs the heap. Releasing a
 * slot pushes its index back. No defragmentation; the heap is fixed-size
 * by design (descriptor heaps can't grow in-place, and growing requires
 * recreating every view that lives in them).
 *
 * Capacity defaults are sized to roughly what HexEngine creates per scene:
 * a few hundred RTV/DSV, a few thousand SRV/UAV/CBV.
 */
class DescriptorHeapAllocator
{
public:
	DescriptorHeapAllocator() = default;
	~DescriptorHeapAllocator() = default;

	bool Create(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t capacity, bool shaderVisible);
	void Destroy();

	/**
	 * @brief Returns the CPU handle of a freshly allocated slot.
	 * Writes `outIndex` so the caller can hand it back to Free().
	 */
	D3D12_CPU_DESCRIPTOR_HANDLE Allocate(uint32_t& outIndex);
	void                        Free(uint32_t index);

	D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle(uint32_t index) const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(uint32_t index) const; ///< only valid if shader-visible

	ID3D12DescriptorHeap*       GetHeap()         const { return _heap.Get(); }
	uint32_t                    GetCapacity()     const { return _capacity; }
	uint32_t                    GetDescriptorSize() const { return _descriptorSize; }
	bool                        IsShaderVisible()  const { return _shaderVisible; }

private:
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _heap;
	D3D12_CPU_DESCRIPTOR_HANDLE                  _cpuStart      = {};
	D3D12_GPU_DESCRIPTOR_HANDLE                  _gpuStart      = {};
	uint32_t                                     _capacity      = 0;
	uint32_t                                     _descriptorSize = 0;
	uint32_t                                     _nextLinear    = 0; ///< first index never allocated yet
	std::vector<uint32_t>                        _freeList;          ///< indices Free()'d back to us
	bool                                         _shaderVisible = false;
};
