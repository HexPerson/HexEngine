#pragma once

#include "../Required.hpp"
#include "../Math/FloatMath.hpp"

struct ID3D11UnorderedAccessView;
struct ID3D11SamplerState;

namespace HexEngine
{
	class IConstantBuffer;
	class IShader;
	class ITexture2D;
	class ITexture3D;

	/**
	 * @brief Hillaire 2020 atmosphere LUT subsystem.
	 *
	 * Owns the precomputed/per-frame lookup tables that drive sky shading,
	 * aerial perspective, and (in later phases) light scattering through
	 * the atmosphere. Replaces the per-pixel analytic integration in
	 * AtmospherePhysical.shader for everything that touches sky-derived
	 * lighting.
	 *
	 * Phase A (this revision) sets up the scaffold: the param cbuffer, a
	 * shell for Create/Destroy/Update lifecycle, and getters. The actual
	 * LUT textures and compute dispatches land in Phase B (sky LUTs) and
	 * Phase C (aerial perspective volume).
	 *
	 * Owned by IEnvironment via _atmosphereLUTs. SceneRenderer calls
	 * Update() once per frame after the camera transform settles but
	 * before any pass that samples the LUTs.
	 */
	class HEX_API AtmosphereLUTs
	{
	public:
		// Atmosphere parameters as they reach the shader. Kept in sync with
		// AtmosphereCommon.shader's hard-coded constants - those values are
		// duplicated here so a future "stylised atmosphere" override path
		// can drive them from C++ without re-touching every shader. For now
		// callers should treat this as opaque; defaults match the shader.
		struct Params
		{
			float groundRadiusMM   = 6.360f;
			float topRadiusMM      = 6.460f;
			float rayleighScaleHeightMM = 0.008f;
			float mieScaleHeightMM = 0.0012f;

			float ozoneCentreMM    = 0.025f;
			float ozoneHalfWidthMM = 0.015f;
			float _pad0            = 0.0f;
			float _pad1            = 0.0f;

			math::Vector3 rayleighScatteringPerMM = math::Vector3(5.802f, 13.558f, 33.1f);
			float mieScatteringPerMM              = 3.996f;

			float mieExtinctionPerMM = 4.40f;
			// Mie anisotropy for LUT generation. Physical atmospheric value
			// is ~0.8 but at that strength the phase function peak around
			// the sun (~150x within 5deg) outruns the 192x108 SkyView LUT's
			// per-texel resolution and produces visible concentric rings
			// when bilinearly sampled. Sky-disk, aureole and halo are added
			// analytically by SkySphere.shader on top of the LUT result, so
			// the LUT itself doesn't need to capture the sharp peak - we
			// dampen to g=0.5 to keep the LUT's Mie contribution smooth.
			float miePhaseG          = 0.50f;
			float _pad2              = 0.0f;
			float _pad3              = 0.0f;

			math::Vector3 ozoneAbsorptionPerMM = math::Vector3(0.650f, 1.881f, 0.085f);
			float _pad4 = 0.0f;
		};

		AtmosphereLUTs();
		~AtmosphereLUTs();

		// Allocate the param cbuffer and load any always-resident shaders.
		// LUT textures are allocated lazily on first Update() so we can
		// honour the engine's eventual screen-resolution-dependent sizing
		// for SkyView and aerial-perspective targets.
		bool Create();
		void Destroy();

		// Re-upload params to the GPU cbuffer. Call when the
		// user-facing atmosphere tunables change; cheap, no LUT recompute.
		void SetParams(const Params& params);
		const Params& GetParams() const { return _params; }

		// Per-frame entry point.
		//   cameraWorldY  : camera Y altitude in engine metres (world space).
		//                   Lifted into atmospheric coordinates via the
		//                   groundRadiusMM + Y*1e-6 convention.
		//   sunDirection  : world-space direction FROM the surface TO the
		//                   sun. Need not be normalised; we re-normalise.
		//                   When y<=0 (sun below horizon) the sky LUT still
		//                   resolves (twilight) but transmittance bottoms out.
		//   sunIntensity  : scalar multiplier baked into the sky LUT.
		// Phase B: dispatches TransmittanceLUT (only when params change),
		// MultiScatteringLUT (per-frame), SkyViewLUT (per-frame).
		// Phase C will add the AerialPerspective volume here too.
		void Update(float cameraWorldY, const math::Vector3& sunDirection, float sunIntensity);

