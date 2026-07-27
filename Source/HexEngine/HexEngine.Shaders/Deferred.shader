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
	Texture3D g_cloudShapeNoise : register(t12);
	Texture3D g_cloudDetailNoise : register(t13);
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
	// Material-features RT (model id + per-model parameters). t14 is the first
	// free slot after the gbuffer (0-4), beauty (5), shadowmaps (6-11), and cloud
	// 3D noise (12-13). C++ side binds via GraphicsDevice::SetTexture2D(14, ...).
	GBUFFER_FEATURES_RESOURCE(14)
	

	SamplerState g_textureSampler : register(s0);
	SamplerComparisonState g_cmpSampler : register(s1);
	SamplerState g_pointSampler : register(s2);
	SamplerState g_mirrorSampler : register(s3);

	cbuffer CloudConstants : register(b4)
	{
		float4 g_cloudBoundsMin;
		float4 g_cloudBoundsMax;
		float4 g_cloudParams0; // x=density, y=coverage, z=erosion, w=maxDistance
		float4 g_cloudParams1; // x=absorption, y=powder, z=anisotropy, w=stepScale
		float4 g_cloudParams2; // x=shapeScale, y=detailScale, z=windSpeed, w=animationSpeed
		float4 g_cloudParams3; // x=viewAbsorption, y=ambientStrength, z=shadowFloor, w=phaseBoost
		float4 g_cloudParams4; // x=silverLiningStrength, y=silverLiningExponent, z=multiScatterStrength, w=heightTintStrength
		float4 g_cloudParams5; // x=tintWarmth, y=skyTintInfluence, z=directionalDiffuse, w=ambientOcclusion
		float4 g_cloudWindDirection; // xyz=wind direction, w=quality preset
		float4 g_cloudWindOffset; // xyz=accumulated wind offset, w=reserved
		float4 g_cloudMarch; // x=view steps, y=light steps, z=ground shadow steps, w=ground shadow strength
	};

	float2 RayBoxDist(float3 boundsMin, float3 boundsMax, float3 rayOrigin, float3 rayDir)
	{
		float3 safeDir = rayDir;
		safeDir.x = abs(safeDir.x) < 1e-5f ? (safeDir.x < 0.0f ? -1e-5f : 1e-5f) : safeDir.x;
		safeDir.y = abs(safeDir.y) < 1e-5f ? (safeDir.y < 0.0f ? -1e-5f : 1e-5f) : safeDir.y;
		safeDir.z = abs(safeDir.z) < 1e-5f ? (safeDir.z < 0.0f ? -1e-5f : 1e-5f) : safeDir.z;
		const float3 invDir = 1.0f / safeDir;
		const float3 t0 = (boundsMin - rayOrigin) * invDir;
		const float3 t1 = (boundsMax - rayOrigin) * invDir;
		const float3 tmin = min(t0, t1);
		const float3 tmax = max(t0, t1);

		const float dstA = max(max(tmin.x, tmin.y), tmin.z);
		const float dstB = min(tmax.x, min(tmax.y, tmax.z));

		const float dstToBox = max(0.0f, dstA);
		const float dstInsideBox = max(0.0f, dstB - dstToBox);

		return float2(dstToBox, dstInsideBox);
	}

	float SampleCloudDensity(float3 worldPos, float3 boundsMin, float3 boundsMax, float3 windOffset)
	{
		const float3 boundsSize = max(boundsMax - boundsMin, 1e-3f.xxx);
		const float3 localUVW = (worldPos - boundsMin) / boundsSize;

		if (any(localUVW < 0.0f.xxx) || any(localUVW > 1.0f.xxx))
			return 0.0f;

		const float shape = g_cloudShapeNoise.SampleLevel(g_mirrorSampler, worldPos * g_cloudParams2.x + windOffset, 0.0f).r;
		const float detail = g_cloudDetailNoise.SampleLevel(g_mirrorSampler, worldPos * g_cloudParams2.y + windOffset * 1.7f, 0.0f).r;
		const float weather = g_cloudShapeNoise.SampleLevel(g_mirrorSampler, worldPos * (g_cloudParams2.x * 0.32f) + windOffset * 0.45f, 0.0f).r;

		const float height = saturate(localUVW.y);
		const float heightMask = smoothstep(0.03f, 0.22f, height) * (1.0f - smoothstep(0.68f, 0.98f, height));
		const float verticalCore = smoothstep(0.05f, 0.55f, height) * (1.0f - smoothstep(0.62f, 0.96f, height));

		const float coverage = saturate(g_cloudParams0.y);
		const float weatherShift = (weather - 0.5f) * 0.35f;
		const float coverageThreshold = saturate(1.0f - coverage + weatherShift);
		float cloud = saturate((shape - coverageThreshold) / max(0.001f, coverage));
		// MUST stay identical to SampleCloudDensity in VolumetricClouds.shader - this is a
		// hand-duplicated copy (the two differ only in texture/sampler names), and it had
		// been left on the old inverted erosion curve after the visible-cloud version was
		// fixed. The result was world cloud shadows computed from a different density field
		// than the clouds actually being drawn: shadows appeared where there was no cloud.
		const float erosionByHeight = lerp(0.55f, 1.45f, smoothstep(0.25f, 0.95f, height));
		cloud = saturate(cloud - (1.0f - detail) * g_cloudParams0.z * erosionByHeight);
		const float billow = saturate(1.0f + (detail - 0.5f) * 0.28f + (weather - 0.5f) * 0.36f);
		const float densityShape = lerp(cloud * cloud, cloud, 0.55f);

		return min(densityShape * heightMask * verticalCore * billow * g_cloudParams0.x, 2.0f);
	}

	// Prefiltered environment lookup: direction + perceptual roughness against the
	// octahedral atlas (built from the same sky-view LUT the sky sphere renders from,
	// so an IBL reflection and the sky seen directly agree). Roughness selects between
	// the atlas's prefiltered rows, so a rough floor gets a genuinely blurred sky
	// rather than a sharp one dimmed. g_textureSampler is this shader's linear
	// sampler (s0); Deferred has no g_linearSampler.
	float3 SampleSkyEnv(float3 dir, float roughness)
	{
		return SampleEnvAtlas(g_iblSkyEnvAtlas, g_textureSampler, dir, roughness);
	}

	// Weight for a probe at this world position: 1 well inside its box, falling to
	// 0 at the boundary over the outer 25%. Used both to fade a probe out at its
	// own edge and to cross-fade against the second-nearest probe, so a pixel in
	// the overlap of two volumes gets a weighted mix rather than whichever one
	// happened to win the sort.
	float ProbeWeight(float3 worldPos, float4 centre, float4 extents)
	{
		if (centre.w < 0.5f)
			return 0.0f;
		const float3 a = abs(worldPos - centre.xyz) / max(extents.xyz, 0.001f.xxx);
		const float boxDist = max(a.x, max(a.y, a.z)); // <1 inside
		return saturate((1.0f - boxDist) / 0.25f);
	}

	// Box-projected direction for a probe (Lagarde): intersect the reflection ray
	// with the probe's box and look from the probe centre toward that hit, so flat
	// floors reflect the actual walls instead of infinitely-distant radiance.
	float3 ProbeSpecularDir(float3 R, float3 worldPos, float4 centre, float4 extents)
	{
		if (extents.w < 0.5f)
			return R;

		const float3 localPos = worldPos - centre.xyz;
		const float3 ext = max(extents.xyz, 0.001f.xxx);
		const float3 safeR = sign(R) * max(abs(R), 1e-4f.xxx);
		const float3 planeA = ( ext - localPos) / safeR;
		const float3 planeB = (-ext - localPos) / safeR;
		const float3 furthest = max(planeA, planeB);
		const float hitDist = min(furthest.x, min(furthest.y, furthest.z));
		return normalize(localPos + R * max(hitDist, 0.0f));
	}

	float CalculateCloudShadow(float3 worldPos, float3 sunDir)
	{
		const float shadowStrength = saturate(g_cloudMarch.w);
		if (shadowStrength <= 0.0001f)
			return 1.0f;

		const float3 boundsMin = g_cloudBoundsMin.xyz;
		const float3 boundsMax = g_cloudBoundsMax.xyz;
		if (worldPos.y > boundsMax.y)
			return 1.0f;

		const int shadowSteps = max(1, (int)g_cloudMarch.z);
		const float2 hit = RayBoxDist(boundsMin, boundsMax, worldPos, sunDir);
		if (hit.y <= 0.0f)
			return 1.0f;

		const float3 windOffset = g_cloudWindOffset.xyz;

		const float invCloudHeight = rcp(max(100.0f, boundsMax.y - boundsMin.y));
		const float stepLen = max(1.0f, hit.y / (float)shadowSteps);

		float opticalDepth = 0.0f;
		float travelled = 0.0f;
		[loop]
		for (int i = 0; i < shadowSteps; ++i)
		{
			if (travelled >= hit.y)
				break;

			const float3 samplePos = worldPos + sunDir * (hit.x + travelled);
			const float density = SampleCloudDensity(samplePos, boundsMin, boundsMax, windOffset);
			opticalDepth += density * stepLen * invCloudHeight;
			travelled += stepLen;
		}

		const float cloudTransmittance = max(g_cloudParams3.z, exp(-opticalDepth * g_cloudParams1.x));
		return lerp(1.0f, cloudTransmittance, shadowStrength);
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
		if(pixelColour.a == -1 || pixelPosWS.a > 0.0f)
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
			const float NdotV = saturate(dot(N, V));
			const float3 R = reflect(-V, N);

			const float3 diffuseColour  = pixelColour.rgb * (1.0f - f0) * (1.0f - metallic);
			const float3 specularColour = lerp(f0, pixelColour.rgb, metallic);

			// P1-B: real DFG lookup, with the analytic fit as the fallback for the
			// first frame (before the table is generated) and when disabled.
			const float3 dfgSample = g_dfgLut.SampleLevel(g_textureSampler, float2(NdotV, perceptualRoughness), 0).rgb;
			const bool useLut = (g_useDfgLut > 0.5f) && (dfgSample.b > 1e-4f);
			const float2 dfg = useLut ? dfgSample.rg : EnvBRDFApprox(NdotV, perceptualRoughness);

			// Multi-scatter energy compensation (Fdez-Aguera 2019).
			//
			// Single-scatter GGX models ONE bounce off the microfacet surface, so
			// the light that would have bounced again is simply lost. The loss
			// grows with roughness and makes rough metals render noticeably too
			// dark. Ess (the DFG table's .b channel) is the energy a white-Fresnel
			// surface actually returns, so 1-Ess is what went missing; scaling by
			// F0 * (1-Ess)/Ess adds back the portion that would have survived
			// further bounces at this surface's reflectance.
			float3 energyCompensation = 1.0f.xxx;
			if (useLut && g_useMultiScatter > 0.5f)
			{
				const float Ess = max(dfgSample.b, 1e-3f);
				energyCompensation = 1.0f.xxx + specularColour * (1.0f / Ess - 1.0f);
			}

			// The atlas is prefiltered per roughness row, so the lookup uses the true
			// mirror direction - no normal-bias hack needed. Diffuse takes the roughest
			// row along the normal as an irradiance proxy (a GGX(1.0) prefilter is not a
			// cosine integral, but it is close enough until P1-C's SH irradiance).
			const float3 skySpec = SampleSkyEnv(R, perceptualRoughness);

			// Diffuse comes from SH irradiance (P1-C), not from the atlas's roughest
			// row. A GGX roughness-1 prefilter is a wide specular lobe, not a cosine
			// convolution - using it as diffuse gave a flat wash with no directional
			// falloff. Order-2 SH reconstructs Lambertian irradiance to ~1% and costs
			// 9 taps of a 1x9 texture.
			const float3 skyDiff = ShIrradiance(g_iblSkySHTex, g_textureSampler, N);

			// Horizon fade: the sky LUT carries no ground radiance, so a downward-facing
			// direction would otherwise light undersides with horizon sky. The SH term
			// already encodes the sky's own directional distribution, so it needs a far
			// gentler fade than the specular lookup does.
			const float specHorizon = saturate(R.y * 3.0f + 0.35f);
			const float diffHorizon = saturate(N.y * 0.35f + 0.65f);

			// Environment radiance before the BRDF weighting: sky terms carry
			// their strengths and horizon fades here so the probe can replace
			// them wholesale inside its box.
			float3 envSpecRadiance = skySpec * specHorizon * g_iblSkySpecular;
			float3 envDiffRadiance = skyDiff * diffHorizon * g_iblSkyDiffuse;

			// ---- Reflection probe override --------------------------------------
			// A captured probe is local radiance with occlusion baked in - inside
			// its box it REPLACES the sky terms (which are unoccluded and
			// therefore wrong indoors) rather than adding to them. Fades back to
			// the sky terms over the outer 15% of the box so walking out of a
			// probe's volume doesn't pop.
			{
				const float w1 = ProbeWeight(pixelPosWS.xyz, g_probeCenter,  g_probeExtents);
				const float w2 = ProbeWeight(pixelPosWS.xyz, g_probeCenter2, g_probeExtents2);
				const float wSum = w1 + w2;

				if (wSum > 0.0f)
				{
					// Normalise so overlapping volumes hand back one probe's worth of
					// energy, then fade the whole probe term against the sky term by
					// the UNnormalised coverage - a pixel only partly covered by any
					// probe should still see some sky rather than a full-strength
					// probe stretched to fill.
					const float n1 = w1 / wSum;
					const float n2 = w2 / wSum;
					const float coverage = saturate(wSum);

					float3 probeSpec = 0.0f.xxx;
					float3 probeDiff = 0.0f.xxx;

					// Diffuse comes from each probe's own SH irradiance, not from the
					// atlas's roughest row. That's the term that carries the probe's
					// occlusion: sky SH says "the whole hemisphere is bright sky", a
					// probe's SH says "mostly walls and ceiling, sky only through the
					// windows". Using sky SH indoors is what floods a room blue, and
					// it's why there was a visible seam at the probe boundary.
					if (w1 > 0.0f)
					{
						const float3 d1 = ProbeSpecularDir(R, pixelPosWS.xyz, g_probeCenter, g_probeExtents);
						probeSpec += n1 * SampleEnvAtlas(g_iblProbeAtlas, g_textureSampler, d1, perceptualRoughness);
						probeDiff += n1 * ShIrradiance(g_iblProbeSH, g_textureSampler, N);
					}
					if (w2 > 0.0f)
					{
						const float3 d2 = ProbeSpecularDir(R, pixelPosWS.xyz, g_probeCenter2, g_probeExtents2);
						probeSpec += n2 * SampleEnvAtlas(g_iblProbeAtlas2, g_textureSampler, d2, perceptualRoughness);
						probeDiff += n2 * ShIrradiance(g_iblProbeSH2, g_textureSampler, N);
					}

					// Specular and diffuse take SEPARATE strengths. Driving both from
					// the probe strength reinstated a full-intensity diffuse IBL even
					// though the sky diffuse term is deliberately off - and since the
					// diffuse lookup is the atlas's roughest row (a near-uniform
					// average of the captured room), it washed the whole interior flat
					// cream and erased every bit of contrast. Probe diffuse now
					// defaults to 0 until P1-C provides real cosine irradiance.
					envSpecRadiance = lerp(envSpecRadiance, probeSpec * g_iblParams.z, coverage);
					envDiffRadiance = lerp(envDiffRadiance, probeDiff * g_iblParams.w, coverage);
				}
			}
			// ----------------------------------------------------------------------

			const float3 iblSpecular =
				envSpecRadiance * (specularColour * dfg.x + dfg.y) * energyCompensation;
			const float3 iblDiffuse = envDiffRadiance * diffuseColour;

			pbr.rgb += iblSpecular + iblDiffuse;
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

		return pbr;

	#if 0
		float shinyPower = pixelSpecular.g;
		float shininessStrength = pixelSpecular.r;
		float emission = pixelPosWS.w;

		if(emission == -1.0f)
		{
			return float4(pixelColour.rgb, 1.0f);
		}
		else if (emission > 0.0f)
		{
			pixelColour.rgb = pixelColour.rgb * emission;
		}
		//else
		{
			ShadowInput shadow;
			shadow.pixelDepth = pixelNormal.w;
			shadow.positionWS = pixelPosWS;
			shadow.positionSS = input.position.xy;
			shadow.samples = g_shadowConfig.samples;

			float d = dot(normalize(pixelNormal.xyz), normalize(g_shadowCasterLightDir.xyz));
			float bias = g_shadowConfig.biasMultiplier* (1.0 - d);// max(0.000002 * (1.0 - d), 0.0000002); // seems good
			//float bias = 0.00011 * (1.0 - d);// max(0.000002 * (1.0 - d), 0.0000002);

			float depthValue = CalculateShadows(shadow, g_cmpSampler, g_pointSampler, SHADOWMAPS, bias);

			float3 ambient = pixelColour.rgb * g_atmosphere.ambientLight.rgb;
			float3 diffuse = float3(0, 0, 0);// pixelColour.rgb* depthValue;// float3(0, 0, 0);
			float3 specular = float3(0, 0, 0);

			CalculateDiffuseAndSpecularLighting(
				depthValue,
				pixelNormal.xyz,
				pixelSpecular.rrr,
				pixelColour.rgb * getSunColour(),
				lightDir,
				eyeVector, 
				shinyPower,
				shininessStrength,		
				g_globalLight[0],
				diffuse,
				specular);

			float lightningFlash = saturate(g_weatherSurface.lightningFlash);
			float3 lightningDir = normalize(g_weatherSurface.lightningBoltDirection.xyz + float3(1e-5f, 1e-5f, 1e-5f));
			float lightningNdotL = saturate(dot(normalize(pixelNormal.xyz), lightningDir));
			float lightningSpec = pow(saturate(dot(normalize(normalize(pixelNormal.xyz) + eyeVector), lightningDir)), lerp(44.0f, 14.0f, saturate(pixelSpecular.r)));
			float3 lightningColour = float3(0.62f, 0.76f, 1.0f);
			float3 lightningContribution = lightningColour * lightningFlash * (pixelColour.rgb * lightningNdotL * 0.42f + lightningSpec * 0.62f);

			float3 finalColour = ambient + diffuse + specular + lightningContribution;

			float4 result = float4(finalColour.rgb, 1.0f);
			return /*saturate*/(result);
		
		}
		#endif
	}
}
