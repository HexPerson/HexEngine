
#include "VertexBuffer.hpp"
#include "GraphicsDeviceD3D11.hpp"
#include <HexEngine.Core/Environment/IEnvironment.hpp>
#include <HexEngine.Core/Environment/LogFile.hpp>

namespace HexEngine
{
	VertexBuffer::~VertexBuffer()
	{
		Destroy();
	}

	void VertexBuffer::Destroy()
	{
		SAFE_RELEASE(_buffer);
	}

	void VertexBuffer::SetVertexData(uint8_t* data, uint32_t size, uint32_t offset /*= 0*/)
	{
		// if the buffer has not changed, there's no reason to write to it again
		if (bufferCopy.size() > 0 && bufferCopy.size() == size)
		{
			if (!memcmp(bufferCopy.data(), data, size))
				return;
		}

		g_pGraphics->Lock();

		if (_buffer)
		{
			D3D11_MAPPED_SUBRESOURCE resourceData;

			auto gfxDevice = (ID3D11DeviceContext*)g_pEnv->_graphicsDevice->GetNativeDeviceContext();

			CHECK_HR(gfxDevice->Map(
				_buffer,
				0,
				D3D11_MAP_WRITE_DISCARD,
				0,
				&resourceData
			));

			memcpy(((uint8_t*)resourceData.pData + offset), data, size);

			gfxDevice->Unmap(_buffer, 0);

			bufferCopy.clear();
			bufferCopy.insert(bufferCopy.end(), data, data + size);
		}

		g_pGraphics->Unlock();
	}

	void* VertexBuffer::GetNativePtr()
	{
		return _buffer;
	}

	uint32_t VertexBuffer::GetStride()
	{
		return _stride;
	}
}