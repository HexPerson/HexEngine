
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
		ITexture2D* GetBeautyTexture() const;
		std::shared_ptr<ITexture2D> GetNoiseTexture() const { return _blueNoise;}
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
		std::shared_ptr<IShader> _ssrResolve;
		std::shared_ptr<IShader> _waterBlitEffect;
		std::shared_ptr<IShader> _fullScreenQuadShader;

		//BlurEffect* _volumetricBlur = nullptr;
		//BlurEffect* _waterBlur = nullptr;
		//BlurEffect* _ssrBlur = nullptr;
		//ITexture2D* _blueNoise = nullptr;

		Bloom* _bloomEffect = nullptr;

		//CloudVolume* _clouds = nullptr;
		std::shared_ptr<Mesh> _sphereMesh = nullptr;
		Entity* _sphereEntity = nullptr;
		std::shared_ptr<Material> _pointLightMaterial;
		std::shared_ptr<Material> _spotLightMaterial;

		TAA _taa;

		ITexture2D* _ssrHistory = nullptr;		
		ITexture2D* _ssrResolved = nullptr;

		//TAA _ssrResolver;

		DenoiserFrameData _denoiseFD;
	};
}
