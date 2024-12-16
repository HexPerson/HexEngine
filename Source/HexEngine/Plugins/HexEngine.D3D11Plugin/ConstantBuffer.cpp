

#include "ConstantBuffer.hpp"
#include "GraphicsDeviceD3D11.hpp"
#include <HexEngine.Core/Environment/IEnvironment.hpp>

namespace HexEngine
{
	ConstantBuffer::ConstantBuffer(uint32_t bufferSize)
	{
		_data = new uint8_t[bufferSize];
	}

	ConstantBuffer::~ConstantBuffer()
	{
		Destroy();

		SAFE_DELETE_ARRAY(_data);
	}

	void ConstantBuffer::Destroy()
	{
		SAFE_RELEASE(_buffer);
	}

	void* ConstantBuffer::GetNativePtr()
	{
		return reinterpret_cast<void*>(_buffer);
	}

	bool ConstantBuffer::Write(void* data, uint32_t size)
	{
		// determine if the data actually changed
		if (memcmp(_data, data, size) == 0)
			return true;

		auto gfxDevice = (ID3D11DeviceContext*)g_pEnv->_graphicsDevice->GetNativeDeviceContext();

		D3D11_MAPPED_SUBRESOURCE resource;
		if (gfxDevice->Map(_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &resource) == S_OK)
		{
			memcpy(resource.pData, data, size);

			gfxDevice->Unmap(_buffer, 0);

			memcpy(_data, data, size);

			return true;
		}

		return false;
	}
}