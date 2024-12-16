

#pragma once

#include "INativeGraphicsResource.hpp"

namespace HexEngine
{
	class IShaderStage : public INativeGraphicsResource
	{
	public:
		virtual ~IShaderStage() {}

		virtual bool GetBinaryCode(std::vector<uint8_t>& code) = 0;

		virtual void CopyFrom(IShaderStage* other) = 0;

		/*IShaderStage& operator =(IShaderStage* other)
		{
			CopyFrom(other);

			return *this;
		}*/
	};
}
