
#include "SceneRenderer.hpp"
#include "../HexEngine.hpp"
#include "../Entity/Component/SpotLight.hpp"
#include "../Entity/Component/PointLight.hpp"

namespace HexEngine
{
	const int32_t MaxShadowCasters = 4;

	/*Cvar env_zenithExponent("env_zenithExponent", "Atmospheric zenith component", 4.6f, 1.0f, 10.0f);
	Cvar env_anisotropicIntensity("env_anisotropicIntensity", "Atmospheric scattering intensity", 0.0f, 0.0f, 10.0f);
	Cvar env_density("env_density", "The density of the atmosphere", 0.12f, 0.0f, 4.0f);*/

	HVar env_zenithExponent("env_zenithExponent", "Atmospheric zenith component", 4.12f, 1.0f, 10.0f);
	HVar env_anisotropicIntensity("env_anisotropicIntensity", "Atmospheric scattering intensity", 0.38f, 0.0f, 10.0f);
	HVar env_density("env_density", "The density of the atmosphere", 0.11f, 0.0f, 4.0f);
	HVar env_volumetricLighting("r_volumetricLighting", "Enable or disable volumetric lighting", true, false, true);
	HVar env_volumetricScattering("env_volumetricScattering", "The amount of scattering used in volumetric lighting calculations", -0.43f, -2.0f, 2.0f);
	HVar env_volumetricStrength("env_volumetricStrength", "The strength multiplier of volumetric lighting", 1.0f, 0.1f, 5.0f);
	HVar env_volumetricSteps("env_volumetricSteps", "The number of iterations over which to calculate volumetric lighting", 100.0f, 10.0f, 500.0f);
	HVar env_waterNormalInfluence("env_waterNormalInfluence", "The strength of the normal maps when rendering water", 0.4f, 0.0f, 4.0f);
	HVar env_volumetricStepIncrement("env_volumetricStepIncrement", "The distance to move along the ray for each volumetric step", 1.0f, 0.1f, 100.0f);
	HVar r_gamma("r_gamma", "The amount of gamma correction to apply", 2.2f, 0.1f, 5.0f);
	HVar r_shadowCascades("r_shadowCascades", "The number of cascades to calculate with shadow mapping", 4, 1, 4);
	HVar r_shadowCascadeRange("r_shadowCascadeRange", "The depth of one shadow cascade, except the last (which will occupy all remaining space", 100.0f, 1.0f, 10000.0f);
	HVar r_penumbraFilterMaxSize("r_penumbraFilterMaxSize", "The maximum filter size for penumbra calculation", 0.002f, 0.0f, 10.0f);
	HVar r_shadowFilterMaxSize("r_shadowFilterMaxSize", "The maximum size of the shadow filter", 0.21f, 0.0f, 10.0f);
	HVar r_shadowBiasMultiplier("r_shadowBiasMultiplier", "The bias multiplier to use when calculating normal offset", 0.0002f, 0.0f, 1.0f);
	HVar r_shadowCascadeBlendRange("r_shadowCascadeBlendRange", "The distance to use for blending shadow cascades together", 10.0f, 1.0f, 1000.0f);
	HVar r_debugScene("r_debugScene", "Draw debugging info for the current scene", 0, 0, 1);
	HVar r_waterResolution("r_waterResolution", "The resolution multiplier at which to render water, a value of 1.0f is full resolution", 1.0f, 0.1f, 1.0f);
	HVar r_bloomLuminanceThreshold("r_bloomLuminanceThreshold", "The minimum amount of luminance for a material to bloom", 0.9999999f, 0.0f, 1.0f);
	HVar r_fxaa("r_fxaa", "Whether or not to use the FXAA anti-aliasing method", 1, 0, 1);
	HVar r_fog("r_fog", "Enable or disable fog effect", 1, 0, 1);
	HVar r_fogDensity("r_fogDensity", "How dense the fog should be", 0.0030f, 0.0f, 1.0f);
	HVar r_lodPartition("r_lodPartition", "The value that determines where LOD partitions occur", 250.0f, 10.0f, 5000.0f);
	HVar r_frustumSphereBoundsMultiplier("r_frustumSphereBoundsMultiplier", "The multiplier applied to the frustum bounds in order to calculate culling", 1.15f, 1.0f, 4.0f);
	HVar r_shadowMinimumLodThreshold("r_shadowMinimumLodLevel", "The lowest LOD level allowed for shadow maps. A high number will improve performance at the expensve of shadow fidelity", 0, 0, 3);
	HVar r_taa("r_taa", "Enable or disable temporal anti-aliasing", true, false, true);
	HVar r_shadowNearClip("r_shadowNearClip", "How much clipping offset to apply to directional lights, larger scenes typically require a higher value", 150.0f, -1000.0f, 1000.0f);
	HVar r_colourFilter("r_colourFilter", "The filter colour to use for colour grading", math::Vector3(1.00f, 0.98f, 0.97f), math::Vector3(0.0f), math::Vector3(1.0f));
	HVar r_contrast("r_contrast", "How much contrast to apply to the final render", 1.0f, 0.1f, 3.0f);
	HVar r_exposure("r_exposure", "How much exposure to apply to the final render", 1.0f, 0.1f, 3.0f);
	HVar r_hueShift("r_hueShift", "How much to adjust the hue by in the final render", 0.0f, -3.0f, 3.0f);
	HVar r_saturation("r_saturation", "How much to saturate the final render", 1.05f, 0.1f, 3.0f);
	HVar r_interpolate("r_interpolate", "Interpolates entities that have a mesh component and have interpolation enabled", true, false, true);
	HVar r_ssr("r_ssr", "Screen-space reflections", true, false, true);

	SceneRenderer::SceneRenderer()
	{}

	void SceneRenderer::Create()
	{
		_gbuffer.Create(g_pEnv->_graphicsDevice, g_pEnv->_graphicsDevice->GetCurrentMSAALevel());

		_sphereMesh = Mesh::Create("EngineData.Models/Primitives/sphere.hmesh");

		_pointLightMaterial = Material::Create("EngineData.Materials/PointLight.hmat");
		_spotLightMaterial = Material::Create("EngineData.Materials/SpotLight.hmat");

		CreateShaders();

		uint32_t width, height;
		g_pEnv->_graphicsDevice->GetBackBufferDimensions(width, height);

		CreateRenderTargets(width, height);

		g_pEnv->_commandManager->RegisterVar(&env_zenithExponent);
		g_pEnv->_commandManager->RegisterVar(&env_anisotropicIntensity);
		g_pEnv->_commandManager->RegisterVar(&env_density);
		g_pEnv->_commandManager->RegisterVar(&env_volumetricLighting);
		g_pEnv->_commandManager->RegisterVar(&env_volumetricScattering);
		g_pEnv->_commandManager->RegisterVar(&env_volumetricStrength);
		g_pEnv->_commandManager->RegisterVar(&env_volumetricSteps);
		g_pEnv->_commandManager->RegisterVar(&env_waterNormalInfluence);
		g_pEnv->_commandManager->RegisterVar(&env_volumetricStepIncrement);
		g_pEnv->_commandManager->RegisterVar(&r_gamma);
		g_pEnv->_commandManager->RegisterVar(&r_shadowCascades);
		g_pEnv->_commandManager->RegisterVar(&r_shadowCascadeRange);
		g_pEnv->_commandManager->RegisterVar(&r_penumbraFilterMaxSize);
		g_pEnv->_commandManager->RegisterVar(&r_shadowFilterMaxSize);
		g_pEnv->_commandManager->RegisterVar(&r_shadowBiasMultiplier);
		g_pEnv->_commandManager->RegisterVar(&r_shadowCascadeBlendRange);
		g_pEnv->_commandManager->RegisterVar(&r_debugScene);
		g_pEnv->_commandManager->RegisterVar(&r_waterResolution);
		g_pEnv->_commandManager->RegisterVar(&r_bloomLuminanceThreshold);
		g_pEnv->_commandManager->RegisterVar(&r_fxaa);
		g_pEnv->_commandManager->RegisterVar(&r_fog);
		g_pEnv->_commandManager->RegisterVar(&r_lodPartition);
		g_pEnv->_commandManager->RegisterVar(&r_frustumSphereBoundsMultiplier);
		g_pEnv->_commandManager->RegisterVar(&r_shadowMinimumLodThreshold);
		g_pEnv->_commandManager->RegisterVar(&r_taa);
		g_pEnv->_commandManager->RegisterVar(&r_shadowNearClip);
		g_pEnv->_commandManager->RegisterVar(&r_colourFilter);
		g_pEnv->_commandManager->RegisterVar(&r_contrast);
		g_pEnv->_commandManager->RegisterVar(&r_exposure);
		g_pEnv->_commandManager->RegisterVar(&r_hueShift);
		g_pEnv->_commandManager->RegisterVar(&r_saturation);
		g_pEnv->_commandManager->RegisterVar(&r_interpolate);
		g_pEnv->_commandManager->RegisterVar(&r_ssr);
		g_pEnv->_commandManager->RegisterVar(&r_fogDensity);

		
	}

	void SceneRenderer::Resize(int32_t width, int32_t height)
	{
		std::unique_lock lock(_lock);

		SAFE_DELETE(_beautyRT);
		SAFE_DELETE(_shadowMapsRT);
		SAFE_DELETE(_shadowMapsAccumulator);
		SAFE_DELETE(_fogBuffer);
		SAFE_DELETE(_volumetricLightingBuffer);
		SAFE_DELETE(_atmosphereRT);
		SAFE_DELETE(_waterAccumulationRT);
		SAFE_DELETE(_lightAccumulationBuffer);
		SAFE_DELETE(_particleRT);
		SAFE_DELETE(_ssrTexture);
		SAFE_DELETE(_ssrHitInfo);
		SAFE_DELETE(_dlssTarget);

		SAFE_DELETE(_bloomEffect);

		_taa.Destroy();

		_gbuffer.Resize(width, height, g_pEnv->_graphicsDevice->GetCurrentMSAALevel());

		CreateRenderTargets(width, height);
	}

