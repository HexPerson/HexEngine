
#include "IndexBuffer.hpp"
#include <HexEngine.Core/HexEngine.hpp>
#include "GraphicsDeviceD3D11.hpp"

IndexBuffer::~IndexBuffer()
{
	Destroy();
}

void IndexBuffer::Destroy()
{
	SAFE_RELEASE(_buffer);
}

void IndexBuffer::SetIndexData(uint8_t* data, uint32_t size, uint32_t offset /*= 0*/)
{
	g_pGraphics->Lock();

	if (_buffer)
	{
		D3D11_MAPPED_SUBRESOURCE resourceData;

		auto gfxDevice = (ID3D11DeviceContext*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDeviceContext();

		CHECK_HR(gfxDevice->Map(
			_buffer,
			0,
			D3D11_MAP_WRITE_DISCARD,
			0,
			&resourceData
		));

		memcpy(((uint8_t*)resourceData.pData + offset), data, size);

		gfxDevice->Unmap(_buffer, 0);
	}

	g_pGraphics->Unlock();
}

void* IndexBuffer::GetNativePtr()
{
	return _buffer;
}