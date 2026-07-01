#pragma once

#include "../Required.hpp"
#include "../Math/FloatMath.hpp"

struct ID3D11UnorderedAccessView;
struct ID3D11SamplerState;
struct ID3D11Texture2D;

namespace HexEngine
{
	class IConstantBuffer;
	class IShader;
	class ITexture3D;

	/**
	 * @brief Phase D froxel-grid volumetric scattering.
	 *
	 * Owns the two 3D camera-frustum-aligned volumes that drive proper
	 * depth-correct volumetric lighting:
	 *   - _scatterVolume     : per-froxel (scatter_radiance, extinction)
	 *   - _integrationVolume : per-froxel (accumulated_inscatter, transmittance)
	 *
	 * Per-frame pipeline (in Update):
	 *   1. Scatter pass (compute) - fills _scatterVolume from medium density
	 *      and sun contribution.
	 *   2. Integration pass (compute) - scans along W axis accumulating
	 *      inscatter and transmittance from camera into _integrationVolume.
	 *
	 * The apply pass (a fullscreen pixel shader) lives in SceneRenderer and
	 * samples _integrationVolume to composite into the beauty buffer. It
	 * replaces the per-pixel ray-march in the legacy VolumetricLighting
	 * shader once Phase D-2 retires that path.
	 */
	class HEX_API VolumetricScattering
	{
	public:
		// Volume dimensions. 128x72 matches a 16:9 ratio at decent density;
		// 64 slices over the 256 m far plane gives ~0.1 m near, ~30 m far.
		static constexpr uint32_t kVolumeWidth  = 128u;
		static constexpr uint32_t kVolumeHeight = 72u;
		static constexpr uint32_t kVolumeDepth  = 64u;
		// Far extent of the froxel range. Beyond this distance the aerial-
		// perspective volume handles atmospheric scattering, so this only
		// needs to cover the visible "sun shafts through nearby haze" range.
		static constexpr float    kFarDepthM   = 128.0f;
		// Matches the engine's directional-light cascade count
		// (DirectionalLight stores 4 shadow maps; r_shadowCascades caps
		// at 4). The scatter pass samples cascade 0..N-1 per froxel and
		// uses the first cascade that contains the position, so god
		// rays appear across the full shadow-data range instead of
		// stopping at cascade 0 (~50m).
		static constexpr uint32_t kMaxCascades = 4u;
		// Max shadow-casting spot lights that can occlude the volumetric.
		// Each consumes one Texture2D shader slot + one VP matrix in the
		// scatter cbuffer, so the cap keeps binding cost bounded. Shadow-
		// casting spots beyond this count fall back to v1 behaviour (no
		// occlusion, light shines through walls in the haze).
		static constexpr uint32_t kMaxShadowedSpots = 4u;
		// Matches ForwardLightConstants::kMaxForwardSpotLights - we reuse
		// the forward-lights list rather than separately culling.
		static constexpr uint32_t kMaxForwardSpots = 16u;
		// Max shadow-casting point lights. Each consumes 6 cubemap faces
		// in a shared TextureCubeArray (1024^2 R32_FLOAT per face). At
		// kMaxShadowedPoints=2 that's 48 MB VRAM - significant but
		// manageable; bumping the cap means doubling VRAM cost.
		static constexpr uint32_t kMaxShadowedPoints = 2u;
		// Matches ForwardLightConstants::kMaxForwardPointLights.
		static constexpr uint32_t kMaxForwardPoints  = 16u;
		// Point-light shadow cubemap-array resolution. Matches the per-
		// face ShadowMap resolution used by PointLight (1024x1024), so
		// each frame's GPU copy is 1:1 without filtering. Lower would
		// save VRAM but require a downsample pass.
		static constexpr uint32_t kPointShadowFaceSize = 1024u;

		VolumetricScattering();
		~VolumetricScattering();

		bool Create();
		void Destroy();