	void SceneRenderer::Destroy()
	{
		std::unique_lock lock(_lock);

		_gbuffer.Destroy();

		//SAFE_DELETE(_clouds);

		SAFE_DELETE(_beautyRT);
		SAFE_DELETE(_shadowMapsRT);
		SAFE_DELETE(_shadowMapsAccumulator);
		SAFE_DELETE(_fogBuffer);
		SAFE_DELETE(_volumetricLightingBuffer);
		SAFE_DELETE(_atmosphereRT);
		SAFE_DELETE(_waterAccumulationRT);
		SAFE_DELETE(_lightAccumulationBuffer);
		SAFE_DELETE(_pointLightBuffer);
		SAFE_DELETE(_particleRT);
		SAFE_DELETE(_ssrTexture);
		SAFE_DELETE(_ssrHitInfo);
		SAFE_DELETE(_dlssTarget);
		
		//SAFE_DELETE(_waterDSV);

		//SAFE_DELETE(_volumetricBlur);
		//SAFE_DELETE(_waterBlur);
		//SAFE_DELETE(_blueNoise);
		SAFE_DELETE(_bloomEffect);
		//SAFE_DELETE(_ssrBlur);
	}

	void SceneRenderer::CreateShaders()
	{
		_compositionShader			= IShader::Create("EngineData.Shaders/Deferred.hcs");	
		_fxaa						= IShader::Create("EngineData.Shaders/FXAA.hcs");
		_fogEffect					= IShader::Create("EngineData.Shaders/PostFog.hcs");
		_volumetricLighting			= IShader::Create("EngineData.Shaders/VolumetricLighting.hcs");
		_bilateralUpsample			= IShader::Create("EngineData.Shaders/BilateralUpsample.hcs");
		_pointLightShader			= IShader::Create("EngineData.Shaders/PointLight.hcs");
		_spotLightShader			= IShader::Create("EngineData.Shaders/SpotLight.hcs");
		_ssrShader					= IShader::Create("EngineData.Shaders/SSR.hcs");
		_vignetteShader				= IShader::Create("EngineData.Shaders/Vignette.hcs");
		_chromaticAberrationShader	= IShader::Create("EngineData.Shaders/ChromaticAbberation.hcs");
		_colourGradingShader		= IShader::Create("EngineData.Shaders/ColourGrade.hcs");
		_ssrResolve					= IShader::Create("EngineData.Shaders/SSRResolve.hcs");
		_tonemapShader				= IShader::Create("EngineData.Shaders/Tonemap.hcs");
		_basicDenoise				= IShader::Create("EngineData.Shaders/BasicDenoise.hcs");

		//_volumetricBlur = new BlurEffect(_volumetricLightingBuffer, BlurType::Gaussian, 2);
		//_waterBlur = new BlurEffect(_waterAccumulationRT, BlurType::Gaussian, 2);

		_blueNoise					= ITexture2D::Create("EngineData.Textures/LDR_RGBA_0.png");

		

		//_ssrBlur = new BlurEffect(_ssrTexture, BlurType::Gaussian, 1);

		
	}

	void SceneRenderer::CreateRenderTargets(int32_t width, int32_t height)
	{
		std::unique_lock lock(_lock);

		auto MsaaLevel = g_pEnv->_graphicsDevice->GetCurrentMSAALevel();

		const DXGI_FORMAT BEAUTY_FORMAT = (DXGI_FORMAT)g_pEnv->_graphicsDevice->GetDesiredBackBufferFormat();

		_beautyRT = g_pEnv->_graphicsDevice->CreateTexture2D(
			width,
			height,
			BEAUTY_FORMAT,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0, MsaaLevel, 0,
			nullptr,
			(D3D11_CPU_ACCESS_FLAG)0,
			MsaaLevel > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			MsaaLevel > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D,
			D3D11_DSV_DIMENSION_UNKNOWN,
			D3D11_USAGE_DEFAULT,
			D3D11_RESOURCE_MISC_SHARED);

		if (!_beautyRT)
		{
			LOG_CRIT("Failed to create composition render target");
			return;
		}

		_particleRT = g_pEnv->_graphicsDevice->CreateTexture2D(
			width,
			height,
			DXGI_FORMAT_R8G8B8A8_UNORM,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0, MsaaLevel, 0,
			nullptr,
			(D3D11_CPU_ACCESS_FLAG)0,
			MsaaLevel > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			MsaaLevel > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D);

		_ssrTexture = g_pEnv->_graphicsDevice->CreateTexture2D(
			width,// / 2,
			height,// / 2,
			BEAUTY_FORMAT,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0, MsaaLevel, 0,
			nullptr,
			(D3D11_CPU_ACCESS_FLAG)0,
			MsaaLevel > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			MsaaLevel > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D);

		_ssrHitInfo = g_pEnv->_graphicsDevice->CreateTexture2D(
			width,// / 2,
			height,// / 2,
			BEAUTY_FORMAT,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0, MsaaLevel, 0,
			nullptr,
			(D3D11_CPU_ACCESS_FLAG)0,
			MsaaLevel > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			MsaaLevel > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D);

		_ssrHistory = g_pEnv->_graphicsDevice->CreateTexture(_ssrTexture);
		_ssrResolved = g_pEnv->_graphicsDevice->CreateTexture(_ssrTexture);

		_shadowMapsRT = g_pEnv->_graphicsDevice->CreateTexture2D(
			width,
			height,
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0, 1, 0,
			nullptr,
			(D3D11_CPU_ACCESS_FLAG)0,
			/*MsaaLevel > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS :*/ D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			/*MsaaLevel > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS :*/ D3D11_SRV_DIMENSION_TEXTURE2D);

		_shadowMapsAccumulator = g_pEnv->_graphicsDevice->CreateTexture2D(
			width,
			height,
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0, 1, 0,
			nullptr,
			(D3D11_CPU_ACCESS_FLAG)0,
			/*MsaaLevel > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS :*/ D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			/*MsaaLevel > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS :*/ D3D11_SRV_DIMENSION_TEXTURE2D);

		_waterAccumulationRT = g_pEnv->_graphicsDevice->CreateTexture2D(
			(int32_t)((float)width * r_waterResolution._val.f32),
			(int32_t)((float)height * r_waterResolution._val.f32),
			DXGI_FORMAT_R8G8B8A8_UNORM,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0, MsaaLevel, 0,
			nullptr,
			(D3D11_CPU_ACCESS_FLAG)0,
			MsaaLevel > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			MsaaLevel > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D);

		/*_waterDSV = g_pEnv->_graphicsDevice->CreateTexture2D(
			width * r_waterResolution._val.f32,
			height * r_waterResolution._val.f32,
			DXGI_FORMAT_R32_TYPELESS,
			1,
			D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_DEPTH_STENCIL,
			0, MsaaLevel, 0,
			D3D11_RTV_DIMENSION_UNKNOWN,
			D3D11_UAV_DIMENSION_UNKNOWN,
			D3D11_SRV_DIMENSION_TEXTURE2D,
			D3D11_DSV_DIMENSION_TEXTURE2D);*/

		_fogBuffer = g_pEnv->_graphicsDevice->CreateTexture2D(
			width,
			height,
			BEAUTY_FORMAT,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0, MsaaLevel, 0,
			nullptr,
			(D3D11_CPU_ACCESS_FLAG)0,
			MsaaLevel > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			MsaaLevel > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D);

		_lightAccumulationBuffer = g_pEnv->_graphicsDevice->CreateTexture2D(
			width,
			height,
			BEAUTY_FORMAT,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0, MsaaLevel, 0,
			nullptr,
			(D3D11_CPU_ACCESS_FLAG)0,
			MsaaLevel > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			MsaaLevel > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D);

		_pointLightBuffer = g_pEnv->_graphicsDevice->CreateTexture2D(
			width,
			height,
			BEAUTY_FORMAT,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0, MsaaLevel, 0,
			nullptr,
			(D3D11_CPU_ACCESS_FLAG)0,
			MsaaLevel > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			MsaaLevel > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D);

		_volumetricLightingBuffer = g_pEnv->_graphicsDevice->CreateTexture2D(
			width / 2,
			height / 2,
			DXGI_FORMAT_R8G8B8A8_UNORM,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0, 1, 0,
			nullptr,
			(D3D11_CPU_ACCESS_FLAG)0,
			/*MsaaLevel > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS :*/ D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			/*MsaaLevel > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS :*/ D3D11_SRV_DIMENSION_TEXTURE2D);

		_atmosphereRT = g_pEnv->_graphicsDevice->CreateTexture2D(
			width,
			height,
			BEAUTY_FORMAT,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0, MsaaLevel, 0,
			nullptr,
			(D3D11_CPU_ACCESS_FLAG)0,
			MsaaLevel > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			MsaaLevel > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D);

		// DLSS target should be full screen
		uint32_t backBufferWidth, backBufferHeight;
		g_pEnv->_graphicsDevice->GetBackBufferDimensions(backBufferWidth, backBufferHeight);

		_dlssTarget = g_pEnv->_graphicsDevice->CreateTexture2D(
			backBufferWidth,
			backBufferHeight,
			BEAUTY_FORMAT,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
			0, MsaaLevel, 0,
			nullptr,
			(D3D11_CPU_ACCESS_FLAG)0,
			MsaaLevel > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_TEXTURE2D,
			MsaaLevel > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D);

		_bloomEffect = new Bloom();
		_bloomEffect->Create(width / 4, height / 4);

		_taa.Create(_beautyRT);

		g_pEnv->_denoiserProvider->CreateBuffers(width, height, _beautyRT, _gbuffer.GetNormal(), _gbuffer.GetDiffuse());
	}

