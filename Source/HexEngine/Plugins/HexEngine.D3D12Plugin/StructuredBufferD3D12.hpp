
#pragma once

#include <HexEngine.Core/Graphics/IStructuredBuffer.hpp>
#include <d3d12.h>
#include <wrl/client.h>

/**
 * @brief D3D12 structured buffer with optional SRV/UAV/counter views.
 *
 * Lives in the default heap (GPU-only memory) when ShaderResource/UAV access
 * is required. When only ShaderResource is asked and the caller writes
 * occasionally via SetData, we put it in an upload heap instead so SetData
 * can map-copy directly without a copy-queue staging step.
 *
 * Counter buffers for AppendConsume use a tiny secondary resource - 4 bytes
 * for the UINT32 counter, sitting alongside the main buffer.
 */
class StructuredBufferD3D12 : public HexEngine::IStructuredBuffer
{
public:
	virtual ~StructuredBufferD3D12() override { Destroy(); }

	virtual void  Destroy() override
	{
		if (_resource && _mapped)
		{
			_resource->Unmap(0, nullptr);
			_mapped = nullptr;
		}
		_resource.Reset();
		_counterResource.Reset();
		_srvIndex = UINT32_MAX;
		_uavIndex = UINT32_MAX;
	}

	virtual void* GetNativePtr() override { return _resource.Get(); }

	virtual bool SetData(const void* data, uint32_t byteSize, uint32_t dstByteOffset = 0) override;

	virtual uint32_t              GetElementStride() const override { return _elementStride; }
	virtual uint32_t              GetElementCount() const  override { return _elementCount; }
	virtual HexEngine::StructuredBufferFlags GetFlags() const override { return _flags; }

public:
	Microsoft::WRL::ComPtr<ID3D12Resource> _resource;
	Microsoft::WRL::ComPtr<ID3D12Resource> _counterResource; ///< 4 bytes, only when AppendConsume
	D3D12_CPU_DESCRIPTOR_HANDLE             _srv = {};
	D3D12_CPU_DESCRIPTOR_HANDLE             _uav = {};
	uint32_t                                 _srvIndex = UINT32_MAX;
	uint32_t                                 _uavIndex = UINT32_MAX;
	void*                                    _mapped = nullptr; ///< only when upload-heap-backed
	uint32_t                                 _elementStride = 0;
	uint32_t                                 _elementCount  = 0;
	HexEngine::StructuredBufferFlags         _flags = HexEngine::StructuredBufferFlags::None;
	bool                                     _isUploadHeap = false;
	D3D12_RESOURCE_STATES                    _currentState = D3D12_RESOURCE_STATE_COMMON;
};
