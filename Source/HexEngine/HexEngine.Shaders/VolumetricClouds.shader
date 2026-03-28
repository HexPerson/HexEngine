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
	Atmosphere
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
		float4 g_cloudMarch; // x=view steps, y=light steps
	};

	static const float PI = 3.14159265f;

	float Hash12(float2 p)
	{
		const float h = dot(p, float2(127.1f, 311.7f));
		return frac(sin(h) * 43758.5453123f);
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

		const float shape = g_shapeNoise.SampleLevel(g_mirrorSampler, worldPos * g_cloudParams2.x + windOffset, 0.0f).r;
		const float detail = g_detailNoise.SampleLevel(g_mirrorSampler, worldPos * g_cloudParams2.y + windOffset * 1.7f, 0.0f).r;

		const float height = saturate(localUVW.y);
		const float heightMask = smoothstep(0.03f, 0.22f, height) * (1.0f - smoothstep(0.68f, 0.98f, height));

		const float coverage = saturate(g_cloudParams0.y);
		const float coverageThreshold = 1.0f - coverage;
		float cloud = saturate((shape - coverageThreshold) / max(0.001f, coverage));
		cloud = saturate(cloud - (1.0f - detail) * g_cloudParams0.z);

		return min(cloud * heightMask * g_cloudParams0.x, 2.0f);
	}

	float MarchToLight(float3 samplePos, float3 boundsMin, float3 boundsMax, float3 windOffset, float3 sunDir, int lightSteps)
	{
		const float2 lightHit = RayBoxDist(boundsMin, boundsMax, samplePos, sunDir);
		if (lightHit.y <= 0.0f)
			return 1.0f;

		const float stepLen = max(1.0f, lightHit.y / max(1, lightSteps));
		const float invCloudHeight = rcp(max(100.0f, boundsMax.y - boundsMin.y));
		float travelled = 0.0f;
		float opticalDepth = 0.0f;

		[loop]
		for (int i = 0; i < lightSteps; ++i)
		{
			if (travelled >= lightHit.y)
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

		const float3 windDir = normalize(g_cloudWindDirection.xyz + float3(1e-5f, 1e-5f, 1e-5f));
		const float3 windOffset = windDir * g_cloudParams2.z * g_cloudParams2.w * (g_time * 0.01f);
		const float3 sunDir = normalize(-g_lightDirection.xyz + float3(1e-5f, 1e-5f, 1e-5f));
		const float3 sunColour = getSunColour() * max(0.0f, g_globalLight[0]);
		const float hgPhase = HenyeyGreenstein(dot(rayDir, sunDir), g_cloudParams1.z);
		const float isotropicPhase = 1.0f / (4.0f * PI);
		const float phase = lerp(isotropicPhase, hgPhase, 0.75f) * g_cloudParams3.w;
		const float3 ambientSky = lerp(g_globalLight.yzw, g_atmosphere.ambientLight.rgb, 0.60f) * g_cloudParams3.y;
		const float skyLuma = dot(ambientSky, float3(0.299f, 0.587f, 0.114f));
		const float3 ambientNeutral = skyLuma.xxx;
		const float3 ambientChromatic = lerp(ambientNeutral, ambientSky, saturate(g_cloudParams5.y));
		const float3 ambientShaded = lerp(ambientChromatic, sunColour, saturate(g_cloudParams5.x) * 0.45f);
		const float3 topTint = lerp(ambientShaded, sunColour, 0.70f);
		const float3 bottomTint = lerp(ambientNeutral, ambientShaded, 0.88f);

		float transmittance = 1.0f;
		float3 cloudLight = 0.0f.xxx;

		float travelled = baseStep * jitter;
		[loop]
		for (int i = 0; i < viewSteps; ++i)
		{
			if (travelled >= maxTraceDistance)
				break;

			const float3 samplePos = eyePos + rayDir * (entryDist + travelled);
			const float density = SampleCloudDensity(samplePos, boundsMin, boundsMax, windOffset);

			if (density > 0.0001f)
			{
				const float lightTrans = MarchToLight(samplePos, boundsMin, boundsMax, windOffset, sunDir, lightSteps);
				const float shadowAmount = 1.0f - lightTrans;
				const float viewToSun = saturate(dot(rayDir, sunDir));
				const float powder = 1.0f + g_cloudParams1.y * (1.0f - lightTrans);
				const float scatter = density * baseStep * invCloudHeight * transmittance;
				const float diffuseProbeDistance = max(1.0f, baseStep * 0.75f);
				const float densityTowardSun = SampleCloudDensity(samplePos + sunDir * diffuseProbeDistance, boundsMin, boundsMax, windOffset);
				const float derivativeDiffuse = saturate((density - densityTowardSun) * 2.25f + 0.12f);
				const float directionalDiffuse = lerp(1.0f, derivativeDiffuse, saturate(g_cloudParams5.z));
				const float silverLining = pow(saturate(shadowAmount), max(0.5f, g_cloudParams4.y)) * g_cloudParams4.x;
				const float multiScatter = 1.0f + shadowAmount * g_cloudParams4.z;
				const float height01 = saturate((samplePos.y - boundsMin.y) / max(1.0f, boundsMax.y - boundsMin.y));
				const float3 heightTint = lerp(bottomTint, topTint, smoothstep(0.08f, 0.92f, height01));
				const float3 stylizedTint = lerp(1.0f.xxx, heightTint, g_cloudParams4.w);
				const float localAO = exp(-density * (0.8f + 1.8f * saturate(g_cloudParams5.w)));
				const float aoTerm = lerp(1.0f, localAO, saturate(g_cloudParams5.w));
				const float3 directLight = (lightTrans * powder * phase * sunColour + silverLining * sunColour * (0.35f + 0.65f * viewToSun)) * multiScatter * directionalDiffuse;
				const float3 ambientLight = ambientShaded * (0.35f + shadowAmount * 0.65f) * (1.0f + shadowAmount * 0.25f * g_cloudParams4.z) * aoTerm;
				cloudLight += scatter * (directLight + ambientLight) * stylizedTint;

				transmittance *= exp(-density * baseStep * invCloudHeight * g_cloudParams3.x);
				if (transmittance < 0.01f)
					break;
			}

			travelled += baseStep;
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