	const std::vector<Light*>& SceneRenderer::GetShadowCasters() const
	{
		return _shadowCasters;
	}

	void SceneRenderer::ClearShadowCasters()
	{
		_shadowCasters.clear();
	}

	void SceneRenderer::RemoveShadowCaster(Light* light)
	{
		auto it = std::remove(_shadowCasters.begin(), _shadowCasters.end(), light);

		if (it != _shadowCasters.end())
			_shadowCasters.erase(it);
	}

	/// <summary>
	/// Render the scene from the perspective of the given camera
	/// </summary>
	/// <param name="scene"></param>
	/// <param name="camera"></param>
	void SceneRenderer::RenderScene(Scene* scene, Camera* camera, SceneFlags flags)
	{
		std::unique_lock lock(_lock);

		if (!camera)
			return;

		if (!_sphereEntity)
		{
			_sphereEntity = g_pEnv->_sceneManager->GetCurrentScene()->CreateEntity("LightSphere");
			_sphereEntity->SetLayer(Layer::Invisible);
			_sphereEntity->SetFlag(EntityFlags::DoNotSave);
			auto sphereMeshRenderer = _sphereEntity->AddComponent<StaticMeshComponent>();
			sphereMeshRenderer->SetMesh(_sphereMesh);
			sphereMeshRenderer->SetMaterial(_pointLightMaterial);
		}
		// TODO move this somewhere
		/*if (_clouds == nullptr)
		{
			_clouds = new CloudVolume(math::Vector3(-5000.0f, -50.0f, -5000.0f), math::Vector3(5000.0f, 50.0f, 5000.0f), 128);
			_clouds->Generate();
		}*/
		// The order to render a scene in is
		// - Shadowmap
		// - Opaque
		// - Water
		// - Transparent
		// - Post processing effects

		_currentScene = scene;
		_currentCamera = camera;
		_cameraEntity = camera->GetEntity();

		_currentShadowCasterForComposition = nullptr;
		_currentShadowMapForComposition = nullptr;

		assert(_cameraEntity && "Camera entity cannot be null");

		

		if ((flags & SceneFlags::PostProcessingEnabled) != (SceneFlags)0)
		{
			if (g_pEnv->_streamlineProvider && g_pEnv->_streamlineProvider->IsEnabled())
			{
				g_pEnv->_streamlineProvider->BeginFrame();
				SetStreamlineConstants();
			}
		}			

		// Collect shadow casters first
		//
		CollectShadowCasters();
		
		// Clear the composition RT
		_beautyRT->ClearRenderTargetView(math::Color(0, 0, 0, 1));
		//_currentCamera->GetFullScreenRenderTarget()->ClearRenderTargetView(math::Color(0, 0, 0, 1));
		_currentCamera->GetRenderTarget()->ClearRenderTargetView(math::Color(0, 0, 0, 0));

		// Set up the Gbuffer in preparation for rendering
		//
		_gbuffer.Clear();		

		for (auto& caster : _shadowCasters)
		{
			RenderShadowMaps(caster);
		}

		// set up the viewport
		auto bbvp = _currentCamera->GetViewport();
		g_pEnv->_graphicsDevice->SetViewports({ bbvp });

		auto sunLight = _currentScene->GetSunLight();

		SetupPerFrameBuffer(
			_currentCamera->GetViewMatrix(),
			_currentCamera->GetProjectionMatrix(),
			_currentCamera->GetViewMatrixPrev(),
			_currentCamera->GetProjectionMatrixPrev(),
			r_shadowCascades._val.i32,
			sunLight ? sunLight->GetEntity()->GetComponent<Transform>()->GetForward() : math::Vector3::Forward,
			_currentCamera->GetViewport(),
			6,
			/*sunLight ? sunLight->GetLightMultiplier() :*/ 1.0f
		);
		_gbuffer.SetAsRenderTargets(_currentCamera->GetViewport());

		RenderOpaque();
		//AccumulateShadowMaps();
		
		
		if(HEX_HASFLAG(flags, SceneFlags::PostProcessingEnabled))
			SetStreamlineConstants();	

		_gbuffer.GetDiffuse()->CopyTo(_beautyRT);

		RenderTransparent();

		//RenderWater();
		RenderPostProcessing(flags);

		if (r_debugScene._val.i32 == 1)
		{
			g_pEnv->_graphicsDevice->SetBlendState(BlendState::Transparency);

			_currentScene->RenderDebug(_currentCamera->GetPVS());

			g_pEnv->_navMeshProvider->DebugRender();

			g_pEnv->_graphicsDevice->SetBlendState(BlendState::Opaque);
		}

		//g_pEnv->_graphicsDevice->SetRenderTarget(g_pEnv->_graphicsDevice->GetBackBuffer());
	}

	void SceneRenderer::CollectShadowCasters()
	{
		// Clear the list of shadow casters ever frame
		//
		_shadowCasters.clear();

		// Always put the main sun light in the scene
		//
		//_shadowCasters.push_back(_currentScene->GetSunLight());

		// Next, get a list of lights and work out whether or not to include them
		//
		std::vector<Light*> directionalLights, spotLights, pointLights, allLights;
		std::vector<Light*> pvs;
		_currentScene->GetComponents<DirectionalLight>((std::vector<DirectionalLight*>&)directionalLights);
		_currentScene->GetComponents<SpotLight>((std::vector<SpotLight*>&)spotLights);
		_currentScene->GetComponents<PointLight>((std::vector<PointLight*>&)pointLights);

		allLights.insert(allLights.end(), directionalLights.begin(), directionalLights.end());
		allLights.insert(allLights.end(), spotLights.begin(), spotLights.end());
		allLights.insert(allLights.end(), pointLights.begin(), pointLights.end());

		for (auto& component : allLights)
		{
			auto light = component->CastAs<Light>();

			if (!light)
				continue;

			if (light->GetDoesCastShadows() == false)
				continue;

			//if (light == _currentScene->GetSunLight())
			//	continue;

			// if its a valid light and casts shadows, add it to the pvs
			//
			pvs.push_back(light);

			// We check that 
			//if (pvs.size() + 1 >= MaxShadowCasters)
			//	break;
		}

		// now that we know which lights are potentially affecting the scene, we should sort them according to priority
		//
		std::sort(pvs.begin(), pvs.end(), [this](Light* left, Light* right) {

			auto leftDist = (left->GetEntity()->GetPosition() - _currentCamera->GetEntity()->GetPosition()).Length();
			auto rightDist = (right->GetEntity()->GetPosition() - _currentCamera->GetEntity()->GetPosition()).Length();

			return leftDist > rightDist;
			});

		// Finally, tack on the shadow casters to the sun light
		//
		_shadowCasters.insert(_shadowCasters.end(), pvs.begin(), pvs.begin() + (pvs.size() < MaxShadowCasters ? pvs.size() : MaxShadowCasters));
	}

	

