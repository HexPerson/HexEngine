
#pragma once

#include "../Required.hpp"
#include "Scene.hpp"
#include "../Graphics/GBuffer.hpp"
#include "../Graphics/BlurEffect.hpp"
#include "../Graphics/Bloom.hpp"
#include "../Graphics/AutoExposure.hpp"
#include "DiffuseGI.hpp"
#include "../Graphics/ClusteredLighting.hpp"
#include "GpuVisibilityCulling.hpp"
#include "../Graphics/TAA.hpp"
#include "../Graphics/IDenoiserProvider.hpp"

namespace HexEngine
{
	class Camera;
	class ITexture3D;
	class IConstantBuffer;
	class IVertexBuffer;
	class IIndexBuffer;

	class SceneRenderer
	{
	public:
		SceneRenderer();

		void Create();
		void Resize(int32_t width, int32_t height);
		void Destroy();

		void RenderScene(Scene* scene, Camera* camera, SceneFlags flags);

		const GBuffer* GetGBuffer();
		const Light* GetCurrentShadowCaster();
		const ShadowMap* GetCurrentShadowMap();
		ITexture2D* GetBeautyTexture() const;
		std::shared_ptr<ITexture2D> GetNoiseTexture() const { return _blueNoise;}
		const std::vector<Light*>& GetShadowCasters() const;
		void ClearShadowCasters();
		void RemoveShadowCaster(Light* light);

		void RenderOverlays(SceneFlags flags, ITexture2D* beauty, ITexture2D* renderTarget);
		GpuVisibilityCulling* GetGpuVisibilityCulling() { return &_gpuVisibilityCulling; }
		// Exposed so DiffuseGIAOProvider (which derives AO from the GI volume's
		// trace alpha) can grab the resolved RT without going through the
		// scene-renderer's private members.
		DiffuseGI* GetDiffuseGI() { return &_diffuseGi; }

	private:
		void SetupPerFrameBuffer(
			const math::Matrix& viewMatrix,
			const math::Matrix& projectionMatrix,
			const math::Matrix& viewMatrixPrev,
			const math::Matrix& projectionMatrixPrev,
			int32_t numCascades,
			const math::Vector3& lightDir,
			const math::Viewport& viewport,
			int passIdx,
			float lightMultiplier = 1.0f,
			bool forceCascade = false);
		void SetupPerShadowCasterBuffer(Light* shadowCaster, bool forceCascade, int32_t passIdx, int32_t lightIndex, int32_t numSamples, float coneSize);
		void RenderShadowMaps(Light* shadowCaster);

		void RenderOpaque();
		//void RenderWater();
		void RenderTransparent();
		void RenderPostProcessing(SceneFlags flags);
		void CollectShadowCasters();
		void RenderFog();
		void RenderVolumetricLighting();
		void RenderVolumetricClouds();
		void RenderSSR();
		// True when RenderSSR will actually run for the current scene/camera, which
		// is also the condition for handing environment specular to the SSR resolve
		// instead of the deferred pass. The two MUST be driven by the same
		// predicate: if the deferred pass drops the term for a frame where no
		// resolve runs, every glossy surface loses its environment response, and if
		// it keeps the term for a frame where the resolve does run, the two stack
		// again - which is the bug this whole path exists to fix.
		bool WillRenderSSR() const;
		// The above, additionally gated on r_iblComposeSSR. Uploaded to the shaders
		// as _iblComposeParams.x.
		bool ShouldComposeEnvSpecularInResolve() const;
		// IBL step 1: prefilter the Hillaire sky-view LUT into the octahedral
		// roughness atlas (_iblSkyEnvMap) consumed by the deferred IBL term.
		// See EnvMapCommon.shader for the atlas layout rationale.
		void RenderSkyEnvMap();
		// IBL step 2: convert + prefilter any reflection probe whose six capture
		// faces just completed into its own octahedral atlas (ProbeEnvMap.shader).
		// Runs at most one probe's prefilter per call - it is a bake, not a
		// per-frame cost.
		void RenderProbeEnvMaps();
		// Interaction look-at outline: jump-flood SDF glow around the focused
		// interactable's static mesh, composited additively into beauty. No-op
		// when nothing is focused (in-game look-at only).
		void RenderOutlineGlow();

