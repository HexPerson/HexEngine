
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
	_descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	_cpuStart       = _heap->GetCPUDescriptorHandleForHeapStart();
	_gpuStart       = _heap->GetGPUDescriptorHandleForHeapStart();
	_bump           = 0;
	return true;
}

void ShaderVisibleHeap::Destroy()
{
	_heap.Reset();
	_cpuStart = {};
	_gpuStart = {};
	_descriptorSize = 0;
	_bump = 0;
}

bool ShaderVisibleHeap::Allocate(uint32_t count, D3D12_CPU_DESCRIPTOR_HANDLE& outCpu, D3D12_GPU_DESCRIPTOR_HANDLE& outGpu)
{
	if (count == 0)
		return false;
	if (_bump + count > kCapacity)
	{
		if (!_warnedExhaustion)
		{
			LOG_CRIT("ShaderVisibleHeap exhausted within a single frame (bump=%u, want=%u, cap=%u). Bumping kCapacity is the right fix.", _bump, count, kCapacity);
			_warnedExhaustion = true;
		}
		return false;
	}
	outCpu = _cpuStart;
	outCpu.ptr += static_cast<SIZE_T>(_bump) * _descriptorSize;
	outGpu = _gpuStart;
	outGpu.ptr += static_cast<UINT64>(_bump) * _descriptorSize;
	_bump += count;
	return true;
}