	void SceneRenderer::SetupPerFrameBuffer(
		const math::Matrix& viewMatrix,
		const math::Matrix& projectionMatrix,
		const math::Matrix& viewMatrixPrev,
		const math::Matrix& projectionMatrixPrev,
		int32_t numCascades,
		const math::Vector3& lightDir,
		const math::Viewport& viewport,
		int passIdx,
		float lightMultiplier,
		bool forceCascade)
	{
		// update the per frame buffer with our numeric data
		//
		auto perFrameBuffer = g_pEnv->_graphicsDevice->GetEngineConstantBuffer(EngineConstantBuffer::PerFrameBuffer);

		if (perFrameBuffer)
		{
			PerFrameConstantBuffer bufferData;

			// Camera data
			bufferData._viewMatrix = viewMatrix.Transpose();
			bufferData._projectionMatrix = projectionMatrix.Transpose();
			bufferData._viewProjectionMatrix = (viewMatrix * projectionMatrix).Transpose();
			bufferData._viewMatrixInverse = viewMatrix.Invert().Transpose();
			bufferData._projectionMatrixInverse = projectionMatrix.Invert().Transpose();
			bufferData._viewProjectionMatrixInverse = (viewMatrix * projectionMatrix).Invert().Transpose();

			

			bufferData._viewMatrixPrev = viewMatrixPrev.Transpose();
			bufferData._projectionMatrixPrev = projectionMatrixPrev.Transpose();
			bufferData._viewProjectionMatrixPrev = (viewMatrixPrev * projectionMatrixPrev).Transpose();
			bufferData._viewMatrixInversePrev = viewMatrixPrev.Invert().Transpose();
			bufferData._projectionMatrixInversePrev = projectionMatrixPrev.Invert().Transpose();
			bufferData._viewProjectionMatrixInversePrev = (viewMatrixPrev * projectionMatrixPrev).Invert().Transpose();

			bufferData._eyePos = math::Vector4(_cameraEntity->GetPosition().x, _cameraEntity->GetPosition().y, _cameraEntity->GetPosition().z, 1.0f);
			bufferData._eyeDir = _currentCamera ? math::Vector4(_currentCamera->GetLookDir()) : math::Vector4();

			bufferData._colourGrading.colourFilter = r_colourFilter._val.v3;
			bufferData._colourGrading.contrast = r_contrast._val.f32;
			bufferData._colourGrading.exposure = r_exposure._val.f32;
			bufferData._colourGrading.hueShift = r_hueShift._val.f32;
			bufferData._colourGrading.saturation = r_saturation._val.f32;

			if (bufferData._viewMatrix != bufferData._viewMatrixPrev)
			{

				bool a = false;
			}

			// Shadowmap data
			
			const auto maxShadowCascades = r_shadowCascades._val.i32;// shadowCaster->GetMaxSupportedShadowCascades();

			float cascadeStart = 0.0f;

			float mul = (_currentCamera->GetFarZ() / (float)numCascades) * (float)maxShadowCascades;;

			for (int i = 0; i < 4; ++i)
			{
				// Calculate the shadow map cascade ranges
				//
				//float start = min(1.0f, (float)(i + 0) / (float)numCascades);
				//float end = min(1.0f, (float)(i + 1) / (float)numCascades);

				//float start = cascadeStart;
				float end = (cascadeStart + r_shadowCascadeRange._val.f32) / _currentCamera->GetFarZ();

				if (i >= maxShadowCascades - 1)
					end = 1.0f;

				// Calculate the frustum splits
				//
				((float*)&bufferData._frustumSplits.x)[i] = end * _currentCamera->GetFarZ();

				cascadeStart += r_shadowCascadeRange._val.f32;
			}

			//bufferData._lightViewMatrix = shadowCaster->GetViewMatrix().Transpose();

			//bufferData._lightPosition = math::Vector4(params.lightPosition.x, params.lightPosition.y, params.lightPosition.z, 1.0f);
			bufferData._lightDirection = math::Vector4(lightDir.x, lightDir.y, lightDir.z, 1.0f);
			bufferData._globalLight[0] = lightMultiplier;

			const auto& fogColour = _currentScene->GetFogColour();

			bufferData._globalLight[1] = fogColour.R();
			bufferData._globalLight[2] = fogColour.G();
			bufferData._globalLight[3] = fogColour.B();

			bufferData._screenWidth = (int32_t)viewport.width;// : 0;
			bufferData._screenHeight = (int32_t)viewport.height;// : 0;

			// atmosphere
			bufferData._atmosphere.zenithExponent = env_zenithExponent._val.f32;
			bufferData._atmosphere.anisotropicIntensity = env_anisotropicIntensity._val.f32;
			bufferData._atmosphere.density = env_density._val.f32;
			bufferData._atmosphere.fogDensity = r_fogDensity._val.f32;

			bufferData._atmosphere.ambientLight = _currentScene->GetAmbientColour();

			bufferData._atmosphere.volumetricScattering = env_volumetricScattering._val.f32;
			bufferData._atmosphere.volumetricStrength = env_volumetricStrength._val.f32;
			bufferData._atmosphere.volumetricSteps = (int)env_volumetricSteps._val.f32;
			bufferData._atmosphere.volumetricStepIncrement = env_volumetricStepIncrement._val.f32;

			// bloom
			bufferData._bloom.luminosityThreshold = r_bloomLuminanceThreshold._val.f32;
			bufferData._bloom.viewportScale = 4.0f;

			bufferData._time = g_pEnv->_timeManager->GetTime();
			bufferData._frame = g_pEnv->_timeManager->_frameCount;
			bufferData._gamma = r_gamma._val.f32;			

			// ocean
			bufferData._oceanConfig = _currentScene->GetOcean();

			bufferData._jitterOffsets = _taa.GetJitterOffset(viewport.width, viewport.height);
			_denoiseFD.jitter = bufferData._jitterOffsets;

			perFrameBuffer->Write(&bufferData, sizeof(bufferData));

			// set the per frame constant buffers
			g_pEnv->_graphicsDevice->SetConstantBufferVS(0, perFrameBuffer);
			g_pEnv->_graphicsDevice->SetConstantBufferPS(0, perFrameBuffer);
		}
	}

	void SceneRenderer::SetupPerShadowCasterBuffer(Light* shadowCaster, bool forceCascade, int32_t passIdx, int32_t lightIndex, int32_t numSamples, float coneSize)
	{
		// update the per frame buffer with our numeric data
		//
		auto perFrameBuffer = g_pEnv->_graphicsDevice->GetEngineConstantBuffer(EngineConstantBuffer::PerShadowCasterBuffer);

		if (perFrameBuffer)
		{
			PerShadowCasterBuffer bufferData;

			// Shadowmap data
			if (shadowCaster != nullptr)
			{
				const auto maxShadowCascades = shadowCaster->GetMaxSupportedShadowCascades();

				//for (int i = 0; i < maxShadowCascades; ++i)
				//{
				//	//_shadowMap[i]->SetRenderState();

				//	// Calculate the shadow map cascade ranges
				//	//
				//	float start = (float)(i + 0) / (float)maxShadowCascades;
				//	float end = (float)(i + 1) / (float)maxShadowCascades;

				//	// Construct the matrices for shadow mapping
				//	//
				//	shadowCaster->ConstructMatrices(_currentCamera, start, end, i);

				//	bufferData._lightProjectionMatrix[i] = shadowCaster->GetProjectionMatrix(i).Transpose();
				//	bufferData._lightViewMatrix[i] = shadowCaster->GetViewMatrix(i).Transpose();
				//}				

				float cascadeStart = 0.0f;

				for (int i = 0; i < maxShadowCascades; ++i)
				{
					// Calculate the shadow map cascade ranges
					//
					//float start = min(1.0f, (float)(i + 0) / (float)numCascades);
					//float end = min(1.0f, (float)(i + 1) / (float)numCascades);

					/*float start = cascadeStart / _currentCamera->GetFarZ();
					float end = (cascadeStart + r_shadowCascadeRange._val.f32) / _currentCamera->GetFarZ();

					if (i == maxShadowCascades - 1)
						end = 1.0f;

					shadowCaster->ConstructMatrices(_currentCamera, start, end, i);*/

					bufferData._lightProjectionMatrix[i] = shadowCaster->GetProjectionMatrix(i).Transpose();
					bufferData._lightViewMatrix[i] = shadowCaster->GetViewMatrix(i).Transpose();
					bufferData._lightViewProjectionMatrix[i] = (shadowCaster->GetViewMatrix(i) * shadowCaster->GetProjectionMatrix(i)).Transpose();

					

					cascadeStart += r_shadowCascadeRange._val.f32;
				}

				auto shadowMapSize = shadowCaster->GetShadowMap() ? shadowCaster->GetShadowMap()->GetViewport().width : 0.0f;
				float shadowVarsMultiplier = (4096.0f / shadowMapSize);

				if (shadowVarsMultiplier > 1.0f)
					shadowVarsMultiplier *= 2.0f;

				// shadow
				bufferData._shadowConfig.penumbraFilterMaxSize = r_penumbraFilterMaxSize._val.f32 * shadowVarsMultiplier;
				bufferData._shadowConfig.shadowFilterMaxSize = r_shadowFilterMaxSize._val.f32 * shadowVarsMultiplier;
				bufferData._shadowConfig.biasMultiplier = r_shadowBiasMultiplier._val.f32;
				bufferData._shadowConfig.samples = numSamples;
				bufferData._shadowConfig.cascadeBlendRange = r_shadowCascadeBlendRange._val.f32;

				bufferData._shadowCasterLightDir = shadowCaster->GetEntity()->GetComponent<Transform>()->GetForward();
				bufferData._lightRadius = shadowCaster->GetRadius();
				bufferData._spotLightConeSize = (coneSize);
				bufferData._shadowConfig.shadowMapSize = shadowMapSize;

				bufferData._shadowConfig.passIndex = passIdx;
				bufferData._shadowConfig.cascadeOverride = forceCascade ? passIdx : -1;
				bufferData._shadowConfig.lightIndex = lightIndex;
			}

			perFrameBuffer->Write(&bufferData, sizeof(bufferData));

			// set the per frame constant buffers
			g_pEnv->_graphicsDevice->SetConstantBufferVS(2, perFrameBuffer);
			g_pEnv->_graphicsDevice->SetConstantBufferPS(2, perFrameBuffer);
		}
	}

	void SceneRenderer::RenderOpaque()
	{
		PROFILE();

		/*SceneRenderParameters params;
		params.passIndex = 6;
		params.camera = _currentCamera;
		params.isShadowPass = false;
		params.frustumSliceBounds = _currentCamera->GetFrustumSphere();*/

		//_currentScene->RenderSkySphere();

		_currentScene->RenderEntities(
			_currentCamera->GetPVS(),
			LAYERMASK(Layer::Sky),
			MeshRenderFlags::MeshRenderNormal);

		_gbuffer.GetDiffuse()->CopyTo(_atmosphereRT);

		//g_pEnv->_graphicsDevice->SetCullingMode(CullingMode::FrontFace);

		_currentScene->RenderEntities(
			_currentCamera->GetPVS(),
			LAYERMASK(Layer::StaticGeometry) | LAYERMASK(Layer::DynamicGeometry),
			MeshRenderFlags::MeshRenderNormal);

		g_pEnv->_graphicsDevice->SetBlendState(BlendState::Transparency);

		//g_pEnv->_graphicsDevice->EnableDepthBuffer(false);
		//_currentScene->RenderTransparent(params);
		//g_pEnv->_graphicsDevice->EnableDepthBuffer(true);

		// Render all particles to their own RT, then blend that back to the diffuse gbuffer
		//{
		//	g_pEnv->_graphicsDevice->SetBlendState(BlendState::Additive);

		//	g_pEnv->_graphicsDevice->SetRenderTarget(_particleRT, g_pEnv->_graphicsDevice->GetDepthStencil());
		//	_particleRT->ClearRenderTargetView(math::Color(0, 0, 0, 0));
		//	

		//_currentScene->RenderParticles(params);

		//	//
		//	_particleRT->BlendTo_Additive(_gbuffer.GetDiffuse());
		//	_particleRT->BlendTo_Additive(_compositionRT);

		//	_gbuffer.SetAsRenderTargets();
		//}

		g_pEnv->_graphicsDevice->SetBlendState(BlendState::Opaque);

		/*if(r_debugScene._val.i32 == 1)
			_currentScene->RenderDebug(params);*/
	}