		// Post-LUT sky tint - the Hillaire model can only produce clear-sky
		// looks (blue dome + sunset). Weather-driven overcast / storm /
		// rain skies come from cloud cover in reality, not atmospheric
		// scattering. Sky shader samples the LUT and then lerps toward
		// `overcastColor` by `overcastAmount` to fake the look.
		//   overcastColor : rgb tint to blend the LUT result toward.
		//                   Bright white-grey for storms, dismal grey for
		//                   overcast, untouched (irrelevant) when amount=0.
		//   overcastAmount: 0 = pure LUT, 1 = pure overcast tint.
		// Independent of Update() so weather can change between frames
		// without re-dispatching the LUTs.
		void SetSkyRenderParams(const math::Vector3& overcastColor, float overcastAmount);
		IConstantBuffer* GetSkyRenderCBuffer() const { return _skyRenderCBuffer; }

		// Getters for downstream samplers. Null until the corresponding
		// phase ships - callers should null-check and fall back to the
		// analytic atmosphere path when missing.
		ITexture2D*       GetTransmittanceLUT() const { return _transmittanceLUT; }
		ITexture2D*       GetMultiScatteringLUT() const { return _multiScatteringLUT; }
		ITexture2D*       GetSkyViewLUT() const { return _skyViewLUT; }
		ITexture3D*       GetAerialPerspectiveVolume() const { return _aerialPerspectiveVolume; }
		IConstantBuffer*  GetAtmosphereCBuffer() const { return _atmosphereCBuffer; }

	private:
		// Lazy allocation. Compute UAVs are created on demand and cached
		// alongside the texture pointer; both Texture and UAV lifetimes
		// are owned by this class.
		bool EnsureResources();
		void ReleaseResources();

		Params _params;
		bool   _paramsDirty = true;            // set on Create() and SetParams(), cleared after the cbuffer write
		bool   _needsTransmittanceRebuild = true; // recompute the precomputed LUT on next Update()
		bool   _resourcesReady = false;        // textures + UAVs + shaders all created

		IConstantBuffer* _atmosphereCBuffer = nullptr;
		IConstantBuffer* _skyViewCBuffer    = nullptr; // per-frame camera + sun
		IConstantBuffer* _skyRenderCBuffer  = nullptr; // post-LUT overcast tint, consumed by SkySphere.shader

		// Sky LUTs (created in EnsureResources).
		ITexture2D* _transmittanceLUT   = nullptr; // 256x64,    RGBA16F
		ITexture2D* _multiScatteringLUT = nullptr; // 32x32,     RGBA16F
		ITexture2D* _skyViewLUT         = nullptr; // 192x108,   RGBA16F

		// Raw UAVs for the compute writes. No abstracted ITexture2D UAV
		// getter in the engine today; created manually from the texture's
		// native D3D11 resource and released by ReleaseResources.
		ID3D11UnorderedAccessView* _transmittanceUav   = nullptr;
		ID3D11UnorderedAccessView* _multiScatteringUav = nullptr;
		ID3D11UnorderedAccessView* _skyViewUav         = nullptr;
		// Linear-clamp sampler bound at s4 during LUT dispatches. The
		// engine's global samplers aren't auto-bound to the CS stage, so
		// we own a private one to avoid the "Sampler not set at Slot 4"
		// D3D11 validation warning and the undefined sampling behaviour
		// that would otherwise apply.
		ID3D11SamplerState* _linearClampSampler = nullptr;

		// Aerial perspective volume (32^3 RGBA16F). Dispatched every frame
		// alongside the sky LUTs; sampled by the AP apply pass to fade
		// distant geometry into atmospheric scattering.
		ITexture3D* _aerialPerspectiveVolume = nullptr;
		ID3D11UnorderedAccessView* _aerialPerspectiveUav = nullptr;

		std::shared_ptr<IShader> _transmittanceShader;
		std::shared_ptr<IShader> _multiScatteringShader;
		std::shared_ptr<IShader> _skyViewShader;
		std::shared_ptr<IShader> _aerialPerspectiveShader;
	};
}
