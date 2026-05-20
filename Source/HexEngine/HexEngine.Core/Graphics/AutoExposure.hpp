#pragma once

#include "../Required.hpp"
#include "ITexture2D.hpp"
#include "IShader.hpp"

struct ID3D11Buffer;
struct ID3D11UnorderedAccessView;
struct ID3D11ShaderResourceView;
struct ID3D11Texture2D;

namespace HexEngine
{
	// Auto exposure: dispatches a compute pass that reads the post-processed beauty buffer,
	// reduces a strided sample of pixels to a single average log-luminance value, reads the
	// result back to the CPU, and smoothly adapts an exposure multiplier toward a target
	// middle-grey luminance.
	//
	// The output multiplier is plugged into the existing colour-grading exposure path so the
	// existing tonemap/colour-grade shader doesn't need to change. When auto exposure is off,
	// GetExposureMultiplier() returns 1.0 and the user-set r_exposure HVar passes through
	// unchanged.
	class HEX_API AutoExposure
	{
	public:
		~AutoExposure();

		bool Create();
		void Destroy();

		// Dispatch the luminance compute pass against the beauty input. Reads back the previous
		// frame's result (if available), smooths the current exposure toward the target, and
		// returns the new multiplier via GetExposureMultiplier().
		//
		// `sunElevation` is the y-component of the sun's "up" direction (i.e. `-lightDir.y`):
		// > 0 = above horizon, <= 0 = below. It's used to blend the day-time target/max
		// against the night-time overrides (r_autoExposureNightTargetLuma / r_autoExposureNightMax)
		// so the meter doesn't push a night scene up to daytime brightness.
		void Update(ITexture2D* beauty, float deltaTimeSeconds, float sunElevation = 1.0f);

		// Current exposure multiplier (1.0 = no change). Multiply this with the user-set
		// r_exposure value before feeding the colour-grading constant buffer.
		float GetExposureMultiplier() const { return _smoothedExposure; }

		// Reset the smoothed exposure back to 1.0 - call when a level/scene is loaded so the
		// adaptation doesn't carry over from the previous environment.
		void Reset();

	private:
		bool EnsureResources(uint32_t inputWidth, uint32_t inputHeight);
		void ReleaseResources();

		std::shared_ptr<IShader> _luminanceShader;

		// Output accumulator (32-bit uint, atomic-added by the compute shader).
		ID3D11Buffer* _accumBuffer = nullptr;
		ID3D11UnorderedAccessView* _accumUav = nullptr;

		// Staging buffer for CPU readback. Has to be USAGE_STAGING with CPU_ACCESS_READ; we
		// CopyResource into it then Map it the following frame to avoid stalling the GPU.
		ID3D11Buffer* _accumStaging = nullptr;

		// Constants for the compute shader (input size, stride, log-luma range).
		ID3D11Buffer* _constantBuffer = nullptr;

		// SRV view onto the beauty texture - we need to bind it to the compute shader at t0,
		// but the engine creates its render-target SRVs lazily; track our own.
		ID3D11ShaderResourceView* _beautySrv = nullptr;
		ID3D11Texture2D* _beautyTrackedTex = nullptr;  // non-owning pointer for identity check

		// Tracking
		uint32_t _inputWidth = 0;
		uint32_t _inputHeight = 0;
		uint32_t _lastDispatchSampleCount = 0;
		bool _hasPendingReadback = false;

		// Current smoothed exposure multiplier. Updated each frame toward the target derived
		// from the latest readback.
		float _smoothedExposure = 1.0f;

		// Throttles r_autoExposureDebug log spew to ~1Hz.
		float _debugAccum = 0.0f;
	};
}
