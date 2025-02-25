

#pragma once

#include "../Required.hpp"

namespace HexEngine
{
	class HEX_API INativeGraphicsResource
	{
	public:
		virtual void Destroy() = 0;

		virtual void* GetNativePtr() = 0;

		virtual void SetDebugName(const std::string& name) {}
		
	};
}
