"InputLayout"
{
	PosTexColour
}
"VertexShaderIncludes"
{
	UICommon
}
"PixelShaderIncludes"
{
	UICommon
	ShadowUtils
	LightingUtils
	Atmosphere
	AtmospherePhysical
	// SkyViewLutParamsToUv, for the sky IBL environment lookup.
	AtmosphereCommon
	// Octahedral environment atlas helpers (SampleEnvAtlas), for sky IBL.
	EnvMapCommon
	PBRutils
	CloudCommon
}
"VertexShader"
{
	UIPixelInput ShaderMain(UIVertexInput input)
	{
		UIPixelInput output;

		output.position = input.position;
		output.texcoord = input.texcoord;
		output.positionSS = output.position;

		return output;
	}
}
"PixelShader"
{
	GBUFFER_RESOURCE(0, 1, 2, 3, 4);
	Texture2D g_beautyTex : register(t5);
	SHADOWMAPS_RESOURCE(6);
	// Cached cloud shadow map (t12; see CloudShadowMap.shader). t13 is left
	// free - it used to hold the second cloud noise volume for the per-pixel
	// slab re-march this replaced.
	Texture2D g_cloudShadowMap : register(t12);
	// t14 = features RT (bound explicitly by RenderDirectionalLights).
	// Prefiltered sky environment atlas for image-based lighting, bound explicitly at
	// t15 (SkyEnvMap.shader's output: octahedral rows, one per roughness level - see
	// EnvMapCommon). A null bind reads as black, which degrades to the old no-IBL
	// behaviour rather than breaking the pass.
	Texture2D g_iblSkyEnvAtlas : register(t15);
	// The frame's selected reflection probe atlas (same octahedral layout).
	// Only read when g_probeCenter.w > 0.5; a null bind reads black.
	Texture2D g_iblProbeAtlas : register(t16);
	// Second-nearest probe, cross-faded with the first so moving between probe
	// volumes doesn't snap the environment.
	Texture2D g_iblProbeAtlas2 : register(t17);
	// P1-C: 1x9 SH irradiance coefficients projected from the sky atlas. This is
	// the real cosine-convolved diffuse term; the atlas's roughest row was only
	// ever a stand-in for it.
	Texture2D g_iblSkySHTex : register(t18);
	// Per-probe SH irradiance for the two selected probes. Unlike the sky SH these
	// are integrated from what each probe actually sees, so they already encode
	// their own occlusion - an indoor probe's irradiance knows the roof is solid.
	Texture2D g_iblProbeSH  : register(t19);
	Texture2D g_iblProbeSH2 : register(t20);
	// P1-B: split-sum DFG table. rg = F0 scale/bias, b = single-scatter energy.
	Texture2D g_dfgLut      : register(t21);
	// t22 = DiffuseGI's bilateral-blurred voxel-occlusion AO (previous frame -
	// GI renders after this pass; one frame of latency, same as the GI-AO
	// provider accepts). .r = occlusion, 1 = fully blocked. Only bound (and
	// only sampled - g_giComposeParams.z gates) for the main camera.
	Texture2D g_giAoTex     : register(t22);
	// Material-features RT (model id + per-model parameters). t14 is the first
	// free slot after the gbuffer (0-4), beauty (5), shadowmaps (6-11), and cloud
	// 3D noise (12-13). C++ side binds via GraphicsDevice::SetTexture2D(14, ...).
	GBUFFER_FEATURES_RESOURCE(14)
	

	SamplerState g_textureSampler : register(s0);
	SamplerComparisonState g_cmpSampler : register(s1);
	SamplerState g_pointSampler : register(s2);
	SamplerState g_mirrorSampler : register(s3);







	// SampleSkyEnv's one caller moved into EnvMapCommon::EvaluateEnvSpecular, which
	// takes the atlas as a parameter so the SSR resolve can call it too.

	// ProbeWeight / ProbeSpecularDir moved to EnvMapCommon.shader, alongside the
	// EvaluateEnvSpecular that both this pass and the SSR resolve now call.

	// Cloud shadow from the cached top-down transmittance map: one filtered
	// tap instead of re-marching the cloud slab per pixel (the old path also
	// used a hand-copied density function that drifted from the clouds
	// actually drawn - the map is rendered from the shared one).
	float CalculateCloudShadow(float3 worldPos, float3 sunDir)
	{
		// sunDir is unused: the map carries the sun direction it was rendered with.
		return SampleCloudShadowMap(g_cloudShadowMap, g_textureSampler, worldPos);
	}

	void CalculateDiffuseAndSpecularLighting(
		float shadowValue,
		float3 pixelNormal,
		float3 pixelSpecular,
		float3 pixelColour,
		float3 lightDir,
		float3 eyeDir,
		float shinyPower,
		float shininessStrength,		
		float lightMultiplier,
		inout float3 diffuse, 
		inout float3 specular)
	{
		float lightIntensity = saturate(dot(pixelNormal, lightDir));

		if (lightIntensity > 0.0f /*&& shadowValue > 0.0f*/)
		{
			diffuse += pixelColour * lightIntensity * shadowValue;

			diffuse = /*saturate*/(diffuse) * lightMultiplier;

			// Calculate the reflection vector based on the light intensity, normal vector, and light direction.
			float3 reflection = normalize(2 * lightIntensity * pixelNormal - lightDir);

			// Determine the amount of specular light based on the reflection vector, viewing direction, and7 specular power.
			specular = pow(saturate(dot(reflection, eyeDir)), shinyPower);// * shininessStrength;

			specular = specular * pixelSpecular * shadowValue;

			specular = /*saturate*/(specular) * lightMultiplier;
		}
	}

	float4 ShaderMain(UIPixelInput input) : SV_TARGET
	{
		float2 texcoord = input.texcoord;

		float2 screenPos = float2(input.position.x / (float)g_screenWidth, input.position.y / (float)g_screenHeight);

		// Sample the gbuffer
		//
		float4 pixelColour = g_beautyTex.Sample(g_pointSampler, screenPos);
		float4 pixelNormal = GBUFFER_NORMAL.Sample(g_pointSampler, screenPos);
		float4 pixelPosWS = GBUFFER_POSITION.Sample(g_pointSampler, screenPos);

		// Rain-drip cell-grid diagnostic. Runs in the deferred lighting pass so
		// it bypasses every per-material surface shader path - if a pixel has
		// valid GBuffer data (any opaque surface, regardless of which shader
		// wrote it), it gets the grid here. Lets us tell whether a mesh that
		// doesn't show the grid is failing to write the GBuffer or just using
		// a surface shader without my injected per-PS debug check.
		if (g_rainDripDebug > 0.5f && pixelNormal.w > 0.0f)
		{
			const float3 nWS = normalize(pixelNormal.xyz);
			const float  isHoriz = step(0.5f, nWS.y);
			const float3 grid = RainDripsCellGridDebug(nWS, pixelPosWS.xyz, g_time, isHoriz);
			return float4(grid, 1.0f);
		}
		//float4 pixelMaterial = GBUFFER_SPECULAR.Sample(g_pointSampler, screenPos);
		
		//return float4(pixelColour.aaa, 1.0f);

		// sky
		if(pixelColour.a == -1 /* || pixelPosWS.a > 0.0f */)
		{
			//return float4(1, 0, 0, 1.0f);
			return float4(pixelColour.rgb, 1.0f);
		}
		
		float3 lightDir = -normalize(g_lightDirection.xyz);
		float3 eyeVector = normalize(g_eyePos.xyz - pixelPosWS.xyz);

		ShadowInput shadow;
		shadow.pixelDepth = pixelNormal.w;
		shadow.positionWS = pixelPosWS;
		shadow.positionSS = input.position.xy;
		shadow.samples = g_shadowConfig.samples;

		float d = dot(normalize(pixelNormal.xyz), normalize(g_shadowCasterLightDir.xyz));
		float bias = g_shadowConfig.biasMultiplier* (1.0 - d);// max(0.000002 * (1.0 - d), 0.0000002); // seems good
			//float bias = 0.00011 * (1.0 - d);// max(0.000002 * (1.0 - d), 0.0000002);

		float depthValue = CalculateShadows(shadow, g_cmpSampler, g_pointSampler, SHADOWMAPS, bias);
		depthValue *= CalculateCloudShadow(pixelPosWS.xyz, lightDir);

		// Screen-space contact shadow. Fills the near-camera detail gap PCSS cascades
		// can't resolve (fine geometry contact like fingers, foliage, hair). Marches
		// the depth buffer toward the sun; if a closer pixel intercepts the ray
		// before our shading point would have reached light, we're contact-shadowed.
		// Cheap (configurable step count), runs in the same deferred light pass so no
		// extra bandwidth. Multiply into the cascade term - both must agree the pixel
		// is lit for it to be lit. Settings come from r_contactShadows* HVars, packed
		// into g_shadowConfig.contactShadowParams by SetupPerShadowCasterBuffer; the
		// .x channel is the enable flag, zero on non-directional casters so the
		// branch naturally collapses.
		// .x carries the fade-START distance (metres). Zero = disabled. The fade
		// runs out to 1.5x that distance via smoothstep, so contact shadows
		// concentrate near the camera and don't add screen-space-jitter noise to
		// distant terrain (where TAA can't reconcile the noise across camera
		// motion - the previously-observed volumetric-terrain mid-depth flicker).
		if (g_shadowConfig.contactShadowParams.x > 0.0f)
		{
			const float fadeStart = g_shadowConfig.contactShadowParams.x;
			const float fadeEnd = fadeStart * 1.5f;
			const float fadeWeight = 1.0f - smoothstep(fadeStart, fadeEnd, pixelNormal.w);
			if (fadeWeight > 0.001f)
			{
				const float contactShadow = ScreenSpaceContactShadow(
					pixelPosWS.xyz,
					lightDir,
					normalize(pixelNormal.xyz),
					GBUFFER_NORMAL,
					g_pointSampler,
					input.position.xy,
					(int)g_shadowConfig.contactShadowParams.y,
					g_shadowConfig.contactShadowParams.z,
					g_shadowConfig.contactShadowParams.w);
				// Lerp between unshadowed (1.0) and contactShadow result based on
				// distance fade so the multiply into depthValue is identity past
				// the fade-end distance.
				depthValue *= lerp(1.0f, contactShadow, fadeWeight);
			}
		}

		float3 legacySunColour = getSunColour();
		float3 physicalSunColour = ComputePhysicalSunColour(pixelPosWS.xyz, lightDir);
		const float legacySunLuma = dot(legacySunColour, float3(0.2126f, 0.7152f, 0.0722f));
		const float physicalSunLuma = max(dot(physicalSunColour, float3(0.2126f, 0.7152f, 0.0722f)), 1e-4f);
		physicalSunColour *= legacySunLuma / physicalSunLuma;

		float4 pbr = CalculatePBR(
			GBUFFER_SPECULAR,
			g_pointSampler,
			screenPos,
			pixelNormal.xyz,
			pixelPosWS.xyz,
			lightDir,
			physicalSunColour,
			pixelColour.rgb,
			depthValue,
			g_globalLight[0]);

		// ---- GI ambient ownership (structural GI fix) -------------------------
		// CalculatePBR just added the legacy flat ambient (albedo * ambientLight)
		// and the IBL block below adds sky SH diffuse - historically the GI
		// composite then stacked additively on top as a THIRD ambient fill,
		// too small relative to the other two to read, and auto-exposure
		// normalized away what remained. Instead: hand a fraction of the flat
		// ambient budget to GI (subtract it here; GI's composite adds
		// structured bounce back later in the frame), and darken what remains
		// by the GI voxel occlusion so covered areas (interiors, underpasses,
		// overhangs) stop receiving full sky/ambient fill. The darkening is
		// what survives exposure and makes GI visibly shape the image.
		float giVis = 1.0f;
		float giOcc = 0.0f;
		if (g_giComposeParams.z > 0.5f)
		{
			giOcc = saturate(g_giAoTex.Sample(g_pointSampler, screenPos).r);
			giVis = saturate(1.0f - giOcc * saturate(g_giComposeParams.y));
		}
		{
			const float3 ambientFlat = pixelColour.rgb * g_atmosphere.ambientLight.rgb;
			const float giHandoff = saturate(g_giComposeParams.x);
			// Remaining flat ambient should be ambientFlat * (1-handoff) * giVis;
			// CalculatePBR added the full term, so subtract the difference.
			pbr.rgb -= ambientFlat * (1.0f - (1.0f - giHandoff) * giVis);
		}

		// Extended shading-model lobes (clearcoat / anisotropic / sheen). The
		// features RT carries the model id + per-model parameters - see
		// ApplyMaterialFeatures for the param layout. Standard PBR + SSS take the
		// early-out and add nothing here. We re-sample the metallic/roughness
		// gbuffer for the perceptual roughness used by the aniso/sheen lobes; the
		// cost is one extra sample on the same texture the PBR path already
		// resolved, so it stays in cache.
		// ---- Sky image-based lighting -------------------------------------------------
		// The engine had no IBL at all: ambient was a flat albedo * ambientLight constant,
		// diffuse-only, so nothing gave a surface an environment response. A wall facing a
		// window stayed dark, and SSR then faithfully reflected that dark wall - which is
		// why glossy floors indoors look black even though SSR is working correctly.
		//
		// This is the split-sum approximation with the sky-view LUT standing in for the
		// environment: specular takes the LUT along the reflection vector weighted by the
		// env-BRDF (EnvBRDFApprox for now; P1-B replaces it with a real DFG LUT), diffuse
		// takes the LUT along the normal as an irradiance proxy. Reflection probes (P1-D)
		// slot in here by replacing the LUT lookup with a local cubemap where one covers the
		// pixel, falling back to this sky term outside probe influence.
		//
		// Caveat this does NOT solve: there is no occlusion on the environment term, so
		// indoors it lights as though the sky were fully visible. AO damps it, but the real
		// answer is probes. Hence the separate diffuse strength, defaulting to 0 - diffuse
		// sky indoors floods a room, whereas the specular term is the part that actually
		// restores the missing reflections.
		{
			const float4 matSample = GBUFFER_SPECULAR.Sample(g_pointSampler, screenPos);
			const float metallic = matSample.r;
			const float perceptualRoughness = clamp(matSample.g, MinRoughness, 1.0f);

			const float3 N = normalize(pixelNormal.xyz);
			const float3 V = normalize(g_eyePos.xyz - pixelPosWS.xyz);

			const float3 diffuseColour = pixelColour.rgb * (1.0f - f0) * (1.0f - metallic);

			// ---- Diffuse ---------------------------------------------------------
			// Diffuse comes from SH irradiance (P1-C), not from the atlas's roughest
			// row. A GGX roughness-1 prefilter is a wide specular lobe, not a cosine
			// convolution - using it as diffuse gave a flat wash with no directional
			// falloff. Order-2 SH reconstructs Lambertian irradiance to ~1% and costs
			// 9 taps of a 1x9 texture.
			//
			// Diffuse stays in this pass unconditionally. SSR's diffuse channel is a
			// screen-space DELTA over the voxel-GI baseline, not a competing estimate
			// of environment irradiance, so there is nothing for the resolve to
			// compose it against - only the specular term has two rival estimators.
			const float3 skyDiff = ShIrradiance(g_iblSkySHTex, g_textureSampler, N);

			// Horizon fade: the sky LUT carries no ground radiance, so a downward-facing
			// direction would otherwise light undersides with horizon sky. The SH term
			// already encodes the sky's own directional distribution, so it needs a far
			// gentler fade than the specular lookup does.
			const float diffHorizon = saturate(N.y * 0.35f + 0.65f);

			float3 envDiffRadiance = skyDiff * diffHorizon * g_iblSkyDiffuse;

			// GI sky occlusion: the sky SH has no idea the roof is solid - the
			// voxel field does. Applied BEFORE the probe lerp so probe
			// irradiance (which already encodes its own occlusion) is not
			// double-darkened. This is the term that finally lets interiors
			// and underpasses go dark instead of receiving full-sky fill.
			envDiffRadiance *= giVis;

			// A probe's SH is integrated from what that probe actually sees, so it
			// already encodes its own occlusion - an indoor probe's irradiance knows
			// the roof is solid. Sky SH indoors is what floods a room blue. Specular
			// and diffuse take SEPARATE strengths (g_iblParams.z / .w): driving both
			// from the probe strength washed interiors flat cream.
			{
				const float w1 = ProbeWeight(pixelPosWS.xyz, g_probeCenter,  g_probeExtents);
				const float w2 = ProbeWeight(pixelPosWS.xyz, g_probeCenter2, g_probeExtents2);
				const float wSum = w1 + w2;

				if (wSum > 0.0f)
				{
					const float n1 = w1 / wSum;
					const float n2 = w2 / wSum;
					const float coverage = saturate(wSum);

					float3 probeDiff = 0.0f.xxx;
					if (w1 > 0.0f)
						probeDiff += n1 * ShIrradiance(g_iblProbeSH, g_textureSampler, N);
					if (w2 > 0.0f)
						probeDiff += n2 * ShIrradiance(g_iblProbeSH2, g_textureSampler, N);

					envDiffRadiance = lerp(envDiffRadiance, probeDiff * g_iblParams.w, coverage);
				}
			}

			pbr.rgb += envDiffRadiance * diffuseColour;

			// ---- Specular --------------------------------------------------------
			// Owned by the SSR resolve when it is running (g_iblComposeInResolve).
			// Adding it here as well is what made the two systems STACK: this pass
			// runs first, the resolve blends additively onto beauty, so a pixel
			// whose ray hit got environment + screen reflection double-counted while
			// a pixel whose ray missed got environment here and nothing there. The
			// resolve is the only place both estimates exist at once, which is the
			// only place lerp(environment, screen, confidence) can be written.
			//
			// The flag is 0 - and this pass keeps the term - whenever no resolve
			// will run: probe capture faces, secondary cameras, r_ssr 0, a scene
			// with nothing reflective, or r_iblComposeSSR 0. See
			// SceneRenderer::ShouldComposeEnvSpecularInResolve.
			if (g_iblComposeInResolve < 0.5f)
			{
				float3 envSpecRadianceUnused;
				float3 specularReflectanceUnused;
				// GI specular occlusion: the env-spec term has no occlusion of
				// its own, so indoors every matte surface reflected full sky -
				// the blue wash in window rooms. Same voxel AO as the diffuse
				// occlusion above, separate strength (g_giComposeParams.w).
				const float giSpecVis = saturate(1.0f - giOcc * saturate(g_giComposeParams.w));
				pbr.rgb += EvaluateEnvSpecular(
					g_iblSkyEnvAtlas, g_iblProbeAtlas, g_iblProbeAtlas2, g_dfgLut,
					g_textureSampler,
					N, V, pixelPosWS.xyz,
					pixelColour.rgb, metallic, perceptualRoughness,
					g_iblParams,
					float2(g_useDfgLut, g_useMultiScatter),
					g_probeCenter, g_probeExtents, g_probeCenter2, g_probeExtents2,
					envSpecRadianceUnused,
					specularReflectanceUnused) * giSpecVis;
			}
		}
		// -------------------------------------------------------------------------------

		const float4 features = GBUFFER_FEATURES.Sample(g_pointSampler, screenPos);
		const uint modelId = DecodeMaterialModelId(features.r);
		if (modelId != MATERIAL_MODEL_STANDARD)
		{
			const float perceptualRoughnessForFeatures = clamp(GBUFFER_SPECULAR.Sample(g_pointSampler, screenPos).g, MinRoughness, 1.0f);
			const float3 viewDir = g_eyePos.xyz - pixelPosWS.xyz;
			const float3 featureBonus = ApplyMaterialFeatures(
				modelId,
				float4(features.g, features.b, features.a, DecodePackedModelParamW(features.r)),
				normalize(pixelNormal.xyz),
				viewDir,
				lightDir,
				physicalSunColour * g_globalLight[0],
				perceptualRoughnessForFeatures,
				depthValue,
				1.0f);
			pbr.rgb += featureBonus;
		}

		// Lightning flash. Briefly boost overall brightness with a cool tint
		// (~6500K bias toward blue) when g_weatherSurface.lightningFlash > 0.
		// The weather controller already drives this between 0 and 1; values
		// fade off quickly so the flash reads as a 50-100 ms strobe rather than
		// a sustained brighten. Cap at 3.5x boost - any higher and HDR display
		// modes lose the highlight headroom for the actual sun. Applied AFTER
		// PBR + feature lobes so every shading path catches it uniformly.
		if (g_weatherSurface.lightningFlash > 0.001f)
		{
			const float3 flashTint = float3(0.85f, 0.92f, 1.10f);
			const float  flashMul  = 1.0f + g_weatherSurface.lightningFlash * 2.5f;
			pbr.rgb *= flashTint * flashMul;
		}

		if(pixelPosWS.a > 0.0f)
		{
			return float4(pbr.rgb + pixelColour.rgb * pixelPosWS.a, pbr.a);
		}

		return pbr;
	}
}
