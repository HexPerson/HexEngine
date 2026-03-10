

#pragma once

#include <HexEngine.Core/Graphics/IVertexBuffer.hpp>

class VertexBuffer : public HexEngine::IVertexBuffer
{
	friend class GraphicsDeviceD3D11;

public:
	virtual ~VertexBuffer();

	virtual void SetVertexData(uint8_t* data, uint32_t size, uint32_t offset = 0) override;

	virtual void Destroy() override;

	virtual void* GetNativePtr() override;

	virtual uint32_t GetStride() override;

private:
	ID3D11Buffer* _buffer = nullptr;
	uint32_t _stride = 0;
	std::vector<uint8_t> bufferCopy;
};