	void SceneRenderer::RenderShadowMaps(Light* shadowCaster)
	{
		PROFILE();

		// Enable front face culling for shadow maps
		//
		//g_pEnv->_graphicsDevice->SetCullingMode(CullingMode::NoCulling);
		{
			float cascadeStart = 0.0f;

			for (auto i = 0; i < shadowCaster->GetMaxSupportedShadowCascades(); ++i)
			{
				auto shadowMap = shadowCaster->GetShadowMap(i);

				if (!shadowMap)
					continue;

				/*auto lightFlags = shadowCaster->GetFlags();

				if (!(_currentCamera->HasMovedThisFrame() ||
					(lightFlags & LightFlags::RebuildShadowMatrices | LightFlags::RebuildPVS) == (LightFlags::RebuildShadowMatrices | LightFlags::RebuildPVS)))
					continue;*/

				if ((g_pEnv->_timeManager->_frameCount + i) % 2 == 0)
					continue;

				// Set as the current render target
				//
				shadowMap->SetRenderTarget();	

				bool shouldOverrideCascade = shadowCaster->CastAs<PointLight>() != nullptr;

				
				float start = cascadeStart / _currentCamera->GetFarZ();
				float end = (cascadeStart + r_shadowCascadeRange._val.f32) / _currentCamera->GetFarZ();

				if (i == shadowCaster->GetMaxSupportedShadowCascades() - 1)
					end = 1.0f;

				if (start == 1.0f && end == 1.0f)
				{
					LOG_DEBUG("Cannot render shadow map cascade where start and end are both 1.0f");
					return;
				}

				if (shadowCaster->GetEntity()->GetName().find("PointLight") != std::string::npos)
				{
					bool a = false;
				}

				{
					shadowCaster->ConstructMatrices(_currentCamera, start, end, i);

					PVSParams params;
					params.lodPartition = r_lodPartition._val.f32;
					params.shapeType = PVSParams::ShapeType::Sphere;
					params.shape.sphere = shadowCaster->GetLightBoundingSphere(i);
					//params.shapeType = PVSParams::ShapeType::Frustum;
					//params.shape.frustum = shadowCaster->GetLightBoundingFrustum(i);
					params.isShadow = true;
					params.camera = _currentCamera;

					shadowCaster->GetPVS(i)->CalculateVisibility(_currentScene, params);					
				}
				

				cascadeStart += r_shadowCascadeRange._val.f32;

				SetupPerFrameBuffer(
					shadowCaster->GetViewMatrix(i),
					shadowCaster->GetProjectionMatrix(i),
					shadowCaster->GetViewMatrixPrev(i),
					shadowCaster->GetProjectionMatrixPrev(i),
					shadowCaster->GetMaxSupportedShadowCascades(),
					shadowCaster->GetEntity()->GetComponent<Transform>()->GetForward(),
					shadowMap->GetViewport(),
					i,
					1.0f/*_currentScene->GetSunLight()->GetLightMultiplier()*/,
					shouldOverrideCascade);

				

				_currentScene->RenderEntities(
					shadowCaster->GetPVS(i),
					LAYERMASK(Layer::StaticGeometry) | LAYERMASK(Layer::DynamicGeometry),
					MeshRenderFlags::MeshRenderShadowMap);

				/*if (r_taa._val.b)
				{
					if (auto guiRenderer = g_pEnv->_uiManager->GetRenderer(); guiRenderer != nullptr)
					{
						guiRenderer->StartFrame();
						_taa.Resolve(shadowMap->GetRenderTarget(), shadowMap->GetRenderTarget(), _gbuffer.GetSpecular(), guiRenderer);

						guiRenderer->EndFrame();
					}
				}*/

				// render the transparent parts of the scene next
				//g_pEnv->_graphicsDevice->SetBlendState(BlendState::Additive);

				//_currentScene->RenderTransparent(params);

				//g_pEnv->_graphicsDevice->SetBlendState(BlendState::Opaque);
			}

			shadowCaster->ClearFlag(LightFlags::RebuildShadowMatrices | LightFlags::RebuildPVS);
		}

		g_pEnv->_graphicsDevice->ClearScissorRect();
		// Switch back to back face culling
		//
		//g_pEnv->_graphicsDevice->SetCullingMode(CullingMode::FrontFace);
	}
#if 0
	void SceneRenderer::RenderWater()
	{
		PROFILE();

		/// RENDER WATER
		SceneRenderParameters params;
		params.passIndex = 6;
		params.camera = _currentCamera;
		params.isShadowPass = false;

		const auto& bbvp = g_pEnv->_graphicsDevice->GetBackBufferViewport();

		// set the shadow viewport
		//
		D3D11_VIEWPORT vp;
		vp.TopLeftX = 0;
		vp.TopLeftY = 0;
		vp.Width = bbvp.Width * r_waterResolution._val.f32;
		vp.Height = bbvp.Height * r_waterResolution._val.f32;
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		g_pEnv->_graphicsDevice->SetViewports({ vp });

		auto guiRenderer = g_pEnv->_uiManager->GetRenderer();
		

		g_pEnv->_graphicsDevice->SetRenderTarget(_waterAccumulationRT, g_pEnv->_graphicsDevice->GetDepthStencil()/*_waterDSV*/);
		_waterAccumulationRT->ClearRenderTargetView(math::Color(0, 0, 0, 0));
		//_waterDSV->ClearDepth(D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL);

		g_pEnv->_graphicsDevice->EnableDepthBuffer(true);

		//guiRenderer->StartFrame();

		//guiRenderer->FullScreenTexturedQuad(_gbuffer.GetDiffuse());

		if (_currentCamera->GetEntity()->GetPosition().y < 0.0f)
			g_pEnv->_graphicsDevice->SetCullingMode(CullingMode::FrontFace);
		else
			g_pEnv->_graphicsDevice->SetCullingMode(CullingMode::BackFace);

		
		//_gbuffer.GetDiffuse()->CopyTo(_waterAccumulationRT);

		_currentScene->RenderWater(params, false, nullptr);

		//_waterAccumulationRT->CopyTo(_compositionRT);
		//_waterAccumulationRT->CopyTo(_gbuffer.GetDiffuse());

		

		guiRenderer->StartFrame();

		g_pEnv->_graphicsDevice->SetBlendState(BlendState::Transparency);
		//_waterBlur->Render(guiRenderer);

		g_pEnv->_graphicsDevice->SetViewports({ bbvp });

		

		g_pEnv->_graphicsDevice->SetRenderTarget(_compositionRT);
		guiRenderer->FullScreenTexturedQuad(_waterAccumulationRT);

		g_pEnv->_graphicsDevice->SetRenderTarget(_gbuffer.GetDiffuse());
		guiRenderer->FullScreenTexturedQuad(_waterAccumulationRT);

		guiRenderer->EndFrame();

		g_pEnv->_graphicsDevice->SetBlendState(BlendState::Opaque);
	}
#endif

	inline void matrixOrthoNormalInvert(math::Matrix& result, const math::Matrix& mat)
	{
		// Transpose the first 3x3
		result.m[0][0] = mat.m[0][0];
		result.m[0][1] = mat.m[1][0];
		result.m[0][2] = mat.m[2][0];
		result.m[1][0] = mat.m[0][1];
		result.m[1][1] = mat.m[1][1];
		result.m[1][2] = mat.m[2][1];
		result.m[2][0] = mat.m[0][2];
		result.m[2][1] = mat.m[1][2];
		result.m[2][2] = mat.m[2][2];

		// Invert the translation
		result.m[3][0] = -((mat.m[3][0] * mat.m[0][0]) + (mat.m[3][1] * mat.m[0][1]) + (mat.m[3][2] * mat.m[0][2]));
		result.m[3][1] = -((mat.m[3][0] * mat.m[1][0]) + (mat.m[3][1] * mat.m[1][1]) + (mat.m[3][2] * mat.m[1][2]));
		result.m[3][2] = -((mat.m[3][0] * mat.m[2][0]) + (mat.m[3][1] * mat.m[2][1]) + (mat.m[3][2] * mat.m[2][2]));

		// Fill in the remaining constants
		result.m[0][3] = 0.0f;
		result.m[1][3] = 0.0f;
		result.m[2][3] = 0.0f;
		result.m[3][3] = 1.0f;
	}

	inline void calcCameraToPrevCamera(math::Matrix& outCameraToPrevCamera, const math::Matrix& cameraToWorld, const math::Matrix& cameraToWorldPrev)
	{
		// Create translated versions of cameraToWorld and cameraToWorldPrev, translated to
		// so that the current camera is effectively at the world origin.
		// CC == 'Camera-Centred'
		math::Matrix cameraToCcWorld = cameraToWorld;
		*(math::Vector4*)&cameraToCcWorld.m[3][0] = math::Vector4(0, 0, 0, 1);
		math::Matrix cameraToCcWorldPrev = cameraToWorldPrev;
		cameraToCcWorldPrev.m[3][0] -= cameraToWorld.m[3][0];
		cameraToCcWorldPrev.m[3][1] -= cameraToWorld.m[3][1];
		cameraToCcWorldPrev.m[3][2] -= cameraToWorld.m[3][2];

		// We can use an optimised invert if we assume that the camera matrix is orthonormal
		math::Matrix ccWorldToCameraPrev;
		matrixOrthoNormalInvert(ccWorldToCameraPrev, cameraToCcWorldPrev);
		//matrixMul(outCameraToPrevCamera, cameraToCcWorld, ccWorldToCameraPrev);
		outCameraToPrevCamera = cameraToCcWorld * ccWorldToCameraPrev;
	}

