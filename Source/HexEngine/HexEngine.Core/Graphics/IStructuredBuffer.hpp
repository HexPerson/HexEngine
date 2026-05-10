#pragma once

#include "INativeGraphicsResource.hpp"

namespace HexEngine
{
	enum class StructuredBufferFlags : uint32_t
	{
		None = 0,
		ShaderResource = HEX_BITSET(0),
		UnorderedAccess = HEX_BITSET(1),
		AppendConsume = HEX_BITSET(2),
		DrawIndirectArgs = HEX_BITSET(3)
	};

	DEFINE_ENUM_FLAG_OPERATORS(StructuredBufferFlags);

	/** @brief Backend-neutral structured/append GPU buffer abstraction. */
	class IStructuredBuffer : public INativeGraphicsResource
	{
	public:
		virtual ~IStructuredBuffer() {}

		virtual bool SetData(const void* data, uint32_t byteSize, uint32_t dstByteOffset = 0) = 0;
		virtual uint32_t GetElementStride() const = 0;
		virtual uint32_t GetElementCount() const = 0;
		virtual StructuredBufferFlags GetFlags() const = 0;
	};
}
