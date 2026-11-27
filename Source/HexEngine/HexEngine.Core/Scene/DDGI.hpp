#pragma once

#include "../Required.hpp"
#include "RtScene.hpp"
#include <memory>

namespace HexEngine
{
	class Scene;
	class Camera;
	class GBuffer;
	class ITexture2D;
	class IShader;

	/**
	 * @brief DDGI diffuse global illumination (probe volumes updated by
	 * compute ray tracing). Replacement for the voxel DiffuseGI; behind
	 * r_ddgi (default off) until accepted - see docs/rendering/ddgi-plan.md.
	 *
	 * Per main-camera frame (Render):
	 *   1. RtScene::Update - BLAS/TLAS refresh on the CPU.
	 *   2. For each cascade: trace kRaysPerProbe rays for 1/period of its
	 *      probes (DDGITrace), blend into irradiance (DDGIBlendIrradiance)
	 *      then depth moments + relocation/state (DDGIBlendDepth).
	 *   3. Apply (DDGIApply): albedo * probe irradiance added to beauty.
	 * r_rtDebug draws the ray-tracer validation view instead.
	 *
	 * While Active(), SceneRenderer hands off all flat ambient and the sky SH
	 * fill in the deferred pass and skips the voxel GI.
	 */
	class HEX_API DDGI
	{
	public:
		static constexpr uint32_t kCascades = 3;
		static constexpr uint32_t kRaysPerProbe = 128; // mirrors kDdgiRaysPerProbe (DDGICommon.shader)
		static constexpr uint32_t kIrrTile = 8;
		static constexpr uint32_t kDepthTile = 16;

		~DDGI();

		/** Update probes and composite into beauty. Main camera only. */
		void Render(Scene* scene, Camera* camera, const GBuffer& gbuffer, ITexture2D* beautyTarget, ITexture2D* skySH);

		/** True when DDGI owns indirect diffuse this frame (r_ddgi and ready). */
		bool IsActive() const;
		/** True when r_rtDebug wants the ray tracer (even with r_ddgi off). */
		static bool WantsRayTracing();

		/** SSR miss fallback: bind probes (b8, t56-t64) when active, else null b8. */
		void BindForReflectionPS() const;
		// Same probe SRVs (t56-64) + constants (b8) for a compute pass that
		// samples the probes (froxel fog); b8 null while inactive. UnbindCS
		// clears them so DDGI's own UAV passes never see them still bound.
		void BindForCS() const;
		void UnbindCS() const;

		void Destroy();
		RtScene& GetRtScene() { return _rt; }

	private:
		struct CascadeConstants
		{
			int32_t gridOrigin[4];
			int32_t dims[4];
			float spacing[4];
		};
		struct Constants
		{
			CascadeConstants cascade[kCascades];
			float rayRotation0[4];
			float rayRotation1[4];
			float rayRotation2[4];
			uint32_t update[4];
			float params0[4];
			float params1[4];
			float params2[4];
			float params3[4];
			float ground[4];
			float groundAlbedo[4];
			float ssgi[4];
			float params4[4]; // x: apply sample jitter (probe spacings), y: froxel fog left indoors, z: froxel DDGI range (m)
		};
		struct GpuLight
		{
			float positionRange[4];
			float directionCone[4];
			float colourIntensity[4];
			float params[4];
		};
		struct Cascade
		{
			ID3D11Buffer* irradiance = nullptr;
			ID3D11ShaderResourceView* irradianceSrv = nullptr;
			ID3D11UnorderedAccessView* irradianceUav = nullptr;
			ID3D11Buffer* depth = nullptr;
			ID3D11ShaderResourceView* depthSrv = nullptr;
			ID3D11UnorderedAccessView* depthUav = nullptr;
			ID3D11Buffer* state = nullptr;
			ID3D11ShaderResourceView* stateSrv = nullptr;
			ID3D11UnorderedAccessView* stateUav = nullptr;
			// Per-probe change-detector state (float2: fast running mean of
			// the probe's luminance, last z score) - DDGIBlendIrradiance u1.
			ID3D11Buffer* stats = nullptr;
			ID3D11ShaderResourceView* statsSrv = nullptr;
			ID3D11UnorderedAccessView* statsUav = nullptr;
			int32_t dims[3] = {};
			int32_t origin[3] = {};
			float spacing = 1.0f;
			uint32_t probeCount = 0;
		};

		bool EnsureResources();
		bool CreateStructured(uint32_t elements, uint32_t stride, ID3D11Buffer*& buffer, ID3D11ShaderResourceView*& srv, ID3D11UnorderedAccessView*& uav);
		void ReleaseCascades();
		void UpdateLights(Scene* scene, const math::Vector3& center, float radius);
		void FillConstants(uint32_t cascade, uint32_t period, uint32_t phase, uint32_t groups);
		void UpdateProbes(Scene* scene, Camera* camera, ITexture2D* skySH);
		void Apply(const GBuffer& gbuffer, ITexture2D* beautyTarget);
		// Screen-space AO (the HBAO+ plugin) rendered into _aoRT for the
		// indirect term only; returns the strength to apply (0 = none).
		float RenderAmbientOcclusion(Camera* camera, const GBuffer& gbuffer, ITexture2D* beautyTarget);
		void DrawRtDebug(const GBuffer& gbuffer, ITexture2D* beautyTarget);
		ID3D11ShaderResourceView* GetSkyShSrv(ITexture2D* skySH);

		RtScene _rt;
		Cascade _cascades[kCascades];
		int32_t _builtDims[3] = {};
		ID3D11Buffer* _rays = nullptr;
		ID3D11ShaderResourceView* _raysSrv = nullptr;
		ID3D11UnorderedAccessView* _raysUav = nullptr;
		uint32_t _rayCapacityProbes = 0;
		ID3D11Buffer* _constantBuffer = nullptr;
		ID3D11Buffer* _lightBuffer = nullptr;
		ID3D11ShaderResourceView* _lightSrv = nullptr;
		uint32_t _lightCapacity = 0;
		uint32_t _lightCount = 0;
		ID3D11SamplerState* _pointSampler = nullptr;
		ID3D11ShaderResourceView* _skyShSrv = nullptr;
		const void* _skyShIdentity = nullptr;

		std::shared_ptr<IShader> _traceShader;
		std::shared_ptr<IShader> _blendIrrShader;
		std::shared_ptr<IShader> _blendDepthShader;
		std::shared_ptr<IShader> _applyShader;
		std::shared_ptr<IShader> _ssgiShader;
		std::shared_ptr<IShader> _ssgiBlurShader;
		std::shared_ptr<IShader> _rtDebugShader;
		ID3D11Buffer* _rtDebugConstants = nullptr;
		// Half-res near-field SSGI gather (DDGISSGI), upsampled by Apply.
		ITexture2D* _ssgiRT = nullptr;
		// Bilateral-denoised copy of _ssgiRT (DDGISSGIBlur); what Apply reads.
		ITexture2D* _ssgiBlurRT = nullptr;
		// Contact AO for the probe term (white-cleared, multiplied by HBAO+).
		ITexture2D* _aoRT = nullptr;
		float _aoStrength = 0.0f;

		Constants _constants = {};
		uint64_t _frame = 0;
		float _groundHeight = 0.0f;
		bool _groundValid = false;
		bool _ready = false;
		bool _resourcesFailed = false;
	};
}
