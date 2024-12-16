
#pragma once

#include "../Required.hpp"
#include "Scene.hpp"
#include "../Graphics/GBuffer.hpp"
#include "../Graphics/BlurEffect.hpp"
#include "../Graphics/Bloom.hpp"
#include "CloudVolume.hpp"
#include "../Graphics/TAA.hpp"
#include "../Graphics/IDenoiserProvider.hpp"

namespace HexEngine
{
	class Camera;

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
		ITexture2D* GetNoiseTexture() const {return _blueNoise;}
		const std::vector<Light*>& GetShadowCasters() const;
		void ClearShadowCasters();
		void RemoveShadowCaster(Light* light);

		void RenderOverlays(SceneFlags flags, ITexture2D* beauty, ITexture2D* renderTarget);

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

		void CreateShaders();
		void CreateRenderTargets(int32_t width, int32_t height);
		void RenderLights();
		void RenderPointLights();
		void RenderSpotLights();
		void RenderDirectionalLights();
		void RenderVignette();
		void SetStreamlineConstants();
		

	private:
		Scene* _currentScene = nullptr;
		Camera* _currentCamera = nullptr;
		Entity* _cameraEntity = nullptr;

		std::recursive_mutex _lock;

		std::vector<Light*> _shadowCasters;
		Light* _currentShadowCasterForComposition = nullptr;
		ShadowMap* _currentShadowMapForComposition = nullptr;

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
		ITexture2D* _atmosphereRT = nullptr;
		ITexture2D* _lightAccumulationBuffer = nullptr;
		ITexture2D* _pointLightBuffer = nullptr;
		ITexture2D* _ssrTexture = nullptr;
		ITexture2D* _ssrHitInfo = nullptr;
		ITexture2D* _blueNoise = nullptr;
		ITexture2D* _dlssTarget = nullptr;
		IShader* _compositionShader = nullptr;
		//IShader* _shadowMaskShader = nullptr;
		IShader* _fxaa = nullptr;
		IShader* _fogEffect = nullptr;
		IShader* _bilateralUpsample = nullptr;
		IShader* _pointLightShader = nullptr;
		IShader* _spotLightShader = nullptr;
		IShader* _ssrShader = nullptr;
		IShader* _vignetteShader = nullptr;
		IShader* _chromaticAberrationShader = nullptr;
		IShader* _colourGradingShader = nullptr;
		IShader* _tonemapShader = nullptr;
		IShader* _basicDenoise = nullptr;

		IShader* _volumetricLighting = nullptr;
		//BlurEffect* _volumetricBlur = nullptr;
		//BlurEffect* _waterBlur = nullptr;
		//BlurEffect* _ssrBlur = nullptr;
		//ITexture2D* _blueNoise = nullptr;

		Bloom* _bloomEffect = nullptr;

		//CloudVolume* _clouds = nullptr;
		Model* _sphereModel = nullptr;
		Entity* _sphereEntity = nullptr;
		Material* _pointLightMaterial = nullptr;
		Material* _spotLightMaterial = nullptr;

		TAA _taa;

		ITexture2D* _ssrHistory = nullptr;
		IShader* _ssrResolve = nullptr;
		ITexture2D* _ssrResolved = nullptr;

		//TAA _ssrResolver;

		DenoiserFrameData _denoiseFD;
	};
}