		/**
		 * @brief Per-frame dispatch.
		 * @param sunDirection World-space direction surface->sun. Need not be normalised.
		 * @param sunColour    Sun light radiance (RGB). Driven by SceneRenderer
		 *                     from the directional light's colour/intensity.
		 * @param sunIntensity Scalar light multiplier (driven by lightMult * energy).
		 * @param phaseG       Henyey-Greenstein anisotropy for the medium.
		 *                     Positive = forward-peaked Mie. ~0.5-0.8 typical.
		 * @param baseExtinction      Per-metre uniform fog/haze extinction.
		 * @param heightDensity       Per-metre exponential height-fog density.
		 * @param heightPivot         Y altitude where the height fog peaks.
		 * @param heightFalloff       Exponential falloff above/below the pivot.
		 * @param cascadeVPs   World->light-clip matrices for cascades
		 *                     0..numCascades-1. Each froxel projects its
		 *                     world position through these and uses the
		 *                     first cascade whose NDC bounds contain it.
		 * @param cascadeShadowMaps Cascade depth textures matching
		 *                     cascadeVPs. nullptrs are tolerated (treated
		 *                     as missing cascade); if numCascades is 0 the
		 *                     volumetric runs unshadowed everywhere.
		 * @param numCascades  How many of the cascade slots are valid
		 *                     (clamped to kMaxCascades). DirectionalLight
		 *                     supports up to 4; r_shadowCascades may set
		 *                     fewer.
		 * @param shadowBias   NDC.z bias to suppress self-shadowing fireflies.
		 *                     ~0.001 typical.
		 * @param shadowMapSize Shadow-map dimensions (width, height) in
		 *                     pixels - all cascades share resolution in
		 *                     this engine. Used for texel-space PCF
		 *                     offsets in the scatter shader.
		 * @param currentViewProj Current frame's camera view-projection
		 *                     matrix (world->clip). The integrate pass stores
		 *                     this internally and uses it on the NEXT frame
		 *                     as the previous-frame matrix for history
		 *                     reprojection: each froxel reconstructs its
		 *                     world position, projects through last frame's
		 *                     viewProj to find where it WAS in the history
		 *                     volume, and samples there. Camera motion no
		 *                     longer drags the EMA into the wrong cell.
		 */
		void Update(const math::Vector3& sunDirection,
		            const math::Vector3& sunColour,
		            float sunIntensity,
		            float phaseG,
		            float scatteringStrength,
		            float baseExtinction,
		            float heightDensity,
		            float heightPivot,
		            float heightFalloff,
		            const math::Matrix (&cascadeVPs)[kMaxCascades],
		            class ITexture2D* const (&cascadeShadowMaps)[kMaxCascades],
		            uint32_t numCascades,
		            float shadowBias,
		            const math::Vector2& shadowMapSize,
		            const math::Matrix& currentViewProj,
		            const math::Vector3& currentEyePos,
		            class ITexture2D* atmosphereTransmittanceLUT,
		            class IConstantBuffer* forwardLightsBuffer,
		            // Per-shadow-slot data for the (up to 4) shadowed spot
		            // lights. spotShadowVPs[s] = world->light-clip for slot s.
		            // spotShadowMaps[s] = depth texture for slot s. Unused
		            // slots can be Identity / nullptr.
		            const math::Matrix (&spotShadowVPs)[kMaxShadowedSpots],
		            class ITexture2D* const (&spotShadowMaps)[kMaxShadowedSpots],
		            // Per-forward-spot mapping: shadowSlotForForwardSpot[i] is
		            // the slot index (0..kMaxShadowedSpots-1) that holds spot i's
		            // shadow data, or -1 if forward spot i is unshadowed.
		            const int (&shadowSlotForForwardSpot)[kMaxForwardSpots],
		            uint32_t numShadowedSpots,
		            const math::Vector2& spotShadowMapSize,
		            float spotShadowBias,
		            // Point shadow data. For each of the (up to) kMaxShadowedPoints
		            // shadowed point lights, pointShadowFaceMaps[slot] is an array
		            // of 6 ITexture2D* (one per cube face) that we GPU-copy each
		            // frame into the cubemap array. pointShadowFarMetres[slot] is
		            // the PointLight's radius (= per-face far plane); near is
		            // hard-coded to 1.0 to match PointLight::ConstructMatrices.
		            class ITexture2D* const pointShadowFaceMaps[kMaxShadowedPoints][6],
		            const float (&pointShadowFarMetres)[kMaxShadowedPoints],
		            const int (&shadowSlotForForwardPoint)[kMaxForwardPoints],
		            uint32_t numShadowedPoints,
		            float pointShadowBiasMetres,
		            // Screen-space emissive injection. gbufferDiffuse carries the
		            // surface tint, gbufferPosition.w the emissive intensity
		            // (length(emission), written by DefaultPixel.shader). Each
		            // froxel samples both at its own screen UV and gains glow
		            // with distance falloff - fog around neon signs and lit
		            // windows lights up. Both nullptr-safe; emissiveStrength 0
		            // disables the path entirely.
		            class ITexture2D* gbufferDiffuse = nullptr,
		            class ITexture2D* gbufferPosition = nullptr,
		            float emissiveStrength = 0.0f,
		            float emissiveRangeMetres = 6.0f,
		            // Ambient inscatter of the fog medium itself: scatter +=
		            // density * fogAmbientColour * fogAmbientStrength. This is
		            // what makes dense weather fog read as a grey/coloured soup
		            // instead of pure darkening - it approximates the multiple
		            // scattering of skylight inside the medium. Colour comes
		            // from the scene ambient (weather-authored).
		            const math::Vector3& fogAmbientColour = math::Vector3(0.0f, 0.0f, 0.0f),
		            float fogAmbientStrength = 0.0f);

