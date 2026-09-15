
#pragma once

#include "ITexture2D.hpp"
#include "IShader.hpp"
#include "IConstantBuffer.hpp"

#include <vector>

namespace HexEngine
{
	class Camera;

	// Phase 4 progressive bloom (Jimenez/COD): a chain of discrete RGBA16F
	// levels (half res down to ~1/64 - this engine has no per-mip RTVs, so
	// each level is its own texture), 13-tap Karis-averaged downsample with
	// the physical prefilter folded into the first hop, 3x3 tent additive
	// upsample, and an energy-normalised composite. Replaces the single
	// quarter-res RT + 9-tap gaussian pair (hard 32px cut, 1-in-16 point
	// prefilter, unnormalised additive blend).
	class HEX_API Bloom
	{
	public:
		~Bloom();

		// width/height = FULL render resolution; the chain sizes itself.
		void Create(int32_t width, int32_t height);
		void Destroy();

		// sceneHdr gains the bloom in place. compositeScratch is a same-size
		// RGBA16F RT the composite draws into before the single copy back
		// (the caller lends one that is idle at this point in the frame -
		// the SSS intermediate).
		void Render(Camera* camera, ITexture2D* sceneHdr, ITexture2D* compositeScratch);

	private:
		std::vector<ITexture2D*> _chain;   // [0] = half res ... [N-1] = smallest
		// Temporal EMA of the accumulated chain top (see BloomTemporal.shader):
		// TAA leaves residual shimmer on thin ultra-bright features, and the
		// bloom threshold / flare knee amplify it into intermittent pops.
		ITexture2D* _bloomHistory = nullptr;
		bool _bloomHistoryValid = false;
		std::shared_ptr<IShader> _temporalShader;
		std::shared_ptr<IShader> _downsampleShader;
		std::shared_ptr<IShader> _upsampleShader;
		std::shared_ptr<IShader> _compositeShader;
		IConstantBuffer* _paramsBuffer = nullptr; // b6: per-hop texel size + flags
	};
}
