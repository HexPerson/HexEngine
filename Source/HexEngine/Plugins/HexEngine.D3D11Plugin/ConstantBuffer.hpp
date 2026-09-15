

#pragma once

#include <HexEngine.Core/Graphics/IConstantBuffer.hpp>

class ConstantBuffer : public HexEngine::IConstantBuffer
{
	friend class GraphicsDeviceD3D11;

public:
	ConstantBuffer(uint32_t bufferSize);

	virtual ~ConstantBuffer();

	virtual void Destroy() override;

	virtual void* GetNativePtr() override;

	virtual bool Write(void* data, uint32_t size) override;

private:
	ID3D11Buffer* _buffer = nullptr;
	uint8_t* _data = nullptr;

	// Size _data and the GPU buffer were allocated at. Write() clamps to this: the sizes
	// come from sizeof() on structs in HexEngine.Core headers, evaluated when THIS plugin
	// was compiled, so a Core rebuild that grows a cbuffer struct without rebuilding the
	// plugin would otherwise memcpy past both allocations every frame.
	uint32_t _bufferSize = 0;
	bool _loggedOverflow = false;
};
