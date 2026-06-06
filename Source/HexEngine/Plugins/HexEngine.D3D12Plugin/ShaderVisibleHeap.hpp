
#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>

/**
 * @brief Per-frame GPU-visible CBV/SRV/UAV descriptor heap.
 *
 * Partitioned into `kFrameCount` equal sections. Each frame writes
 * descriptors into the section indexed by its swap-chain slot, and bumps
 * only within that section. BeginFrame(slot) resets the bump for the
 * SECTION the new frame is about to use - other sections keep their
 * contents intact so in-flight frames N-1 / N-2 keep reading what they
 * recorded.
 *
 * The earlier "reset _bump to 0 every BeginFrame" design caused a classic
 * descriptor-table race: the GPU reads descriptors at execution time, not
 * at command-record time, so when the CPU re-wrote slot 0 for frame N+1,
 * frame N's still-queued draws ended up reading frame N+1's data. Visible
 * as "various elements vanish depending on hover state" because the
 * specific descriptors that got clobbered depended on per-frame draw count.
 */
class ShaderVisibleHeap
{
public:
	static constexpr uint32_t kFrameCount       = 3;
	// 524288 total ≈ 174720 per section ≈ 1820 draws per frame, comfortably
	// covering the editor's hover-heavy frames. D3D12 hard-caps shader-visible
	// CBV/SRV/UAV heaps at 1,000,000 descriptors so we have headroom to grow.
	// At ~32 bytes per descriptor the total heap memory is ~16 MB.
	static constexpr uint32_t kCapacity         = 1u << 19;                  // 524288
	static constexpr uint32_t kPerFrameUnaligned = kCapacity / kFrameCount;
	// Each FlushGraphics burns 96 descriptors (16 CBV + 64 SRV + 16 UAV).
	// Align the per-frame section down to a 96-multiple so a single draw's
	// allocation can't straddle the section boundary.
	static constexpr uint32_t kPerFrame         = (kPerFrameUnaligned / 96) * 96;

	ShaderVisibleHeap() = default;
	~ShaderVisibleHeap() = default;

	bool Create(ID3D12Device* device);
	void Destroy();

	/** Bumps `count` consecutive slots inside the current frame's section.
	 *  Returns false if the current section is exhausted. */
	bool Allocate(uint32_t count, D3D12_CPU_DESCRIPTOR_HANDLE& outCpu, D3D12_GPU_DESCRIPTOR_HANDLE& outGpu);

	/** Switches to the section for the frame about to be recorded and resets
	 *  ONLY that section's bump pointer. Caller is responsible for having
	 *  fence-waited that frame's old work so this section is safe to overwrite. */
	void BeginFrame(uint32_t frameSlot);

	ID3D12DescriptorHeap* GetHeap() const { return _heap.Get(); }
	uint32_t              GetDescriptorSize() const { return _descriptorSize; }

private:
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _heap;
	D3D12_CPU_DESCRIPTOR_HANDLE                  _cpuStart = {};
	D3D12_GPU_DESCRIPTOR_HANDLE                  _gpuStart = {};
	uint32_t                                     _descriptorSize  = 0;
	uint32_t                                     _currentSection  = 0;   // 0 .. kFrameCount-1
	uint32_t                                     _bumpInSection   = 0;   // 0 .. kPerFrame
	bool                                         _warnedExhaustion = false;
};
