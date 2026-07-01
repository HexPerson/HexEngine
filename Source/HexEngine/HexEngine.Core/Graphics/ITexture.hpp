

#pragma once

#include "INativeGraphicsResource.hpp"

namespace HexEngine
{
	/** @brief Shared texture interface for 2D/3D texture implementations. */
	class HEX_API ITexture : public INativeGraphicsResource
	{
	public:
        virtual ~ITexture() {}

		/** @brief Returns texture width in pixels. */
		virtual int32_t GetWidth() = 0;
		/** @brief Reads texture contents into an 8-bit buffer. */
		virtual void	GetPixels(std::vector<uint8_t>& buffer) = 0;
		/** @brief Reads texture contents into a floating-point buffer. */
		virtual void	GetPixels(std::vector<float>& buffer) = 0;
		/** @brief Returns an OS-level shared handle when supported by the backend. */
		virtual void*	GetSharedHandle() = 0;

		/**
		 * @brief Returns the backend-native shader-resource view (ID3D11ShaderResourceView*
		 * on D3D11, equivalent SRV descriptor handle on D3D12) for binding the texture
		 * as a read-only shader input. Callers must inspect the active backend before
		 * casting. May return nullptr if the texture was created without
		 * D3D11_BIND_SHADER_RESOURCE / SRV_DIMENSION set.
		 *
		 * Default implementation returns nullptr so backends that haven't implemented
		 * this yet still compile - callers should null-check and fall back gracefully.
		 */
		virtual void*	GetNativeShaderView() { return nullptr; }
		/** @brief Maps texture memory for CPU access and returns the mapped pointer. */
		virtual void*	LockPixels(int32_t* rowPitch = nullptr) = 0;
		/** @brief Unmaps previously locked texture memory. */
		virtual void	UnlockPixels() = 0;
	};
}
