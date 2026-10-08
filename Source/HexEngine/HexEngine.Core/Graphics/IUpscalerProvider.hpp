#pragma once

#include "../Plugin/IPlugin.hpp"

namespace HexEngine
{
	class ITexture2D;

	/** @brief Render-scale presets shared by temporal upscalers (FSR 2 ratios). */
	enum class UpscalerQuality : int32_t
	{
		NativeAA = 0,			///< 1.0x - temporal AA only, no upscale
		Quality = 1,			///< 1.5x
		Balanced = 2,			///< 1.7x
		Performance = 3,		///< 2.0x
		UltraPerformance = 4,	///< 3.0x
		Count
	};

	/**
	 * @brief One frame's inputs for a temporal upscaler.
	 *
	 * Everything is at render resolution except `output` (display resolution,
	 * created with UAV binding). Texture conventions are the engine's own; the
	 * provider converts:
	 *   - colour: linear HDR, jittered, not tonemapped
	 *   - depth: standard (near 0, far 1), non-infinite
	 *   - motion vectors: CalcVelocity's [0,1]-NDC delta (previous - current, +y up)
	 */
	struct UpscalerFrameInputs
	{
		ITexture2D* color = nullptr;
		ITexture2D* depth = nullptr;
		ITexture2D* motionVectors = nullptr;
		ITexture2D* output = nullptr;

		uint32_t renderWidth = 0;
		uint32_t renderHeight = 0;
		uint32_t displayWidth = 0;
		uint32_t displayHeight = 0;

		/** Jitter applied to the projection this frame, in render pixels (the provider's GetJitterOffset convention). */
		math::Vector2 jitterPixels;

		float frameTimeDeltaMs = 16.6f;
		float cameraNear = 0.1f;
		float cameraFar = 1000.0f;
		float cameraFovYRadians = 1.0f;

		bool sharpen = true;
		float sharpness = 0.5f;		///< 0..1

		/** Discard history (camera cut / teleport / first frame). */
		bool reset = false;
	};

	/**
	 * @brief Plugin interface for temporal upscalers (FSR 2 today).
	 *
	 * The renderer jitters with GetJitterOffset, renders at GetRenderResolution,
	 * then calls Evaluate in place of its own TAA - the upscaler is the temporal
	 * resolver. Providers create their GPU state lazily on the first Evaluate and
	 * rebuild it when the display or render size changes.
	 */
	class IUpscalerProvider : public IPluginInterface
	{
	public:
		DECLARE_PLUGIN_INTERFACE(IUpscalerProvider, 001);

		/** @brief Short display name, e.g. "FSR 2.2". */
		virtual const char* GetName() const = 0;

		/** @brief Whether the current graphics backend/device can run this upscaler. */
		virtual bool IsSupported() = 0;

		/** @brief Render size for a quality preset at the given display size. */
		virtual bool GetRenderResolution(UpscalerQuality quality, uint32_t displayWidth, uint32_t displayHeight,
			uint32_t& renderWidth, uint32_t& renderHeight) = 0;

		/**
		 * @brief Sub-pixel jitter for a frame, in render pixels, in [-0.5, 0.5].
		 *
		 * +x right, +y DOWN (image space). To apply to a projection:
		 * ndc = (2 * x / renderWidth, -2 * y / renderHeight).
		 */
		virtual math::Vector2 GetJitterOffset(uint64_t frameIndex, uint32_t renderWidth, uint32_t displayWidth) = 0;

		/** @brief Records the upscale into the immediate context. False = nothing was written to `output`. */
		virtual bool Evaluate(const UpscalerFrameInputs& inputs) = 0;
	};
}
