

#pragma once

#include "../Required.hpp"

namespace HexEngine
{
	/** @brief Base interface for graphics resources that wrap a native API object. */
	class HEX_API INativeGraphicsResource
	{
	public:
		/** @brief Releases the underlying graphics API resource. */
		virtual void Destroy() = 0;

		/** @brief Returns the backend-native pointer (for example ID3D11*). */
		virtual void* GetNativePtr() = 0;

		/** @brief Assigns a backend debug name when supported. */
		virtual void SetDebugName(const std::string& name) {}
		
	};
}
