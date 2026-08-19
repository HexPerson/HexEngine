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
	AtmospherePhysical
	Utils
}
"VertexShader"
{
	UIPixelInput ShaderMain(UIVertexInput input)
	{
		UIPixelInput output;

		output.position = input.position;
		output.texcoord = input.texcoord;
		output.positionSS = output.position;
		output.colour = input.colour;

		return output;
	}
}
"PixelShader"
{
	GBUFFER_RESOURCE(0, 1, 2, 3, 4);

	Texture2D g_sceneColour : register(t5);
	Texture2D g_noiseTexture : register(t6);
	Texture3D g_shapeNoise : register(t7);
	Texture3D g_detailNoise : register(t8);

	SamplerState g_pointSampler : register(s2);
	SamplerState g_mirrorSampler : register(s3);
	SamplerState g_linearSampler : register(s4);

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

	static const float PI = 3.14159265f;

	float Hash12(float2 p)
	{
		const float h = dot(p, float2(127.1f, 311.7f));
		return frac(sin(h) * 43758.5453123f);
	}

	float ValueNoise2(float2 p)
	{
		const float2 pi = floor(p);
		const float2 pf = frac(p);
		const float2 w = pf * pf * (3.0f - 2.0f * pf);
		const float n00 = Hash12(pi);
		const float n10 = Hash12(pi + float2(1.0f, 0.0f));
		const float n01 = Hash12(pi + float2(0.0f, 1.0f));
		const float n11 = Hash12(pi + float2(1.0f, 1.0f));
		return lerp(lerp(n00, n10, w.x), lerp(n01, n11, w.x), w.y);
	}

	// 2D WEATHER MAP - the root fix for clouds never filling the sky. Cloud
	// PRESENCE is decided per XZ column by this horizontal field, so
	// placement is altitude-independent and the coverage cvar maps ~linearly
	// onto actual sky fraction (1.0 = a true overcast is reachable). The 3D
	// Perlin-Worley noises only SCULPT the shapes the map dictates. Advected
	// by the same integrated wind offset as the shape noise, at a slower
	// rate - weather systems drift slower than the cloud tops churn.
	float WeatherCoverage(float2 xz, float2 windXz)
	{
		const float2 p = (xz + windXz * 220.0f) * (1.0f / 1400.0f); // ~1.4 km systems
		const float fbm =
			ValueNoise2(p) * 0.55f +
			ValueNoise2(p * 2.3f + 17.1f.xx) * 0.30f +
			ValueNoise2(p * 5.1f + 41.7f.xx) * 0.15f;
		return saturate(fbm);
	}

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

	float HenyeyGreenstein(float cosTheta, float anisotropy)
	{
		const float g = clamp(anisotropy, -0.95f, 0.95f);
		const float denom = max(1e-4f, 1.0f + g * g - 2.0f * g * cosTheta);
		return (1.0f - g * g) / (4.0f * PI * pow(denom, 1.5f));
	}

	float DualLobeHG(float cosTheta, float anisotropy)
	{
		// A strong forward lobe (silver lining) blended with a gentle back-scatter
		// lobe (soft ambient rim) - real clouds exhibit both at once.
		const float forward = HenyeyGreenstein(cosTheta, anisotropy);
		const float backward = HenyeyGreenstein(cosTheta, -0.25f);
		return lerp(forward, backward, 0.35f);
	}

	float3 GetWorldRay(float2 uv)
	{
		float4 clipPos = float4(uv * 2.0f - 1.0f, 1.0f, 1.0f);
		clipPos.y *= -1.0f;
		float4 worldPos = mul(clipPos, g_viewProjectionMatrixInverse);
		worldPos.xyz /= max(1e-5f, worldPos.w);
		return normalize(worldPos.xyz - g_eyePos.xyz);
	}

	float SampleCloudDensity(float3 worldPos, float3 boundsMin, float3 boundsMax, float3 windOffset)
	{
		const float3 boundsSize = max(boundsMax - boundsMin, 1e-3f.xxx);
		const float3 localUVW = (worldPos - boundsMin) / boundsSize;

		if (any(localUVW < 0.0f.xxx) || any(localUVW > 1.0f.xxx))
			return 0.0f;

		// Column presence from the 2D weather map (see WeatherCoverage). The
		// old path thresholded the 3D shape noise directly, so whether a
		// cloud existed depended on what that noise contained at this
		// position AND altitude - coverage was accidental and no cvar value
		// could produce an overcast.
		const float coverage = saturate(g_cloudParams0.y);
		const float wm = WeatherCoverage(worldPos.xz, windOffset.xz);
		const float columnCoverage = saturate((wm - (1.0f - coverage * 1.04f)) / max(0.06f, 1.0f - coverage * 0.85f));
		if (columnCoverage <= 0.002f)
			return 0.0f;

		const float shape = g_shapeNoise.SampleLevel(g_mirrorSampler, worldPos * g_cloudParams2.x + windOffset, 0.0f).r;
		const float detail = g_detailNoise.SampleLevel(g_mirrorSampler, worldPos * g_cloudParams2.y + windOffset * 1.7f, 0.0f).r;

		const float height = saturate(localUVW.y);
		const float heightMask = smoothstep(0.03f, 0.22f, height) * (1.0f - smoothstep(0.68f, 0.98f, height));
		// Fuller weather columns build TALLER clouds: widen the vertical core
		// with coverage so an overcast reads as a deck, scattered as puffs.
		const float coreTop = lerp(0.62f, 0.90f, columnCoverage);
		const float verticalCore = smoothstep(0.05f, lerp(0.55f, 0.35f, columnCoverage), height) * (1.0f - smoothstep(coreTop, 0.98f, height));

		// 3D Perlin-Worley SCULPTS the column the map dictates. REMAP form
		// (Schneider), not raw subtraction: plain subtraction stacked on a
		// base that is already a product of <=1 terms crushed interior
		// density to ~0.1 and made the whole sky nearly invisible. The remap
		// (x - e) / (1 - e) eats from the BOTTOM - edges erode away while a
		// full column keeps ~full density.
		float cloud = columnCoverage * heightMask * verticalCore;
		const float shapeErode = (1.0f - shape) * lerp(0.62f, 0.30f, columnCoverage);
		cloud = saturate((cloud - shapeErode) / max(0.05f, 1.0f - shapeErode));
		const float erosionByHeight = lerp(0.55f, 1.45f, smoothstep(0.25f, 0.95f, height));
		const float detailErode = saturate((1.0f - detail) * g_cloudParams0.z * erosionByHeight);
		cloud = saturate((cloud - detailErode) / max(0.05f, 1.0f - detailErode));
		const float billow = saturate(1.0f + (detail - 0.5f) * 0.28f + (wm - 0.5f) * 0.36f);
		const float densityShape = lerp(cloud * cloud, cloud, 0.55f);

		return min(densityShape * billow * g_cloudParams0.x, 2.0f);
	}

	float MarchToLight(float3 samplePos, float3 boundsMin, float3 boundsMax, float3 windOffset, float3 sunDir, int lightSteps)
	{
		const float2 lightHit = RayBoxDist(boundsMin, boundsMax, samplePos, sunDir);
		if (lightHit.y <= 0.0f)
			return 1.0f;

		// Horizon-scale bounds can put kilometres of slab above a sample;
		// light beyond ~900m of cloud is fully extinct anyway, and without
		// this clamp the light steps stretch into uselessness.
		const float lightSpan = min(lightHit.y, 900.0f);
		const float stepLen = max(1.0f, lightSpan / max(1, lightSteps));
		const float invCloudHeight = rcp(max(100.0f, boundsMax.y - boundsMin.y));
		float travelled = 0.0f;
		float opticalDepth = 0.0f;

		[loop]
		for (int i = 0; i < lightSteps; ++i)
		{
			if (travelled >= lightSpan)
				break;

			const float3 p = samplePos + sunDir * travelled;
			const float density = SampleCloudDensity(p, boundsMin, boundsMax, windOffset);
			opticalDepth += density * stepLen * invCloudHeight;
			travelled += stepLen;
		}

		return max(g_cloudParams3.z, exp(-opticalDepth * g_cloudParams1.x));
	}

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		const float2 uv = input.texcoord;
		float pixelDepth = GBUFFER_NORMAL.Sample(g_pointSampler, uv).w;
		if (pixelDepth <= 0.0f || pixelDepth == -1.0f)
			pixelDepth = g_frustumDepths[3];

		const float3 boundsMin = g_cloudBoundsMin.xyz;
		const float3 boundsMax = g_cloudBoundsMax.xyz;
		const float3 eyePos = g_eyePos.xyz;
		const float3 rayDir = GetWorldRay(uv);
		const float2 cloudHit = RayBoxDist(boundsMin, boundsMax, eyePos, rayDir);

		if (cloudHit.y <= 0.0f)
			return float4(0.0f, 0.0f, 0.0f, 0.0f);

		const float entryDist = cloudHit.x;
		float maxTraceDistance = min(cloudHit.y, max(0.0f, pixelDepth - entryDist));
		maxTraceDistance = min(maxTraceDistance, g_cloudParams0.w);
		if (maxTraceDistance <= 0.0f)
			return float4(0.0f, 0.0f, 0.0f, 0.0f);

		int viewSteps = max(8, (int)g_cloudMarch.x);
		int lightSteps = max(2, (int)g_cloudMarch.y);
		const float qualityPreset = g_cloudWindDirection.w;
		if (qualityPreset <= 0.5f)
		{
			viewSteps = max(8, (int)(viewSteps * 0.80f));
			lightSteps = max(2, (int)(lightSteps * 0.70f));
		}
		else if (qualityPreset >= 1.5f)
		{
			viewSteps = (int)(viewSteps * 1.15f);
			lightSteps = (int)(lightSteps * 1.20f);
		}

		float baseStep = maxTraceDistance / max(1, viewSteps);
		baseStep = max(1.0f, baseStep * g_cloudParams1.w);
		const float invCloudHeight = rcp(max(100.0f, boundsMax.y - boundsMin.y));

		const float2 noiseUv = uv * float2(max(1.0f, g_screenWidth / 128.0f), max(1.0f, g_screenHeight / 128.0f));
		const float noise = g_noiseTexture.Sample(g_linearSampler, noiseUv).r;
		const float jitter = frac(noise + Hash12(input.position.xy) + frac(g_time * 0.1337f));

		const float3 windOffset = g_cloudWindOffset.xyz;
		const float3 sunDir = normalize(-g_lightDirection.xyz + float3(1e-5f, 1e-5f, 1e-5f));
		const float3 cloudProbeOrigin = float3(0.0f, lerp(boundsMin.y, boundsMax.y, 0.58f), 0.0f);
		const float3 cloudSunRadiance = ComputePhysicalSunColour(cloudProbeOrigin, sunDir);
		const float cloudSunLuma = max(dot(cloudSunRadiance, float3(0.299f, 0.587f, 0.114f)), 0.001f);
		const float3 cloudSunHue = cloudSunRadiance / cloudSunLuma;
		const float sunElevation = -g_lightDirection.y;
		const float sunsetAmount = saturate((0.22f - sunElevation) / 0.32f);
		const float3 cloudSunBalancedHue = lerp(cloudSunHue, float3(1.0f, 0.78f, 0.58f), sunsetAmount * 0.55f);
		const float3 cloudSunColour = cloudSunBalancedHue * max(g_globalLight[0], 0.35f) * 0.55f;
		const float3 horizonProbeDir = normalize(float3(-sunDir.x, 0.10f, -sunDir.z));
		const float3 zenithProbeDir = float3(0.0f, 1.0f, 0.0f);
		const PhysicalAtmosphereSample cloudHorizonProbe = IntegrateAtmospherePhysical(cloudProbeOrigin, horizonProbeDir, g_frustumDepths[3], sunDir, 12, false);
		const PhysicalAtmosphereSample cloudZenithProbe = IntegrateAtmospherePhysical(cloudProbeOrigin, zenithProbeDir, g_frustumDepths[3], sunDir, 12, false);
		const float lightningFlash = saturate(g_weatherSurface.lightningFlash);
		const float3 lightningDir = normalize(g_weatherSurface.lightningBoltDirection.xyz + float3(1e-5f, 1e-5f, 1e-5f));
		const float3 lightningColour = float3(0.64f, 0.78f, 1.0f) * lightningFlash;
		const float cosSun = dot(rayDir, sunDir);
		const float isotropicPhase = 1.0f / (4.0f * PI);
		const float phase = lerp(isotropicPhase, DualLobeHG(cosSun, g_cloudParams1.z), 0.85f) * g_cloudParams3.w;
		const float3 ambientSky = lerp(cloudHorizonProbe.inscatter, cloudZenithProbe.inscatter, 0.30f) * g_cloudParams3.y;
		const float skyLuma = dot(ambientSky, float3(0.299f, 0.587f, 0.114f));
		const float3 ambientNeutral = skyLuma.xxx;
		const float3 ambientChromatic = lerp(ambientNeutral, ambientSky, saturate(g_cloudParams5.y));
		const float3 ambientShaded = lerp(ambientChromatic, cloudSunColour, saturate(g_cloudParams5.x) * 0.28f);
		const float3 topTint = lerp(ambientShaded, cloudSunColour, 0.48f);
		const float3 bottomTint = lerp(ambientNeutral, ambientShaded, 0.88f);

		float transmittance = 1.0f;
		float3 cloudLight = 0.0f.xxx;

		// PROGRESSIVE stepping for the horizon-scale domain: uniform steps
		// over a 10km+ trace either mush the near field or starve the step
		// budget. Steps grow with distance - full detail overhead, coarse
		// (but still sampled) toward the horizon deck.
		const float maxDist = max(1.0f, g_cloudParams0.w);
		float travelled = baseStep * jitter;
		[loop]
		for (int i = 0; i < viewSteps; ++i)
		{
			if (travelled >= maxTraceDistance)
				break;

			const float stepLenView = baseStep * clamp(1.0f + (entryDist + travelled) * 0.0011f, 1.0f, 7.0f);
			const float3 samplePos = eyePos + rayDir * (entryDist + travelled);
			const float density = SampleCloudDensity(samplePos, boundsMin, boundsMax, windOffset);

			if (density > 0.0001f)
			{
				const float lightTrans = MarchToLight(samplePos, boundsMin, boundsMax, windOffset, sunDir, lightSteps);
				const float shadowAmount = 1.0f - lightTrans;
				const float viewToSun = saturate(dot(rayDir, sunDir));
				const float powder = 1.0f + g_cloudParams1.y * (1.0f - lightTrans);
				// Soft distance fade toward the trace limit - the horizon deck
				// dissolves into the atmosphere instead of ending at a wall.
				const float distanceFade = 1.0f - smoothstep(0.70f, 1.0f, (entryDist + travelled) / maxDist);
				const float scatter = density * stepLenView * invCloudHeight * transmittance * distanceFade;
				const float diffuseProbeDistance = max(1.0f, baseStep * 0.75f);
				const float densityTowardSun = SampleCloudDensity(samplePos + sunDir * diffuseProbeDistance, boundsMin, boundsMax, windOffset);
				const float derivativeDiffuse = saturate((density - densityTowardSun) * 2.25f + 0.12f);
				const float directionalDiffuse = lerp(1.0f, derivativeDiffuse, saturate(g_cloudParams5.z));
				const float densityTowardLightning = SampleCloudDensity(samplePos + lightningDir * diffuseProbeDistance, boundsMin, boundsMax, windOffset);
				const float lightningDerivative = saturate((density - densityTowardLightning) * 2.0f + 0.10f);
				const float silverLining = pow(saturate(shadowAmount), max(0.5f, g_cloudParams4.y)) * g_cloudParams4.x;
				// Energy-conserving multiple scattering: on top of the single-scatter
				// (octave 0 = 1.0), add dimmer, deeper-penetrating higher orders whose
				// extinction is progressively attenuated (lightTrans^0.5, ^0.25) so
				// dense cores glow with soft internal bounce instead of going flat.
				// Scaled by the weather-driven multi-scatter strength.
				const float msStrength = saturate(g_cloudParams4.z);
				const float multiScatter = 1.0f + msStrength * (0.60f * pow(lightTrans, 0.5f) + 0.35f * pow(lightTrans, 0.25f));
				const float height01 = saturate((samplePos.y - boundsMin.y) / max(1.0f, boundsMax.y - boundsMin.y));
				const float3 heightTint = lerp(bottomTint, topTint, smoothstep(0.08f, 0.92f, height01));
				const float3 stylizedTint = lerp(1.0f.xxx, heightTint, g_cloudParams4.w);
				const float localAO = exp(-density * (0.8f + 1.8f * saturate(g_cloudParams5.w)));
				const float aoTerm = lerp(1.0f, localAO, saturate(g_cloudParams5.w));
				const float densityAhead = SampleCloudDensity(samplePos + rayDir * diffuseProbeDistance * 0.7f, boundsMin, boundsMax, windOffset);
				const float edgeFactor = saturate(abs(density - densityAhead) * 2.4f);
				const float forwardGlow = pow(viewToSun, 4.0f) * saturate(1.0f - density * 0.95f) * (0.25f + 0.75f * shadowAmount);
				const float coreDarken = lerp(1.0f, 0.72f, saturate(density * 0.9f));
				const float edgeBoost = 1.0f + edgeFactor * 0.24f;
				const float3 directLight = ((lightTrans * powder * phase * cloudSunColour + silverLining * cloudSunColour * (0.22f + 0.48f * viewToSun)) * edgeBoost + cloudSunColour * forwardGlow * g_cloudParams4.x * 0.22f) * multiScatter * directionalDiffuse;
				const float lightningScatterPhase = 0.35f + 0.65f * saturate(dot(rayDir, lightningDir) * 0.5f + 0.5f);
				const float lightningEdge = pow(saturate(1.0f - density), 1.4f);
				const float3 lightningLight = lightningColour * (0.34f + lightningDerivative * 0.66f) * lightningScatterPhase * (0.55f + lightningEdge * 0.45f);
				const float3 ambientLight = (ambientShaded * (0.35f + shadowAmount * 0.65f) + lightningColour * (0.08f + 0.14f * lightningEdge)) * (1.0f + shadowAmount * 0.25f * g_cloudParams4.z) * aoTerm * coreDarken;
				cloudLight += scatter * (directLight + ambientLight + lightningLight) * stylizedTint;

				transmittance *= exp(-density * stepLenView * invCloudHeight * g_cloudParams3.x * distanceFade);
				if (transmittance < 0.01f)
					break;
			}

			travelled += stepLenView;
		}

		// Keep dense cores from collapsing to pure black under aggressive shadowing.
		cloudLight = max(cloudLight, ambientShaded * (1.0f - transmittance) * 0.12f);

		const float alpha = saturate(1.0f - transmittance);
		if (alpha <= 1e-4f)
			return 0.0f.xxxx;

		// Clouds are composited with non-premultiplied alpha.
		const float safeAlpha = max(0.03f, alpha);
		const float3 straightCloud = min(cloudLight / safeAlpha, 8.0f.xxx);
		return float4(straightCloud, alpha);
	}
}
