
#include "IndexBufferD3D12.hpp"
#include "GraphicsDeviceD3D12.hpp"

#include <cstring>

namespace
{
	D3D12_RESOURCE_DESC MakeUploadBufferDesc(uint64_t bytes)
	{
		D3D12_RESOURCE_DESC d = {};
		d.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
		d.Width              = bytes;
		d.Height             = 1;
		d.DepthOrArraySize   = 1;
		d.MipLevels          = 1;
		d.Format             = DXGI_FORMAT_UNKNOWN;
		d.SampleDesc.Count   = 1;
		d.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		return d;
	}

	D3D12_HEAP_PROPERTIES UploadHeap()
	{
		D3D12_HEAP_PROPERTIES h = {};
		h.Type = D3D12_HEAP_TYPE_UPLOAD;
		return h;
	}
}

void IndexBufferD3D12::Destroy()
{
	for (auto& u : _uploads)
	{
		if (u.resource && u.mapped) u.resource->Unmap(0, nullptr);
		if (u.resource && _device != nullptr)
			_device->DeferredRelease(std::move(u.resource));
	}
	_uploads.clear();
	_activeResource = nullptr;
}

void IndexBufferD3D12::SetIndexData(uint8_t* data, uint32_t size, uint32_t offset /*= 0*/)
{
	if (data == nullptr || size == 0 || _byteSize == 0 || _device == nullptr) return;
	if (offset + size > _byteSize) return;

	const uint64_t completed = _device->GetCompletedFenceValue();

	UploadEntry* avail = nullptr;
	for (auto& u : _uploads)
	{
		if (u.fence <= completed) { avail = &u; break; }
	}

	if (avail == nullptr)
	{
		UploadEntry entry;
		auto desc = MakeUploadBufferDesc(_byteSize);
		auto heap = UploadHeap();
		if (FAILED(_device->GetDevice()->CreateCommittedResource(
			&heap, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
			IID_PPV_ARGS(&entry.resource))))
		{
			return;
		}
		D3D12_RANGE noRead = { 0, 0 };
		entry.resource->Map(0, &noRead, &entry.mapped);
		_uploads.push_back(std::move(entry));
		avail = &_uploads.back();
	}

	std::memcpy(static_cast<uint8_t*>(avail->mapped) + offset, data, size);
	avail->fence = _device->GetPendingFenceValue();

	_activeResource      = avail->resource.Get();
	_view.BufferLocation = avail->resource->GetGPUVirtualAddress();
	_view.SizeInBytes    = _byteSize;
	_view.Format         = (_stride == 2) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
}
