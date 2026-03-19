#pragma once

#include "../Plugin/IPlugin.hpp"

namespace HexEngine
{
	/**
	 * @brief Per-frame input bundle consumed by denoiser implementations.
	 *
	 * Texture semantics follow the engine's SSR/lighting denoiser contract:
	 * diffuse/specular signal and hit-distance buffers, normal-depth, material,
	 * and motion vectors for temporal reprojection.
	 */
	struct DenoiserFrameData
	{
		/** Active camera for this denoiser dispatch. */
		Camera* camera = nullptr;
		/** Current frame sub-pixel jitter used by temporal passes. */
		math::Vector2 jitter;
		/** Diffuse radiance signal input. */
		ITexture2D* diffuseSignalInput = nullptr;
		/** Diffuse hit-distance input. */
		ITexture2D* diffuseHitDistance = nullptr;
		/** Specular radiance signal input. */
		ITexture2D* specularSignalInput = nullptr;
		/** Specular hit-distance input. */
		ITexture2D* specularHitDistance = nullptr;
		/** Packed normal/depth input. */
		ITexture2D* normalAndDepth = nullptr;
		/** Material data input (for example roughness/metallic). */
		ITexture2D* material = nullptr;
		/** Motion vectors used for temporal reprojection. */
		ITexture2D* motionVectors = nullptr;
	};

	/** @brief Plugin interface for frame denoiser backends (for example NRD/OIDN). */
	class IDenoiserProvider : public IPluginInterface
	{
	public:
		DECLARE_PLUGIN_INTERFACE(IDenoiserProvider, 001);

		/**
		 * @brief Allocates/refreshes backend resources for the current render size and input set.
		 * @param width Input/output width in pixels.
		 * @param height Input/output height in pixels.
		 */
		virtual void CreateBuffers(int32_t width, int32_t height, ITexture2D* diffuseSignalInput, ITexture2D* diffuseHitDistance, ITexture2D* specularSignalInput, ITexture2D* specularHitDistance, ITexture2D* normalAndDepth, ITexture2D* material, ITexture2D* motionVectors) = 0;

		/**
		 * @brief Builds a frame descriptor from the currently bound input textures.
		 * @param fd Output frame descriptor consumed by FilterFrame.
		 */
		virtual void BuildFrameData(DenoiserFrameData& fd, ITexture2D* diffuseSignalInput, ITexture2D* diffuseHitDistance, ITexture2D* specularSignalInput, ITexture2D* specularHitDistance, ITexture2D* normalAndDepth, ITexture2D* material, ITexture2D* motionVectors) = 0;

		/**
		 * @brief Runs denoising for the provided frame.
		 * @param fd Prepared frame input data.
		 * @param output Output texture receiving the denoised result.
		 */
		virtual void FilterFrame(const DenoiserFrameData& fd, ITexture2D* output) = 0;
	};
}