	void SceneRenderer::SetStreamlineConstants()
	{
		auto sl = g_pEnv->_streamlineProvider;

		if (!sl || !sl->IsEnabled())
			return;

		if (!_currentCamera)
			return;

		if (_currentCamera->IsDLSSEnabled() == false)
		{
			g_pEnv->_streamlineProvider->SetDLSSOptions(0.5f, false, false, DLSSMode::Off, g_pEnv->GetScreenWidth(), g_pEnv->GetScreenHeight());
			return;
		}

		StreamlineConstants consts;

		auto viewReprojection = _currentCamera->GetViewMatrix().Invert() * _currentCamera->GetViewMatrixPrev();
		auto reprojectionMatrix = _currentCamera->GetProjectionMatrix().Invert() * viewReprojection * _currentCamera->GetProjectionMatrixPrev();

		const auto& cameraTransform = _currentCamera->GetEntity()->GetComponent<Transform>();

		consts.cameraAspectRatio = _currentCamera->GetAspectRatio();
		consts.cameraFOV = ToRadian(_currentCamera->GetFov());
		consts.cameraFar = _currentCamera->GetFarZ();
		consts.cameraNear = _currentCamera->GetNearZ();
		consts.cameraMotionIncluded = true;// false;

		consts.cameraPos = cameraTransform->GetPosition();
		consts.cameraFwd = cameraTransform->GetForward();
		consts.cameraUp = cameraTransform->GetUp();
		consts.cameraRight = cameraTransform->GetRight();

		consts.cameraViewToClip = _currentCamera->GetProjectionMatrix();
		consts.clipToCameraView = _currentCamera->GetProjectionMatrix().Invert();

#if 1
		math::Matrix cameraViewToWorld(
			math::Vector4(consts.cameraRight.x, consts.cameraRight.y, consts.cameraRight.z, 0.f),
			math::Vector4(consts.cameraUp.x, consts.cameraUp.y, consts.cameraUp.z, 0.f),
			math::Vector4(consts.cameraFwd.x, consts.cameraFwd.y, consts.cameraFwd.z, 0.f),
			math::Vector4(consts.cameraPos.x, consts.cameraPos.y, consts.cameraPos.z, 1.f)
		);

		const auto& prevPosition = cameraTransform->GetPosition(TransformState::Previous);
		const auto& prevForward = cameraTransform->GetForward(TransformState::Previous);
		const auto& prevUp = cameraTransform->GetUp(TransformState::Previous);
		const auto& prevRight = cameraTransform->GetRight(TransformState::Previous);

		math::Matrix cameraViewToWorldPrev(
			math::Vector4(prevRight.x, prevRight.y, prevRight.z, 0.f),
			math::Vector4(prevUp.x, prevUp.y, prevUp.z, 0.f),
			math::Vector4(prevForward.x, prevForward.y, prevForward.z, 0.f),
			math::Vector4(prevPosition.x, prevPosition.y, prevPosition.z, 1.f)
		);

		math::Matrix cameraViewToPrevCameraView;
		calcCameraToPrevCamera(cameraViewToPrevCameraView, cameraViewToWorld, cameraViewToWorldPrev);

		math::Matrix clipToPrevCameraView;
		//matrixMul(clipToPrevCameraView, consts.clipToCameraView, cameraViewToPrevCameraView);
		clipToPrevCameraView = consts.clipToCameraView * cameraViewToPrevCameraView;

		//matrixMul(consts.clipToPrevClip, clipToPrevCameraView, cameraViewToClipPrev);
		consts.clipToPrevClip = clipToPrevCameraView * _currentCamera->GetProjectionMatrixPrev();

		//matrixFullInvert(consts.prevClipToClip, consts.clipToPrevClip);
		consts.prevClipToClip = consts.clipToPrevClip.Invert();

#else
		consts.clipToPrevClip = reprojectionMatrix;
		consts.prevClipToClip = reprojectionMatrix.Invert();
#endif

		consts.depthInverted = false;
		consts.jitterOffset = _taa.GetJitterOffset(_currentCamera->GetViewport().width, _currentCamera->GetViewport().height);
		//consts.mvecScale = { 1.0f / m_RenderingRectSize.x , 1.0f / m_RenderingRectSize.y }; // This are scale factors used to normalize mvec (to -1,1) and donut has mvec in pixel space
		
		consts.reset = false;
		consts.motionVectors3D = false;
		consts.motionVectorsInvalidValue = FLT_MIN;
		consts.motionVectorsJittered = false;

		sl->SetCommonConstants(consts);
	}

	void SceneRenderer::RenderPostProcessing(SceneFlags flags)
	{
		PROFILE();

		bool canPostProcess = (flags & SceneFlags::PostProcessingEnabled) != (SceneFlags)0;

		if (canPostProcess)
		{
			if (g_pEnv->_ssaoProvider)
			{
				g_pEnv->_ssaoProvider->ApplyAmbientOcclusion(
					_currentCamera,
					_gbuffer.GetDepthBuffer(),
					nullptr/*_gbuffer.GetNormal()*/,
					_beautyRT);
			}

			//_gbuffer.GetDiffuse()->CopyTo(_compositionRT);

			RenderLights();
			RenderFog();
			//RenderWater();
			RenderVolumetricLighting();		

			RenderSSR();

			if (r_taa._val.b)
			{
				_taa.Resolve(_beautyRT, _beautyRT, _gbuffer.GetVelocity(), _gbuffer.GetNormal(), g_pEnv->_uiManager->GetRenderer());
			}
			
			_bloomEffect->Render(_currentCamera, _beautyRT, _beautyRT);
			
			//_beautyRT->GetPixels(_denoiseFD.colour);
			//_gbuffer.GetNormal()->GetPixels(_denoiseFD.normals);
			//_gbuffer.GetDiffuse()->GetPixels(_denoiseFD.albedo);

			_denoiseFD.camera = _currentCamera;

			g_pEnv->_denoiserProvider->BuildFrameData(_denoiseFD, _beautyRT, _gbuffer.GetNormal(), _gbuffer.GetDiffuse());

			g_pEnv->_denoiserProvider->FilterFrame(_denoiseFD, _beautyRT);
		}
		else
		{
			g_pEnv->_ssaoProvider->ApplyAmbientOcclusion(
				_currentCamera,
				_gbuffer.GetDepthBuffer(),
				nullptr/*_gbuffer.GetNormal()*/,
				_beautyRT);

			//_gbuffer.GetDiffuse()->CopyTo(_compositionRT);
			RenderLights();
		}

		// Switch back to the back buffer
		//g_pEnv->_graphicsDevice->SetRenderTarget(g_pEnv->_graphicsDevice->GetBackBuffer());
		

		

		if (auto guiRenderer = g_pEnv->_uiManager->GetRenderer(); guiRenderer != nullptr)
		{
			guiRenderer->StartFrame();

			if (canPostProcess)
			{
#if 1
				if (_currentCamera->IsDLSSEnabled() && g_pEnv->_streamlineProvider != nullptr)
				{
					_dlssTarget->ClearRenderTargetView(math::Color(0, 0, 0, 0));

						g_pEnv->_streamlineProvider->PrepareFrameResources(
							_beautyRT->GetNativePtr(),
							_dlssTarget->GetNativePtr(),
							_gbuffer.GetVelocity()->GetNativePtr(),
							_gbuffer.GetDepthBuffer()->GetNativePtr(),
							g_pEnv->_graphicsDevice->GetNativeDeviceContext());

						g_pEnv->_streamlineProvider->EvaluateFeature(StreamlineFeature::DLSS, g_pEnv->_graphicsDevice->GetNativeDeviceContext());

					g_pEnv->_graphicsDevice->SetRenderTarget(_currentCamera->GetRenderTarget());

					RenderOverlays(flags, _dlssTarget, _currentCamera->GetRenderTarget());
				}
				else
#endif
				{

					g_pEnv->_graphicsDevice->SetRenderTarget(_currentCamera->GetRenderTarget());

					RenderOverlays(flags, _beautyRT, _currentCamera->GetRenderTarget());
				}				
			}
			else
			{
				g_pEnv->_graphicsDevice->SetRenderTarget(_currentCamera->GetRenderTarget());
				g_pEnv->_graphicsDevice->SetViewport(g_pEnv->_graphicsDevice->GetBackBufferViewport());
				guiRenderer->FullScreenTexturedQuad(_beautyRT);
			}

			guiRenderer->EndFrame();
		}		
	}

	void SceneRenderer::RenderOverlays(SceneFlags flags, ITexture2D* beauty, ITexture2D* renderTarget)
	{
		bool canPostProcess = (flags & SceneFlags::PostProcessingEnabled) != (SceneFlags)0;

		if (!canPostProcess || !_currentCamera)
			return;

		g_pEnv->_graphicsDevice->SetViewport(g_pEnv->_graphicsDevice->GetBackBufferViewport());

		if (auto guiRenderer = g_pEnv->_uiManager->GetRenderer(); guiRenderer != nullptr)
		{
			guiRenderer->StartFrame();

			//guiRenderer->FullScreenTexturedQuad(beauty, _tonemapShader);
			//renderTarget->CopyTo(beauty);

			//guiRenderer->FullScreenTexturedQuad(beauty);

			

			guiRenderer->FullScreenTexturedQuad(beauty, _colourGradingShader.get());
			renderTarget->CopyTo(beauty);

			guiRenderer->FullScreenTexturedQuad(beauty, _vignetteShader.get());
			renderTarget->CopyTo(beauty);

			guiRenderer->FullScreenTexturedQuad(beauty, _chromaticAberrationShader.get());
			renderTarget->CopyTo(beauty);

			

			if (r_fxaa._val.i32 && canPostProcess)
				guiRenderer->FullScreenTexturedQuad(beauty, _fxaa.get());
			else
				guiRenderer->FullScreenTexturedQuad(beauty);


			// Render the debug targets
			if (r_debugScene._val.b && canPostProcess)
			{
				_gbuffer.RenderDebugTargets(10, 10, 150, guiRenderer);


				int32_t xpos = 10;

				guiRenderer->FillTexturedQuad(_ssrTexture, 150 * 5 + 10, 10, 150, 150, math::Color(1, 1, 1, 1));

				guiRenderer->FillTexturedQuad(_dlssTarget, 150 * 6 + 20, 10, 150, 150, math::Color(1, 1, 1, 1));

				for (auto& caster : _shadowCasters)
				{
					for (auto i = 0; i < caster->GetMaxSupportedShadowCascades(); ++i)
					{
						auto map = caster->GetShadowMap(i);

						if (!map)
							continue;

						map->RenderDebugTargets(xpos, 170, 150, guiRenderer);
						xpos += 160;
					}
				}

				// draw the shadow accumulator
				guiRenderer->FillTexturedQuad(_shadowMapsAccumulator, xpos, 170, 150, 150, math::Color(1, 1, 1, 1));
			}
		}
	}

