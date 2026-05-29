#pragma once

#include "ISSAOProvider.hpp"

namespace HexEngine
{
	class Camera;
	class DiffuseGI;
	class IShader;
	class IConstantBuffer;
	class ITexture2D;

	/**
	 * @brief ISSAOProvider implementation that derives AO from the diffuse-GI
	 * voxel cone trace.
	 *
	 * The trace pass packs accumulated voxel occlusion into the alpha of its
	 * resolved RT. This provider samples that alpha and modulates the target
	 * (typically the beauty RT) with a multiplicative blend, mirroring how
	 * HBAOPlus integrates via GFSDK_SSAO_MULTIPLY_RGB.
	 *
	 * Cost: one tiny fullscreen quad with multiplicative blend per frame on top
	 * of the GI work the renderer already pays for. No separate cone trace, no
	 * additional g-buffer reads, no separate temporal accumulation.
	 *
	 * Quality trade-off vs HBAO:
	 *   + Captures medium-scale ambient (room interiors, building eaves,
	 *     overhanging geometry) more naturally because the GI voxels
	 *     already know what's in the world.
	 *   - Misses small-scale crevices (chair leg-to-floor corners, sub-pixel
	 *     contact gaps) that horizon-based SSAO catches more sharply.
	 *
	 * Hooked into Game3DEnvironment as the fallback when no SSAO plugin
	 * registers, and force-enabled when r_useGIAO is set.
	 */
	class HEX_API DiffuseGIAOProvider : public ISSAOProvider
	{
	public:
		explicit DiffuseGIAOProvider(DiffuseGI* gi);
		~DiffuseGIAOProvider();

		bool Create() override;
		void Destroy() override;

		void ApplyAmbientOcclusion(Camera* camera, ITexture2D* depthBuffer, ITexture2D* normals, ITexture2D* target) override;

	private:
		DiffuseGI*               _gi = nullptr;
		std::shared_ptr<IShader> _applyShader;
		IConstantBuffer*         _constants = nullptr;
	};
}