		void CreateShaders();
		void CreateRenderTargets(int32_t width, int32_t height);
		void RenderLights();
		void RenderPointLights();
		void RenderSpotLights();
		void RenderDirectionalLights();
		void RenderDiffuseGI();
		// Two-pass separable screen-space subsurface scattering. Reads the beauty
		// RT, the features gbuffer (model-id channel gates the work), and the
		// normal/depth target (depth-aware kernel weighting); writes back into
		// beauty after both passes. Runs early in post (after lighting+GI, before
		// fog/clouds) so the SSS scatter happens in linear HDR space and fog/cloud
		// volumetrics aren't blurred through skin.
		void RenderSubsurfaceScattering();
		// Aerial perspective apply. Reads AtmosphereLUTs' 32^3 froxel volume,
		// hazes distant geometry's beauty toward atmospheric scattering colour
		// so the silhouette of distant terrain/buildings smoothly fades into
		// the LUT sky behind it instead of contrasting sharply. Runs after
		// SSS and before transparency / fog so transparents still get clean
		// per-pixel colour and the existing artistic fog stack composes on top.
		void RenderAerialPerspective();
		// Phase D apply. Samples the volumetric integration volume at
		// (uv, depth), composites beauty * transmittance + inscatter.
		// Replaces the legacy per-pixel RenderVolumetricLighting when
		// VolumetricScattering is available; otherwise the old march
		// runs as before for backward compatibility.
		void RenderVolumetricScattering();
		// Bokeh depth-of-field. Single-pass 32-tap Vogel disk gather sized by
		// per-pixel circle-of-confusion. Runs after tonemap so the bokeh discs
		// reflect post-tonemap colour - matters because pre-tonemap HDR
		// highlights would saturate the gathered buckets and lose the "ball"
		// shape on bright sources. Gated by r_dof.
		void RenderBokehDoF();
		// Deferred decal pass. Runs after the GBuffer opaque fill and before the
		// beauty copy / lighting pass so subsequent shading sees the decal-modified
		// surface (puddles get smooth roughness fed into lighting + reflections,
		// blood gets dark albedo lit normally, etc.). v1 writes only diffuse + mat;
		// normal/position/velocity/features are untouched. See Decal.shader for the
		// per-pixel projection details and DecalComponent for placement.
		void RenderDecals();
		void RenderVignette();
		void SetStreamlineConstants();

		// Gathers the closest point + spot lights in the scene and uploads them to the
		// forward-lights constant buffer (PS slot b7). Used by the transparency pass so glass /
		// alpha-blended surfaces can sample the same dynamic lighting as opaque surfaces.
		void SetupForwardLights();

	public:
		static constexpr uint32_t kMaxForwardPointLights = 16;
		static constexpr uint32_t kMaxForwardSpotLights = 16;

		struct ForwardLightConstants
		{
			math::Vector4 countsAndParams = math::Vector4::Zero;       // x=pointCount, y=spotCount
			math::Vector4 reserved = math::Vector4::Zero;
			math::Vector4 pointPosRadius[kMaxForwardPointLights];
			math::Vector4 pointColorStrength[kMaxForwardPointLights];
			math::Vector4 spotPosRadius[kMaxForwardSpotLights];
			// spotDirCone[i] = (dir.xyz, cos(outerHalfAngle)).
			math::Vector4 spotDirCone[kMaxForwardSpotLights];
			math::Vector4 spotColorStrength[kMaxForwardSpotLights];
			// spotInnerCone[i].x = cos(innerHalfAngle). The shader does
			// smoothstep(cosOuter, cosInner, cosTheta) for a soft, energy-conserving
			// cone falloff. Stored as a separate array (rather than packed into a
			// spare slot of an existing vec4) so adding it doesn't reshuffle the
			// existing fields and break older shader bindings.
			math::Vector4 spotInnerCone[kMaxForwardSpotLights];
		};

	private:
		Scene* _currentScene = nullptr;
		Camera* _currentCamera = nullptr;
		Entity* _cameraEntity = nullptr;

		std::recursive_mutex _lock;

		std::vector<Light*> _shadowCasters;
		Light* _currentShadowCasterForComposition = nullptr;
		ShadowMap* _currentShadowMapForComposition = nullptr;

		// Cached by SetupPerFrameBuffer's sunset/night modulation block each
		// frame, consumed by the froxel volumetric update (which runs later,
		// post-gbuffer). Keeps the froxel medium's extinction + ambient
		// inscatter EXACTLY in sync with the night-dimmed cbuffer values the
		// apply shader's beyond-range fog continuation reads - a mismatch
		// draws a density/colour ring at the volume's 128m far plane.
		float _volumetricFogNightDim = 1.0f;
		math::Vector3 _volumetricAmbientNight = math::Vector3(0.1f, 0.1f, 0.1f);

		GBuffer _gbuffer;
		GBuffer _gbufferTransparency;