	void SceneRenderer::RenderLights()
	{
		// switch to the light accumulation buffer
		g_pEnv->_graphicsDevice->SetRenderTarget(_lightAccumulationBuffer);
		_lightAccumulationBuffer->ClearRenderTargetView(math::Color(0, 0, 0, 0));

		g_pEnv->_graphicsDevice->SetBlendState(BlendState::Additive);
		//g_pEnv->_graphicsDevice->EnableDepthBuffer(false);

		RenderDirectionalLights();
		RenderPointLights();
		RenderSpotLights();

		//_lightAccumulationBuffer->BlendTo_Additive(_gbuffer.GetDiffuse());
		//_lightAccumulationBuffer->BlendTo_Additive(_compositionRT);

		//_lightAccumulationBuffer->CopyTo(_gbuffer.GetDiffuse());
		_lightAccumulationBuffer->CopyTo(_beautyRT);
		
		g_pEnv->_graphicsDevice->SetBlendState(BlendState::Opaque);
		//g_pEnv->_graphicsDevice->EnableDepthBuffer(true);
	}

	void SceneRenderer::RenderDirectionalLights()
	{
#if 1
		std::vector<DirectionalLight*> directionalLights;
		if (_currentScene->GetComponents<DirectionalLight>(directionalLights) == false)
			return;

		const auto& cameraPos = _currentCamera->GetEntity()->GetPosition();

		

		//g_pEnv->_graphicsDevice->SetRenderTarget(_compositionRT);

		// Clear the render target
		//
		//_compositionRT->ClearRenderTargetView(math::Color(0, 0, 0, 1));

		// Bind the gbuffer as a resource for the composition shader
		//
		

		if (auto guiRenderer = g_pEnv->_uiManager->GetRenderer(); guiRenderer != nullptr)
		{
			guiRenderer->StartFrame();

			// For each shadow caster in the scene we have to run a composition pass, so that each light's shadowmap can be incorporated
			//
			for (auto& caster : directionalLights)
			{
				DirectionalLight* light = (DirectionalLight*)caster;

				//_currentShadowCasterForComposition = caster;

				_gbuffer.BindAsShaderResource();
				g_pEnv->_graphicsDevice->SetTexture2D(_beautyRT);

				// Bind each shadowmap
				for (auto i = 0; i < light->GetMaxSupportedShadowCascades(); ++i)
				{
					auto shadowMap = light->GetShadowMap(i);

					if (!shadowMap)
					{
						g_pEnv->_graphicsDevice->SetTexture2D(nullptr);
						continue;
					}

					shadowMap->BindAsShaderResource();
				}
				//_currentShadowMapForComposition = shadowMap;
				//g_pEnv->_graphicsDevice->SetTexture2D(_shadowMapsAccumulator);

				SetupPerShadowCasterBuffer(light, false, 0, 0, 2, 0.0f);

				

				guiRenderer->FullScreenTexturedQuad(nullptr, _compositionShader.get());

				_lightAccumulationBuffer->CopyTo(_beautyRT);
			}


			guiRenderer->EndFrame();
		}
#endif
	}

	void SceneRenderer::RenderPointLights()
	{
		std::vector<PointLight*> pointLights;
		if (_currentScene->GetComponents<PointLight>(pointLights) == false)
			return;

		g_pEnv->_graphicsDevice->SetRenderTarget(_pointLightBuffer);
		_pointLightBuffer->ClearRenderTargetView(math::Color(0, 0, 0, 0));

		const auto& cameraPos = _currentCamera->GetEntity()->GetPosition();

		auto renderer = _sphereEntity->GetComponent<StaticMeshComponent>();
		renderer->SetMaterial(_pointLightMaterial);
		auto mesh = renderer->GetMesh();
		auto instance = mesh->GetInstance();

		bool renderedSphere = false;

		int32_t numPointLightsRendered = 0;

		for (auto& comp : pointLights)
		{
			PointLight* light = (PointLight*)comp;

			const auto& diffuse = light->GetDiffuseColour();

			if (diffuse.w <= 0.0f)
				continue;

			auto lightEnt = light->GetEntity();
			const auto& lightPos = lightEnt->GetPosition();
			const float lightRad = light->GetRadius();

			SetupPerShadowCasterBuffer(light, true, 0, numPointLightsRendered, 16, 0.0f);

			// if we're inside the sphere we should reverse culling
			/*if ((cameraPos - lightPos).Length() <= lightRad)
			{
				g_pEnv->_graphicsDevice->SetCullingMode(CullingMode::FrontFace);
			}
			else
			{
				g_pEnv->_graphicsDevice->SetCullingMode(CullingMode::BackFace);
			}*/



			if (renderedSphere == false)
			{
				instance->Start();

				renderer->RenderMesh(mesh.get(), MeshRenderFlags::MeshRenderNormal, numPointLightsRendered);

				renderedSphere = true;
			}

			_sphereEntity->SetPosition(lightPos);
			_sphereEntity->SetScale(math::Vector3(lightRad));

			//instance->Render(_sphereEntity->GetWorldTMTranspose(), _sphereEntity->GetWorldTMPrev().Transpose(), math::Color(1, 1, 1, 1));


			const auto& lightForward = lightEnt->GetComponent<Transform>()->GetForward();
			const math::Matrix lightMatrix = math::Matrix::CreateScale(lightRad) * math::Matrix::CreateWorld(lightPos, lightForward, math::Vector3::Up);
			instance->Render(
				_sphereEntity->GetWorldTM(),
				_sphereEntity->GetWorldTMTranspose()/*lightMatrix.Transpose()*/,
				_sphereEntity->GetWorldTMPrev().Transpose(),
				diffuse,
				math::Vector2(lightRad, light->GetLightStrength()));

			numPointLightsRendered++;

			
		}

		if (numPointLightsRendered > 0)
		{
			instance->Finish();

			//g_pEnv->_graphicsDevice->SetTexture2D(_beautyRT);

			D3DPERF_BeginEvent(0xFFFFFFFF, L"Begin PointLight");

			g_pEnv->_graphicsDevice->DrawIndexedInstanced(_sphereMesh->GetNumIndices(), numPointLightsRendered);

			D3DPERF_EndEvent();
		}

		_pointLightBuffer->BlendTo_Additive(_lightAccumulationBuffer);

		//g_pEnv->_graphicsDevice->SetCullingMode(CullingMode::BackFace); // restore culling
	}

#if 1
	void SceneRenderer::RenderSpotLights()
	{
		std::vector<SpotLight*> spotLights;
		if (_currentScene->GetComponents<SpotLight>(spotLights) == false)
			return;

		const auto& cameraPos = _currentCamera->GetEntity()->GetPosition();

		std::vector<SpotLight*> spotLightsInsideVolume, spotLightOutsideVolume;

		// sort the spot lights into two vectors, one for volumes the camera is inside of, one for outside
		for (auto& comp : spotLights)
		{
			SpotLight* light = (SpotLight*)comp;

			auto lightEnt = light->GetEntity();
			const auto& lightPos = lightEnt->GetPosition();
			const float lightRad = light->GetRadius();

			if ((cameraPos - lightPos).Length() <= lightRad)
			{
				spotLightsInsideVolume.push_back(light);
			}
			else
			{
				spotLightOutsideVolume.push_back(light);
			}
		}

		g_pEnv->_graphicsDevice->SetRenderTarget(_pointLightBuffer);
		_pointLightBuffer->ClearRenderTargetView(math::Color(0, 0, 0, 0));

		

		auto renderer = _sphereEntity->GetComponent<StaticMeshComponent>();
		renderer->SetMaterial(_spotLightMaterial);
		auto mesh = renderer->GetMesh();
		auto instance = mesh->GetInstance();
		bool renderedSphere = false;

		int32_t numPointLightsRendered = 0;

		auto renderLightList = [&](const std::vector<SpotLight*>& lights, CullingMode cullMode)
			{
				renderedSphere = false;

				for (auto& comp : lights)
				{
					SpotLight* light = (SpotLight*)comp;

					const auto& diffuse = light->GetDiffuseColour();

					if (diffuse.w <= 0.0f)
						continue;

					auto lightEnt = light->GetEntity();
					const auto& lightPos = lightEnt->GetPosition();
					const float lightRad = light->GetRadius();

					SetupPerShadowCasterBuffer(light, true, 0, numPointLightsRendered, 2, light->GetConeSize());

					if (renderedSphere == false)
					{
						instance->Start();

						_gbuffer.BindAsShaderResource();

						for (int32_t i = 0; i < 6; ++i)
						{
							auto shadowMap = light->GetShadowMap(i);

							if (shadowMap)
							{
								shadowMap->BindAsShaderResource();
							}
							else
								g_pEnv->_graphicsDevice->SetTexture2D(nullptr);
						}

						renderer->RenderMesh(mesh.get(), MeshRenderFlags::MeshRenderNormal, numPointLightsRendered);

						renderedSphere = true;
					}

					_sphereEntity->SetPosition(lightPos);
					_sphereEntity->SetScale(math::Vector3(lightRad));

					g_pEnv->_graphicsDevice->SetCullingMode(cullMode);
	
					const auto& lightForward = lightEnt->GetComponent<Transform>()->GetForward();
					const math::Matrix lightMatrix = math::Matrix::CreateScale(lightRad) * math::Matrix::CreateWorld(lightPos, lightForward, math::Vector3::Up);
					instance->Render(
						_sphereEntity->GetWorldTM(),
						_sphereEntity->GetWorldTMTranspose()/*lightMatrix.Transpose()*/,
						_sphereEntity->GetWorldTMPrev().Transpose(),
						diffuse,
						math::Vector2(lightRad, light->GetLightStrength()));

					numPointLightsRendered++;
				}

				instance->Finish();

				if (numPointLightsRendered > 0)
				{
					D3DPERF_BeginEvent(0xFFFFFFFF, L"Begin SpotLight");

					g_pEnv->_graphicsDevice->DrawIndexedInstanced(_sphereMesh->GetNumIndices(), numPointLightsRendered);

					D3DPERF_EndEvent();
				}

				
			};

		renderLightList(spotLightsInsideVolume, CullingMode::FrontFace);
		renderLightList(spotLightOutsideVolume, CullingMode::BackFace);

		
		g_pEnv->_graphicsDevice->SetCullingMode(CullingMode::BackFace);
		_pointLightBuffer->BlendTo_Additive(_lightAccumulationBuffer);
	}
#endif