		ITexture3D* GetScatterVolume() const     { return _scatterVolume; }
		// Returns the most-recently-written integration volume (the one
		// containing this frame's blended-with-history result). The
		// apply pass samples this.
		ITexture3D* GetIntegrationVolume() const { return _integrationVolumes[_writeIdx]; }

	private:
		bool EnsureResources();
		void ReleaseResources();

		IConstantBuffer* _scatterParamsCBuffer   = nullptr;
		IConstantBuffer* _integrateParamsCBuffer = nullptr;

		ITexture3D* _scatterVolume     = nullptr; // RGBA16F
		// Ping-pong integration volumes for temporal accumulation. Each
		// frame the integrate compute reads the PREVIOUS frame from
		// [writeIdx ^ 1] (as history input) and writes the EMA-blended
		// result to [writeIdx]. The apply pass reads [writeIdx]. After
		// each Update we flip writeIdx so next frame's history input is
		// this frame's output.
		ITexture3D* _integrationVolumes[2] = { nullptr, nullptr };
		ID3D11UnorderedAccessView* _integrationUavs[2] = { nullptr, nullptr };
		uint32_t _writeIdx = 0u;
		// Monotonic frame counter for Halton jitter. Each frame picks a
		// different sub-froxel sample position; combined with the EMA
		// blend this is effectively temporal supersampling.
		uint32_t _jitterFrame = 0u;

		ID3D11UnorderedAccessView* _scatterUav     = nullptr;
		// Point-clamp sampler bound at s2 during the scatter dispatch for
		// shadow map lookups. The engine's global samplers don't auto-bind
		// to the CS stage so we own a private one (same pattern as
		// AtmosphereLUTs::_linearClampSampler).
		ID3D11SamplerState* _shadowPointSampler = nullptr;
		// Linear-clamp sampler bound at s4 during the integrate dispatch
		// for sub-froxel-accurate history reprojection sampling. Linear
		// filter gives proper interpolation when the reprojected UVW
		// lands between texels (which is the common case under motion).
		ID3D11SamplerState* _linearClampSampler = nullptr;

		// TextureCubeArray of point-light shadow maps. 6 * kMaxShadowedPoints
		// faces total, each kPointShadowFaceSize^2 R32_FLOAT. Each frame we
		// GPU-copy from the per-face PointLight shadow maps into this array's
		// slots, then bind it once as a TextureCubeArray SRV. Hardware does
		// face selection from the sample direction in the shader, so the
		// shader just samples `array.Sample(dir, slot)` with no per-face
		// switch logic.
		class ITexture2D* _pointShadowCubeArray = nullptr;
		// Lazily-created STAGING texture for the CPU readback diagnostic
		// (pointShadowBiasMetres <= -9.5). One face worth of R32_FLOAT,
		// CPU-readable; reused across faces/frames. Debug-only cost.
		ID3D11Texture2D* _readbackStaging = nullptr;

		std::shared_ptr<IShader> _scatterShader;
		std::shared_ptr<IShader> _integrateShader;

		// Previous-frame view-projection matrix for history reprojection.
		// Each Update writes its currentViewProj here AFTER the dispatch
		// so the next Update can use it as the prev-frame matrix in the
		// reprojection lookup. _hasPrevViewProj guards the first frame
		// (when there's no valid history) so the integrate shader knows
		// to fall back to "pure current" without blending zero history.
		math::Matrix _prevViewProj = math::Matrix::Identity;
		math::Vector3 _prevEyePos = math::Vector3(0.0f, 0.0f, 0.0f);
		bool _hasPrevViewProj = false;

		bool _resourcesReady = false;
	};
}