		// post processing
		ITexture2D* _beautyRT = nullptr;
		ITexture2D* _shadowMapsRT = nullptr;
		ITexture2D* _shadowMapsAccumulator = nullptr;
		ITexture2D* _waterAccumulationRT = nullptr;
		ITexture2D* _particleRT = nullptr;
		//ITexture2D* _waterDSV = nullptr;
		ITexture2D* _fogBuffer = nullptr;
		ITexture2D* _volumetricLightingBuffer = nullptr;
		ITexture2D* _cloudsBuffer = nullptr;
		ITexture2D* _atmosphereRT = nullptr;
		ITexture2D* _lightAccumulationBuffer = nullptr;
		ITexture2D* _pointLightBuffer = nullptr;
		ITexture2D* _ssrDiffuseTexture = nullptr;
		ITexture2D* _ssrDiffuseHitInfo = nullptr;
		ITexture2D* _ssrTexture = nullptr;
		ITexture2D* _ssrHitInfo = nullptr;
		ITexture2D* _waterRT = nullptr;
		std::shared_ptr<ITexture2D> _blueNoise;
		ITexture2D* _dlssTarget = nullptr;
		std::shared_ptr<IShader> _compositionShader;
		std::shared_ptr<IShader> _fxaa;
		std::shared_ptr<IShader> _fogEffect;
		std::shared_ptr<IShader> _bilateralUpsample;
		std::shared_ptr<IShader> _pointLightShader;
		std::shared_ptr<IShader> _spotLightShader;
		std::shared_ptr<IShader> _ssrShader;
		std::shared_ptr<IShader> _vignetteShader;
		std::shared_ptr<IShader> _chromaticAberrationShader;
		std::shared_ptr<IShader> _colourGradingShader;
		std::shared_ptr<IShader> _tonemapShader;
		std::shared_ptr<IShader> _hdrOutputShader;
		std::shared_ptr<IShader> _basicDenoise;
		std::shared_ptr<IShader> _volumetricLighting;
		std::shared_ptr<IShader> _volumetricClouds;
		std::shared_ptr<IShader> _ssrResolve;
		std::shared_ptr<IShader> _waterBlitEffect;
		std::shared_ptr<IShader> _fullScreenQuadShader;

		// IBL: prefiltered sky environment. Octahedral roughness atlas
		// (128 x 128*rows, RGBA16F) regenerated each frame from the sky-view
		// LUT by RenderSkyEnvMap(). Fixed size - not part of the resize path.
		ITexture2D* _iblSkyEnvMap = nullptr;
		// P1-C: 1x9 RGBA16F of SH irradiance coefficients projected from the sky
		// atlas. Replaces the flat ambient constant for diffuse environment light.
		ITexture2D* _iblSkySH = nullptr;
		std::shared_ptr<IShader> _iblSkyEnvShader;
		std::shared_ptr<IShader> _probeEnvShader;
		std::shared_ptr<IShader> _envSHShader;
		// P1-B: split-sum DFG table (RG = F0 scale/bias, B = single-scatter
		// energy for multi-scatter compensation). View-independent, so it is
		// generated once and never regenerated.
		ITexture2D* _dfgLut = nullptr;
		std::shared_ptr<IShader> _dfgLutShader;
		bool _dfgLutGenerated = false;
		// This frame's selected reflection probe (set in SetupPerFrameBuffer,
		// consumed by the deferred t16 bind). Raw pointer valid for the frame only.
		class ReflectionProbeComponent* _activeProbe = nullptr;
		// Second-nearest probe, bound at t17 so the shader can cross-fade.
		class ReflectionProbeComponent* _activeProbe2 = nullptr;

		// Weather overcast tint, cached from the sky-render setup so the
		// prefiltered environment atlas can apply the SAME lerp the sky sphere
		// does. Without it a reflection shows Hillaire's clear blue under a grey
		// storm sky - the LUT the atlas prefilters never sees this tint.
		math::Vector3 _skyOvercastColour = math::Vector3(1.0f, 1.0f, 1.0f);
		float         _skyOvercastAmount = 0.0f;

