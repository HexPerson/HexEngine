#pragma once

#include <HexEngine.Core/Graphics/IStructuredBuffer.hpp>
#include <HexEngine.Core/Environment/IEnvironment.hpp>

class StructuredBuffer : public HexEngine::IStructuredBuffer
{
	friend class GraphicsDeviceD3D11;

public:
	virtual ~StructuredBuffer()
	{
		Destroy();
	}

	virtual void Destroy() override
	{
		SAFE_RELEASE(_shaderResourceView);
		SAFE_RELEASE(_unorderedAccessView);
		SAFE_RELEASE(_buffer);
	}

	virtual void* GetNativePtr() override
	{
		return _buffer;
	}

	virtual bool SetData(const void* data, uint32_t byteSize, uint32_t dstByteOffset = 0) override
	{
		if (_buffer == nullptr || data == nullptr || byteSize == 0)
		{
			return false;
		}

		auto* context = reinterpret_cast<ID3D11DeviceContext*>(HexEngine::g_pEnv->_graphicsDevice->GetNativeDeviceContext());
		if (context == nullptr)
		{
			return false;
		}

		if (_usage == D3D11_USAGE_DYNAMIC && (_cpuAccessFlags & D3D11_CPU_ACCESS_WRITE) != 0)
		{
			D3D11_MAPPED_SUBRESOURCE resource = {};
			if (FAILED(context->Map(_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &resource)))
			{
				return false;
			}

			memcpy(static_cast<uint8_t*>(resource.pData) + dstByteOffset, data, byteSize);
			context->Unmap(_buffer, 0);
			return true;
		}

		context->UpdateSubresource(_buffer, 0, nullptr, data, 0, 0);
		return true;
	}
	virtual uint32_t GetElementStride() const override { return _elementStride; }
	virtual uint32_t GetElementCount() const override { return _elementCount; }
	virtual HexEngine::StructuredBufferFlags GetFlags() const override { return _flags; }

private:
	ID3D11Buffer* _buffer = nullptr;
	ID3D11ShaderResourceView* _shaderResourceView = nullptr;
	ID3D11UnorderedAccessView* _unorderedAccessView = nullptr;
	uint32_t _elementStride = 0;
	uint32_t _elementCount = 0;
	uint32_t _cpuAccessFlags = 0;
	D3D11_USAGE _usage = D3D11_USAGE_DEFAULT;
	HexEngine::StructuredBufferFlags _flags = HexEngine::StructuredBufferFlags::None;
};