	void SceneRenderer::RenderTransparent()
	{
		PROFILE();

		g_pEnv->_graphicsDevice->SetRenderTarget(_beautyRT, _gbuffer.GetDepthBuffer());

		_currentScene->RenderEntities(
			_currentCamera->GetPVS(),
			LAYERMASK(Layer::StaticGeometry),
			MeshRenderFlags::MeshRenderTransparency);

		//_beautyRT->CopyTo(_gbuffer.GetDiffuse());

		//g_pEnv->_graphicsDevice->SetCullingMode(CullingMode::BackFace);
	}

	void SceneRenderer::RenderFog()
	{
		if (!r_fog._val.i8)
			return;

		PROFILE();

		// Switch back to the back buffer
		_fogBuffer->ClearRenderTargetView(math::Color(0, 0, 0, 0));
		g_pEnv->_graphicsDevice->SetRenderTarget(_fogBuffer);

		if (auto guiRenderer = g_pEnv->_uiManager->GetRenderer(); guiRenderer != nullptr)
		{
			guiRenderer->StartFrame();

			// Render fog as a post process
			_gbuffer.BindAsShaderResource(_beautyRT);
			g_pEnv->_graphicsDevice->SetTexture2D(_atmosphereRT);
			guiRenderer->FullScreenTexturedQuad(nullptr, _fogEffect.get());

			//_fogBuffer->CopyTo(_gbuffer.GetDiffuse());
			_fogBuffer->CopyTo(_beautyRT);

			guiRenderer->EndFrame();
		}
	}

	void SceneRenderer::RenderVolumetricLighting()
	{
		if (!env_volumetricLighting._val.b)
			return;

		PROFILE();

		_volumetricLightingBuffer->ClearRenderTargetView(math::Color(0, 0, 0, 1));
		g_pEnv->_graphicsDevice->SetBlendState(BlendState::Additive);

		// Switch back to the back buffer
		g_pEnv->_graphicsDevice->SetRenderTarget(_volumetricLightingBuffer);

		const auto& bbvp = _currentCamera->GetViewport();

		// set the shadow viewport
		//
		D3D11_VIEWPORT vp;
		vp.TopLeftX = 0;
		vp.TopLeftY = 0;
		vp.Width = bbvp.width / 2;
		vp.Height = bbvp.height / 2;
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		g_pEnv->_graphicsDevice->SetViewport(vp);

		if (auto guiRenderer = g_pEnv->_uiManager->GetRenderer(); guiRenderer != nullptr)
		{
			guiRenderer->StartFrame();

			for (auto& caster : _shadowCasters)
			{
				if (caster->GetIsVolumetric() == false)
					continue;

				// Render fog as a post process
				_gbuffer.BindAsShaderResource();

				// Bind the shadow mappers
				/*for (auto i = 0; i < 6; ++i)
				{
					if (i >= caster->GetMaxSupportedShadowCascades())
						g_pEnv->_graphicsDevice->SetTexture2D(nullptr);
					else
					{
						if (caster->GetShadowMap(i))
							caster->GetShadowMap(i)->BindAsShaderResource();
					}
				}*/

				for (auto i = 0; i < 6; ++i)
				{
					auto shadowMap = caster->GetShadowMap(i);

					if (!shadowMap)
					{
						g_pEnv->_graphicsDevice->SetTexture2D(nullptr);
						continue;
					}

					shadowMap->BindAsShaderResource();
				}

				g_pEnv->_graphicsDevice->SetTexture2D(_blueNoise.get());

				bool forceCascade = caster->CastAs<SpotLight>() != nullptr;

				SetupPerShadowCasterBuffer(caster, forceCascade, 0, 0, 0, 0.0f);

				guiRenderer->FullScreenTexturedQuad(nullptr, _volumetricLighting.get());
			}

			// the blur should have the gbuffer as it needs to sample depth
			
			//_volumetricBlur->Render(guiRenderer);

			g_pEnv->_graphicsDevice->SetViewport(*bbvp.Get11());			

#if 0 // use bilateral
			_gbuffer.BindAsShaderResource();
			_volumetricLightingBuffer->BlendTo_Additive(_compositionRT);
#else
			_volumetricLightingBuffer->BlendTo_Additive(_beautyRT, nullptr);
#endif


			guiRenderer->EndFrame();
		}

		g_pEnv->_graphicsDevice->SetBlendState(BlendState::Opaque);
	}

	void SceneRenderer::RenderSSR()
	{
		PROFILE();

		if (!r_ssr._val.b)
			return;

		_ssrTexture->ClearRenderTargetView(math::Color(0, 0, 0, 0));
		_ssrHitInfo->ClearRenderTargetView(math::Color(0, 0, 0, 0));

		g_pEnv->_graphicsDevice->SetRenderTargets({ _ssrTexture, _ssrHitInfo });
		
		const auto& bbvp = _currentCamera->GetViewport();
		// set the shadow viewport
		//
		D3D11_VIEWPORT vp;
		vp.TopLeftX = 0;
		vp.TopLeftY = 0;
		vp.Width = bbvp.width;// / 2;
		vp.Height = bbvp.height;// / 2;
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		g_pEnv->_graphicsDevice->SetViewport(vp);

		if (auto guiRenderer = g_pEnv->_uiManager->GetRenderer(); guiRenderer != nullptr)
		{
			guiRenderer->StartFrame();

			// Render fog as a post process
			_gbuffer.BindAsShaderResource();

			g_pEnv->_graphicsDevice->SetTexture2D(_beautyRT);
			g_pEnv->_graphicsDevice->SetTexture2D(_blueNoise.get());
			g_pEnv->_graphicsDevice->SetTexture2D(_ssrHistory);
			g_pEnv->_graphicsDevice->SetTexture2D(_gbuffer.GetVelocity());

			guiRenderer->FullScreenTexturedQuad(nullptr, _ssrShader.get());

			

#if 1 // resolve it

			_ssrResolved->ClearRenderTargetView(math::Color(0, 0, 0, 0));
			g_pEnv->_graphicsDevice->SetRenderTarget(_ssrResolved);

			g_pEnv->_graphicsDevice->SetTexture2D(_ssrHistory);
			g_pEnv->_graphicsDevice->SetTexture2D(_gbuffer.GetVelocity());
			g_pEnv->_graphicsDevice->SetTexture2D(_ssrHitInfo);
			guiRenderer->FullScreenTexturedQuad(_ssrTexture, _ssrResolve.get());

			_ssrResolved->CopyTo(_ssrHistory);
#endif

			

			//_ssrBlur->Render(guiRenderer);

			g_pEnv->_graphicsDevice->SetViewport(*bbvp.Get11());
			
			// to denoise
			g_pEnv->_graphicsDevice->SetRenderTarget(_beautyRT);
			guiRenderer->FullScreenTexturedQuad(_ssrResolved, nullptr/*_basicDenoise*/);
			// without denoising
			//_ssrTexture->CopyTo(_beautyRT);
			// 
			

			//_ssrTexture->BlendTo_Alpha(_beautyRT);
			

			//_ssrResolved->BlendTo_Additive(_compositionRT);
			
			//_ssrTexture->BlendTo_Additive(_compositionRT);

			//_compositionRT->CopyTo(_gbuffer.GetDiffuse());

			/*_ssrTexture->BlendTo_NonPremultiplied(_compositionRT);
			_ssrTexture->BlendTo_NonPremultiplied(_gbuffer.GetDiffuse());*/



			guiRenderer->EndFrame();
		}
	}

	void SceneRenderer::RenderVolumetricClouds()
	{

	}

	const GBuffer* SceneRenderer::GetGBuffer()
	{
		return &_gbuffer;
	}

	const Light* SceneRenderer::GetCurrentShadowCaster()
	{
		return _currentShadowCasterForComposition;
	}

	const ShadowMap* SceneRenderer::GetCurrentShadowMap()
	{
		return _currentShadowMapForComposition;
	}
}