		// Interaction outline glow (jump-flood SDF). _outlineJfaA/B ping-pong the
		// nearest-seed coordinate field (RG32F, pixel coords); _outlineGlowRT
		// holds the composited glow ring that's additively blended into beauty.
		ITexture2D* _outlineJfaA = nullptr;
		ITexture2D* _outlineJfaB = nullptr;
		ITexture2D* _outlineGlowRT = nullptr;
		IConstantBuffer* _outlineParamsBuffer = nullptr;
		std::shared_ptr<IShader> _outlineSeedShader;
		std::shared_ptr<IShader> _outlineJfaShader;
		std::shared_ptr<IShader> _outlineCompositeShader;
		// Screen-space subsurface scattering (Jorge Jimenez separable). Run twice
		// per frame as a two-pass post (horizontal then vertical), gated by the
		// features gbuffer's material-model channel.
		std::shared_ptr<IShader> _subsurfaceShader;
		IConstantBuffer* _subsurfaceParamsBuffer = nullptr;
		ITexture2D* _subsurfaceIntermediateRT = nullptr;
		// Bokeh DoF. Single fullscreen PS that reads beauty + normal/depth and
		// writes back into beauty in-place via a scratch RT swap (we reuse the
		// SSS intermediate RT for the scratch since both run on the same beauty
		// format and never overlap in the post chain).
		std::shared_ptr<IShader> _bokehDoFShader;
		IConstantBuffer* _bokehDoFParamsBuffer = nullptr;
		// Aerial perspective apply. Reads beauty + gbuffer normal/diff, samples
		// AtmosphereLUTs' 32^3 froxel volume, composites distance haze into
		// the beauty RT via a scratch RT swap (reuses _subsurfaceIntermediateRT
		// like Bokeh DoF does, since AP runs after SSS in the post chain).
		std::shared_ptr<IShader> _aerialPerspectiveApplyShader;
		// Phase D volumetric scattering apply. Samples the integration
		// volume produced by VolumetricScattering at (uv, depth) and
		// composites the scatter+transmittance into beauty. Replaces the
		// legacy per-pixel RenderVolumetricLighting when the new system
		// is available; otherwise the old path still runs.
		std::shared_ptr<IShader> _volumetricScatterApplyShader;

		// Decal pass. The position RT is bound for write during GBuffer fill, so we
		// snapshot it into _decalPositionCopy each frame before the decal pass and
		// bind that as the SRV the decal PS samples. The cube VB/IB is a single
		// shared unit cube used by every decal (the decal renderer just rebinds
		// per-decal world matrix + constants).
		std::shared_ptr<IShader> _decalShader;
		IConstantBuffer* _decalConstantsBuffer = nullptr;
		ITexture2D* _decalPositionCopy = nullptr;
		IVertexBuffer* _decalCubeVB = nullptr;
		IIndexBuffer* _decalCubeIB = nullptr;

		// Auto-puddles. A single fullscreen PS that paints procedural puddles
		// into the GBuffer where surfaces are flat + a noise mask + the weather
		// system's puddleAmount line up. Shares the decal pass's render-target
		// bindings (diff + mat) so it can run inline at the end of RenderDecals.
		std::shared_ptr<IShader> _autoPuddlesShader;
		IConstantBuffer* _autoPuddlesConstantsBuffer = nullptr;
		// Dedicated fullscreen-quad VB/IB. We don't use GuiRenderer's
		// FullScreenTexturedQuad because that path only writes RT0; the auto-
		// puddle pass needs MRT (diff + mat) in a single draw.
		IVertexBuffer* _autoPuddlesQuadVB = nullptr;
		IIndexBuffer*  _autoPuddlesQuadIB = nullptr;

		Bloom* _bloomEffect = nullptr;
		AutoExposure _autoExposure;

		ITexture3D* _cloudShapeNoise = nullptr;
		ITexture3D* _cloudDetailNoise = nullptr;
		IConstantBuffer* _cloudConstantBuffer = nullptr;
		IConstantBuffer* _forwardLightsBuffer = nullptr;
		std::shared_ptr<Mesh> _sphereMesh = nullptr;
		Entity* _sphereEntity = nullptr;
		std::shared_ptr<Material> _pointLightMaterial;
		std::shared_ptr<Material> _spotLightMaterial;

		TAA _taa;

		// Set when TAA or DLSS resolved this frame, so the overlay chain can skip FXAA
		// rather than stacking a second, spatial AA pass on top of a temporal one.
		bool _temporalAaAppliedThisFrame = false;

		// Cut detection for TAA history invalidation - see RenderScene. Raw observer
		// pointers, only ever compared for identity, never dereferenced.
		Camera* _taaHistoryCamera = nullptr;
		Scene* _taaHistoryScene = nullptr;
		math::Vector3 _taaHistoryCameraPos = math::Vector3(0.0f, 0.0f, 0.0f);
		DiffuseGI _diffuseGi;
		// Phase 2: clustered light culling (list build + heatmap; no consumer yet).
		ClusteredLighting _clusteredLights;
		std::shared_ptr<IShader> _clusterApplyShader;
		GpuVisibilityCulling _gpuVisibilityCulling;

		ITexture2D* _ssrHistory = nullptr;		
		ITexture2D* _ssrResolved = nullptr;

		DenoiserFrameData _denoiseFD;
	};
}
