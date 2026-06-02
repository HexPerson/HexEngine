
#include "StructuredBufferD3D12.hpp"
#include "GraphicsDeviceD3D12.hpp"
#include <HexEngine.Core/HexEngine.hpp>

bool StructuredBufferD3D12::SetData(const void* data, uint32_t byteSize, uint32_t dstByteOffset)
{
	if (_resource == nullptr || data == nullptr || byteSize == 0)
		return false;
	const uint32_t total = _elementStride * _elementCount;
	if (dstByteOffset + byteSize > total)
		return false;

	// Upload-heap variant: persistent-mapped, just memcpy in.
	if (_isUploadHeap)
	{
		if (_mapped == nullptr) return false;
		memcpy(static_cast<uint8_t*>(_mapped) + dstByteOffset, data, byteSize);
		return true;
	}

	// Default-heap variant: stage through a temporary upload heap + record a
	// copy on the device's open command list. Hands off ownership of the
	// staging resource to the next-frame fence-wait via the device.
	auto* device = static_cast<GraphicsDeviceD3D12*>(HexEngine::g_pEnv->_graphicsDevice);
	return device != nullptr && device->UploadBufferData(this, data, byteSize, dstByteOffset);
}
