
#include "ShaderVisibleHeap.hpp"
#include <HexEngine.Core/HexEngine.hpp>

bool ShaderVisibleHeap::Create(ID3D12Device* device)
{
	if (device == nullptr)
		return false;

	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	desc.NumDescriptors = kCapacity;
	desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	desc.NodeMask       = 0;

	if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&_heap))))
	{
		LOG_CRIT("ShaderVisibleHeap::Create CreateDescriptorHeap(SHADER_VISIBLE) failed");
		return false;
	}
	_descriptorSize  = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	_cpuStart        = _heap->GetCPUDescriptorHandleForHeapStart();
	_gpuStart        = _heap->GetGPUDescriptorHandleForHeapStart();
	_currentSection  = 0;
	_bumpInSection   = 0;
	return true;
}

void ShaderVisibleHeap::Destroy()
{
	_heap.Reset();
	_cpuStart        = {};
	_gpuStart        = {};
	_descriptorSize  = 0;
	_currentSection  = 0;
	_bumpInSection   = 0;
}

void ShaderVisibleHeap::BeginFrame(uint32_t frameSlot)
{
	_currentSection = frameSlot % kFrameCount;
	_bumpInSection  = 0;
}

bool ShaderVisibleHeap::Allocate(uint32_t count, D3D12_CPU_DESCRIPTOR_HANDLE& outCpu, D3D12_GPU_DESCRIPTOR_HANDLE& outGpu)
{
	if (count == 0)
		return false;
	if (_bumpInSection + count > kPerFrame)
	{
		if (!_warnedExhaustion)
		{
			LOG_CRIT("ShaderVisibleHeap section exhausted within a single frame (section=%u, bump=%u, want=%u, per-frame cap=%u). Bumping kCapacity is the right fix.",
				_currentSection, _bumpInSection, count, kPerFrame);
			_warnedExhaustion = true;
		}
		return false;
	}
	const uint32_t absoluteIndex = _currentSection * kPerFrame + _bumpInSection;
	outCpu = _cpuStart;
	outCpu.ptr += static_cast<SIZE_T>(absoluteIndex) * _descriptorSize;
	outGpu = _gpuStart;
	outGpu.ptr += static_cast<UINT64>(absoluteIndex) * _descriptorSize;
	_bumpInSection += count;
	return true;
}
