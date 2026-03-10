

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
};
