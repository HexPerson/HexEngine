
#include "VertexBufferD3D12.hpp"
#include "GraphicsDeviceD3D12.hpp"
#include <HexEngine.Core/HexEngine.hpp>

#include <cstring>
#include <cmath>

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

void VertexBufferD3D12::Destroy()
{
	for (auto& u : _uploads)
	{
		if (u.resource && u.mapped) u.resource->Unmap(0, nullptr);
		// Defer release - in-flight cmd lists may still reference any of
		// these entries via captured VBV.BufferLocation values.
		if (u.resource && _device != nullptr)
			_device->DeferredRelease(std::move(u.resource));
	}
	_uploads.clear();
	_activeResource = nullptr;
}

void VertexBufferD3D12::SetVertexData(uint8_t* data, uint32_t size, uint32_t offset /*= 0*/)
{
	if (data == nullptr || size == 0 || _byteSize == 0 || _device == nullptr) return;
	if (offset + size > _byteSize)
	{
		// B5 visual-parity debug: a dropped upload leaves this buffer holding
		// stale/garbage geometry while the draw still references it - a prime
		// suspect for the "some meshes scramble / reversed winding" corruption.
		// D3D11's SetVertexData never drops (persistent MAP_WRITE_DISCARD), so
		// this path is a backend divergence. If it fires, the buffer was created
		// too small for the data the engine later writes.
		static int s_drop = 0;
		if (s_drop < 16)
		{
			++s_drop;
			LOG_WARN("D3D12 VB SetVertexData DROPPED upload: offset=%u size=%u > byteSize=%u (stride=%u) - mesh keeps stale geometry",
				offset, size, _byteSize, _stride);
		}
		return;
	}

	const uint64_t completed = _device->GetCompletedFenceValue();

	// Find an upload entry whose last-use fence has completed.
	UploadEntry* avail = nullptr;
	for (auto& u : _uploads)
	{
		if (u.fence <= completed) { avail = &u; break; }
	}

	// None free - allocate another one. Sub-resources are per-buffer so a
	// static VB stays at 1 entry forever; only buffers actually rewritten
	// mid-frame grow.
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
	// Stamp with EndFrame's about-to-be-signaled value: any draw queued
	// before that signal needs this entry's data to stay intact.
	avail->fence = _device->GetPendingFenceValue();

	_activeResource          = avail->resource.Get();
	_view.BufferLocation     = avail->resource->GetGPUVirtualAddress();
	_view.SizeInBytes        = _byteSize;
	_view.StrideInBytes      = _stride;

	// Diagnostic (B5 visual-parity debug): instance VBs have stride >= ~208
	// (MeshInstanceData = 3 matrices + colour + uvscale). Scan the first
	// matrix's 16 floats for NaN/Inf/absurdly-large magnitudes which would
	// cause the radial-fan corruption pattern observed in the editor.
	// Limited to a handful of reports to avoid log spam.
	if (_stride >= 200 && size >= 64 && data != nullptr)
	{
		static int s_badReports = 0;
		if (s_badReports < 8)
		{
			const float* m = reinterpret_cast<const float*>(data);
			bool bad = false;
			for (int i = 0; i < 16; ++i)
			{
				if (!std::isfinite(m[i]) || std::fabs(m[i]) > 1.0e7f) { bad = true; break; }
			}
			if (bad)
			{
				++s_badReports;
				LOG_WARN("D3D12 instance-VB SetVertexData: WORLD[0..3] looks bad: "
					"[%g %g %g %g | %g %g %g %g | %g %g %g %g | %g %g %g %g] stride=%u size=%u",
					m[0],m[1],m[2],m[3], m[4],m[5],m[6],m[7],
					m[8],m[9],m[10],m[11], m[12],m[13],m[14],m[15],
					_stride, size);
			}
		}
	}
}
