
#include "DescriptorHeapAllocator.hpp"
#include <HexEngine.Core/HexEngine.hpp>

bool DescriptorHeapAllocator::Create(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t capacity, bool shaderVisible)
{
	if (device == nullptr || capacity == 0)
		return false;

	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.Type           = type;
	desc.NumDescriptors = capacity;
	// RTV / DSV heaps are CPU-only by spec - silently drop the shaderVisible
	// request rather than failing creation.
	const bool canBeShaderVisible = (type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
		|| (type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
	desc.Flags = (shaderVisible && canBeShaderVisible)
		? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
		: D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	desc.NodeMask = 0;

	if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&_heap))))
	{
		LOG_CRIT("DescriptorHeapAllocator::Create CreateDescriptorHeap failed (type=%d, capacity=%u)", (int)type, capacity);
		return false;
	}

	_capacity       = capacity;
	_descriptorSize = device->GetDescriptorHandleIncrementSize(type);
	_cpuStart       = _heap->GetCPUDescriptorHandleForHeapStart();
	_gpuStart       = (desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
		? _heap->GetGPUDescriptorHandleForHeapStart()
		: D3D12_GPU_DESCRIPTOR_HANDLE{};
	_nextLinear     = 0;
	_shaderVisible  = (desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) != 0;
	_freeList.clear();
	_freeList.reserve(capacity / 8);
	return true;
}

void DescriptorHeapAllocator::Destroy()
{
	_heap.Reset();
	_freeList.clear();
	_capacity = 0;
	_descriptorSize = 0;
	_nextLinear = 0;
	_cpuStart = {};
	_gpuStart = {};
	_shaderVisible = false;
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHeapAllocator::Allocate(uint32_t& outIndex)
{
	// Prefer freed-up slots over advancing _nextLinear, so we keep the
	// allocation high-water-mark low and let the heap fill its capacity even
	// after lots of churn.
	if (!_freeList.empty())
	{
		outIndex = _freeList.back();
		_freeList.pop_back();
	}
	else
	{
		if (_nextLinear >= _capacity)
		{
			LOG_CRIT("DescriptorHeapAllocator: out of slots (capacity=%u)", _capacity);
			outIndex = UINT32_MAX;
			return D3D12_CPU_DESCRIPTOR_HANDLE{};
		}
		outIndex = _nextLinear++;
	}
	return GetCpuHandle(outIndex);
}

void DescriptorHeapAllocator::Free(uint32_t index)
{
	if (index == UINT32_MAX || index >= _capacity)
		return;
	_freeList.push_back(index);
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHeapAllocator::GetCpuHandle(uint32_t index) const
{
	D3D12_CPU_DESCRIPTOR_HANDLE h = _cpuStart;
	h.ptr += static_cast<SIZE_T>(index) * _descriptorSize;
	return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE DescriptorHeapAllocator::GetGpuHandle(uint32_t index) const
{
	D3D12_GPU_DESCRIPTOR_HANDLE h = _gpuStart;
	h.ptr += static_cast<UINT64>(index) * _descriptorSize;
	return h;
}
