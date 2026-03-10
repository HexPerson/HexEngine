
#pragma once

#include <HexEngine.Core/Graphics/IIndexBuffer.hpp>

class IndexBuffer : public HexEngine::IIndexBuffer
{
	friend class GraphicsDeviceD3D11;

public:
	virtual ~IndexBuffer() override;

	virtual void SetIndexData(uint8_t* data, uint32_t size, uint32_t offset = 0) override;

	virtual void Destroy() override;

	virtual void* GetNativePtr() override;

private:
	ID3D11Buffer* _buffer = nullptr;
};
