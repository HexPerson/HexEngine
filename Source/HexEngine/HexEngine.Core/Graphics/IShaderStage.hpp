

#pragma once

#include "INativeGraphicsResource.hpp"

namespace HexEngine
{
	/** @brief Single compiled shader stage interface (VS/PS/GS/...). */
	class IShaderStage : public INativeGraphicsResource
	{
	public:
		virtual ~IShaderStage() {}

		/**
		 * @brief Retrieves shader bytecode for reflection/input-layout creation.
		 * @param code Output bytecode buffer.
		 */
		virtual bool GetBinaryCode(std::vector<uint8_t>& code) = 0;

		/**
		 * @brief Copies backend stage state from another stage object.
		 * @param other Source stage.
		 */
		virtual void CopyFrom(IShaderStage* other) = 0;

		/*IShaderStage& operator =(IShaderStage* other)
		{
			CopyFrom(other);

			return *this;
		}*/
	};
}
