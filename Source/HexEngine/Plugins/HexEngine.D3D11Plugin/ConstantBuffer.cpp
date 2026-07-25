

#include "ConstantBuffer.hpp"
#include "GraphicsDeviceD3D11.hpp"
#include <HexEngine.Core/Environment/IEnvironment.hpp>
// LOG_WARN dereferences g_pEnv->_logFile, which IEnvironment.hpp only forward-declares.
#include <HexEngine.Core/Environment/LogFile.hpp>

ConstantBuffer::ConstantBuffer(uint32_t bufferSize) :
	_bufferSize(bufferSize)
{
	_data = new uint8_t[bufferSize];
	memset(_data, 0, bufferSize);
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
	if (data == nullptr || _data == nullptr || _bufferSize == 0)
		return false;

	// Clamp to what was actually allocated. Callers pass sizeof() of a struct declared in
	// HexEngine.Core, but this buffer was sized by the same sizeof() evaluated when the
	// PLUGIN was compiled. Rebuilding Core after appending a field to one of those structs,
	// without also rebuilding this plugin, makes size > _bufferSize - which previously
	// over-read _data in the memcmp and over-wrote both the mapped GPU buffer and the _data
	// heap allocation, every frame. The visible symptom is subtle: the appended fields
	// simply never reach the GPU, so shaders read stale or garbage values for them while
	// everything else looks fine.
	if (size > _bufferSize)
	{
		if (!_loggedOverflow)
		{
			_loggedOverflow = true;
			LOG_WARN("ConstantBuffer::Write: %u bytes requested but buffer is %u - "
				"truncating. This means HexEngine.Core was rebuilt with a larger constant "
				"buffer struct than the graphics plugin was compiled against; rebuild the "
				"plugin.", size, _bufferSize);
		}

		size = _bufferSize;
	}

	// determine if the data actually changed
	if (memcmp(_data, data, size) == 0)
		return true;

	auto gfxDevice = (ID3D11DeviceContext*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDeviceContext();

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