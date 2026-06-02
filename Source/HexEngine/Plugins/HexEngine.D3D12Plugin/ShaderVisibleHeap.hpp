
#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>

/**
 * @brief Per-frame GPU-visible CBV/SRV/UAV descriptor heap.
 *
 * Phase B4 takes a "bump per frame, drain at end of frame" approach for
 * descriptor uploads. At draw time the device copies the CPU-side descriptors
 * for the currently-bound resources into a contiguous range here, then binds
 * the GPU handle as a root descriptor table.
 *
 * Bump pointer is per-frame and reset each EndFrame (after the frame's fence
 * is signalled, so no in-flight descriptors get clobbered).
 *
 * Capacity is generous - HexEngine's scene init binds maybe a few thousand
 * descriptors total; per-frame draw bindings are an order of magnitude less.
 * Bump if you ever see "shader-visible heap exhausted" warnings.
 */
class ShaderVisibleHeap
{
public:
	static constexpr uint32_t kCapacity = 1u << 16; // 65536 descriptors per frame

	ShaderVisibleHeap() = default;
	~ShaderVisibleHeap() = default;

	bool Create(ID3D12Device* device);
	void Destroy();

	/**
	 * @brief Bumps `count` consecutive slots and returns their CPU + GPU base.
	 *
	 * Caller is responsible for filling them via CopyDescriptors before binding.
	 * Returns false if the heap is exhausted for this frame.
	 */
	bool Allocate(uint32_t count, D3D12_CPU_DESCRIPTOR_HANDLE& outCpu, D3D12_GPU_DESCRIPTOR_HANDLE& outGpu);

	/** @brief Resets the bump pointer for a new frame. */
	void BeginFrame() { _bump = 0; }

	ID3D12DescriptorHeap* GetHeap() const { return _heap.Get(); }
	uint32_t              GetDescriptorSize() const { return _descriptorSize; }

private:
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _heap;
	D3D12_CPU_DESCRIPTOR_HANDLE                  _cpuStart = {};
	D3D12_GPU_DESCRIPTOR_HANDLE                  _gpuStart = {};
	uint32_t                                     _descriptorSize = 0;
	uint32_t                                     _bump = 0;
	bool                                         _warnedExhaustion = false;
};
