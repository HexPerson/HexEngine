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
	Texture3D g_voxelRadianceTex0 : register(t5);
	Texture3D g_voxelOpacityTex0 : register(t6);
	Texture3D g_voxelAlbedoTex0 : register(t7);
	Texture2D g_probeIrradianceTex0 : register(t8);
	Texture2D g_probeVisibilityTex0 : register(t9);
	Texture3D g_voxelRadianceTex1 : register(t10);
	Texture3D g_voxelOpacityTex1 : register(t11);
	Texture3D g_voxelAlbedoTex1 : register(t12);
	Texture2D g_probeIrradianceTex1 : register(t13);
	Texture2D g_probeVisibilityTex1 : register(t14);
	Texture3D g_voxelRadianceTex2 : register(t15);
	Texture3D g_voxelOpacityTex2 : register(t16);
	Texture3D g_voxelAlbedoTex2 : register(t17);
	Texture2D g_probeIrradianceTex2 : register(t18);
	Texture2D g_probeVisibilityTex2 : register(t19);
	Texture3D g_voxelRadianceTex3 : register(t20);
	Texture3D g_voxelOpacityTex3 : register(t21);
	Texture3D g_voxelAlbedoTex3 : register(t22);
	Texture2D g_probeIrradianceTex3 : register(t23);
	Texture2D g_probeVisibilityTex3 : register(t24);
	Texture2D g_sceneLightingTex : register(t25);

	SamplerState g_pointSampler : register(s2);
	SamplerState g_linearSampler : register(s4);

	cbuffer GIConstants : register(b4)
	{
		float4 g_clipCenterExtent[4];
		float4 g_clipVoxelInfo[4];
		float4 g_giParams0; // x=intensity, y=energyClamp, z=debugMode, w=activeClipmap
		float4 g_giParams1; // x=hysteresis, y=historyReject, z=halfInvW, w=halfInvH
		float4 g_giParams2; // x=screenBounce, y=probeBlend, z=voxelDecay, w=useVoxelAlphaOpacity
		float4 g_giParams3; // xyz=sunDirectionWS, w=sunDirectionality
		float4 g_giParams4; // x=jitterScale, y=clipBlendWidth, z=pixelMotionStart, w=pixelMotionStrength
		float4 g_giParams5; // x=luminanceRejectScale, y=ditherDarkAmp, z=ditherBrightAmp, w=movementPreset
		float4 g_giParams6; // x=voxelNeighbourBlend, y=shiftSettle, z=voxelAlbedoInfluence, w=reserved
		float4 g_giParams7; // x=gpuMaterialProxyBlend, y=gpuComputeBaseSunEnabled, z=sunShadowPerVoxel, w=cameraMotionBlend
	};

	static const float3 kClipDebugColours[4] =
	{
		float3(0.95, 0.30, 0.20),
		float3(0.20, 0.80, 0.30),
		float3(0.20, 0.45, 0.95),
		float3(0.92, 0.78, 0.22)
	};
	static const uint PROBE_GRID_X = 16;
	static const uint PROBE_GRID_Y = 10;
	static const uint PROBE_GRID_Z = 16;
	static const uint PROBE_ATLAS_W = PROBE_GRID_X * PROBE_GRID_Z;
	static const uint PROBE_ATLAS_H = PROBE_GRID_Y;

	bool IsInside01(float3 uvw)
	{
		return all(uvw >= 0.0f.xxx) && all(uvw <= 1.0f.xxx);
	}

	float Hash12(float2 p)
	{
		const float h = dot(p, float2(127.1f, 311.7f));
		return frac(sin(h) * 43758.5453123f);
	}

	float4 SampleVoxelRadiance(uint clipIdx, float3 uvw)
	{
		switch (clipIdx)
		{
		case 0: return g_voxelRadianceTex0.SampleLevel(g_linearSampler, uvw, 0.0f);
		case 1: return g_voxelRadianceTex1.SampleLevel(g_linearSampler, uvw, 0.0f);
		case 2: return g_voxelRadianceTex2.SampleLevel(g_linearSampler, uvw, 0.0f);
		default: return g_voxelRadianceTex3.SampleLevel(g_linearSampler, uvw, 0.0f);
		}
	}

	float SampleVoxelOpacity(uint clipIdx, float3 uvw)
	{
		switch (clipIdx)
		{
		case 0: return g_voxelOpacityTex0.SampleLevel(g_linearSampler, uvw, 0.0f).r;
		case 1: return g_voxelOpacityTex1.SampleLevel(g_linearSampler, uvw, 0.0f).r;
		case 2: return g_voxelOpacityTex2.SampleLevel(g_linearSampler, uvw, 0.0f).r;
		default: return g_voxelOpacityTex3.SampleLevel(g_linearSampler, uvw, 0.0f).r;
		}
	}

	float4 SampleVoxelAlbedo(uint clipIdx, float3 uvw)
	{
		switch (clipIdx)
		{
		case 0: return g_voxelAlbedoTex0.SampleLevel(g_linearSampler, uvw, 0.0f);
		case 1: return g_voxelAlbedoTex1.SampleLevel(g_linearSampler, uvw, 0.0f);
		case 2: return g_voxelAlbedoTex2.SampleLevel(g_linearSampler, uvw, 0.0f);
		default: return g_voxelAlbedoTex3.SampleLevel(g_linearSampler, uvw, 0.0f);
		}
	}

	float3 SampleProbeIrradiance(uint clipIdx, float2 uv)
	{
		switch (clipIdx)
		{
		case 0: return g_probeIrradianceTex0.SampleLevel(g_linearSampler, uv, 0.0f).rgb;
		case 1: return g_probeIrradianceTex1.SampleLevel(g_linearSampler, uv, 0.0f).rgb;
		case 2: return g_probeIrradianceTex2.SampleLevel(g_linearSampler, uv, 0.0f).rgb;
		default: return g_probeIrradianceTex3.SampleLevel(g_linearSampler, uv, 0.0f).rgb;
		}
	}

	float SampleProbeVisibility(uint clipIdx, float2 uv)
	{
		switch (clipIdx)
		{
		case 0: return g_probeVisibilityTex0.SampleLevel(g_linearSampler, uv, 0.0f).r;
		case 1: return g_probeVisibilityTex1.SampleLevel(g_linearSampler, uv, 0.0f).r;
		case 2: return g_probeVisibilityTex2.SampleLevel(g_linearSampler, uv, 0.0f).r;
		default: return g_probeVisibilityTex3.SampleLevel(g_linearSampler, uv, 0.0f).r;
		}
	}

	float2 ProbeAtlasUV(uint px, uint py, uint pz)
	{
		const float atlasX = (float)(px + pz * PROBE_GRID_X) + 0.5f;
		const float atlasY = (float)py + 0.5f;
		return float2(atlasX / (float)PROBE_ATLAS_W, atlasY / (float)PROBE_ATLAS_H);
	}

	void ComputeProbeLerp(float3 uvw, out uint3 p0, out uint3 p1, out float3 w)
	{
		const float3 g = float3((float)(PROBE_GRID_X - 1), (float)(PROBE_GRID_Y - 1), (float)(PROBE_GRID_Z - 1));
		const float3 p = saturate(uvw) * g;
		const float3 pf = floor(p);
		p0 = uint3(pf);
		p1 = min(p0 + 1, uint3(PROBE_GRID_X - 1, PROBE_GRID_Y - 1, PROBE_GRID_Z - 1));
		w = saturate(p - pf);
	}

	float3 SampleProbeIrradianceTrilinear(uint clipIdx, float3 uvw)
	{
		uint3 p0, p1;
		float3 w;
		ComputeProbeLerp(uvw, p0, p1, w);

		const float3 c000 = SampleProbeIrradiance(clipIdx, ProbeAtlasUV(p0.x, p0.y, p0.z));
		const float3 c100 = SampleProbeIrradiance(clipIdx, ProbeAtlasUV(p1.x, p0.y, p0.z));
		const float3 c010 = SampleProbeIrradiance(clipIdx, ProbeAtlasUV(p0.x, p1.y, p0.z));
		const float3 c110 = SampleProbeIrradiance(clipIdx, ProbeAtlasUV(p1.x, p1.y, p0.z));
		const float3 c001 = SampleProbeIrradiance(clipIdx, ProbeAtlasUV(p0.x, p0.y, p1.z));
		const float3 c101 = SampleProbeIrradiance(clipIdx, ProbeAtlasUV(p1.x, p0.y, p1.z));
		const float3 c011 = SampleProbeIrradiance(clipIdx, ProbeAtlasUV(p0.x, p1.y, p1.z));
		const float3 c111 = SampleProbeIrradiance(clipIdx, ProbeAtlasUV(p1.x, p1.y, p1.z));

		const float3 c00 = lerp(c000, c100, w.x);
		const float3 c10 = lerp(c010, c110, w.x);
		const float3 c01 = lerp(c001, c101, w.x);
		const float3 c11 = lerp(c011, c111, w.x);
		const float3 c0 = lerp(c00, c10, w.y);
		const float3 c1 = lerp(c01, c11, w.y);
		return lerp(c0, c1, w.z);
	}

	float SampleProbeVisibilityTrilinear(uint clipIdx, float3 uvw)
	{
		uint3 p0, p1;
		float3 w;
		ComputeProbeLerp(uvw, p0, p1, w);

		const float c000 = SampleProbeVisibility(clipIdx, ProbeAtlasUV(p0.x, p0.y, p0.z));
		const float c100 = SampleProbeVisibility(clipIdx, ProbeAtlasUV(p1.x, p0.y, p0.z));
		const float c010 = SampleProbeVisibility(clipIdx, ProbeAtlasUV(p0.x, p1.y, p0.z));
		const float c110 = SampleProbeVisibility(clipIdx, ProbeAtlasUV(p1.x, p1.y, p0.z));
		const float c001 = SampleProbeVisibility(clipIdx, ProbeAtlasUV(p0.x, p0.y, p1.z));
		const float c101 = SampleProbeVisibility(clipIdx, ProbeAtlasUV(p1.x, p0.y, p1.z));
		const float c011 = SampleProbeVisibility(clipIdx, ProbeAtlasUV(p0.x, p1.y, p1.z));
		const float c111 = SampleProbeVisibility(clipIdx, ProbeAtlasUV(p1.x, p1.y, p1.z));

		const float c00 = lerp(c000, c100, w.x);
		const float c10 = lerp(c010, c110, w.x);
		const float c01 = lerp(c001, c101, w.x);
		const float c11 = lerp(c011, c111, w.x);
		const float c0 = lerp(c00, c10, w.y);
		const float c1 = lerp(c01, c11, w.y);
		return lerp(c0, c1, w.z);
	}

	float3 ComputeScreenSpaceBounce(float2 uv, float3 centerPosWS, float3 centerNormal, float centerDepth)
	{
		const float2 fullTexel = float2(
			1.0f / max(1.0f, (float)g_screenWidth),
			1.0f / max(1.0f, (float)g_screenHeight));

		static const float2 kOffsets[8] =
		{
			float2(1.0f, 0.0f),
			float2(-1.0f, 0.0f),
			float2(0.0f, 1.0f),
			float2(0.0f, -1.0f),
			float2(0.707f, 0.707f),
			float2(-0.707f, 0.707f),
			float2(0.707f, -0.707f),
			float2(-0.707f, -0.707f)
		};

		float3 accum = 0.0f.xxx;
		float accumWeight = 0.0f;

		[unroll]
		for (uint i = 0; i < 8; ++i)
		{
			const float2 sampleUv = saturate(uv + kOffsets[i] * fullTexel * 6.0f);
			const float4 sampleDiffuse = GBUFFER_DIFFUSE.Sample(g_pointSampler, sampleUv);
			const float4 sampleNormalDepth = GBUFFER_NORMAL.Sample(g_pointSampler, sampleUv);
			if (sampleDiffuse.a == -1.0f || sampleNormalDepth.w <= 0.0f)
				continue;

			const float3 samplePosWS = GBUFFER_POSITION.Sample(g_pointSampler, sampleUv).xyz;
			float3 sampleLighting = g_sceneLightingTex.Sample(g_linearSampler, sampleUv).rgb;
			const float sampleLuma = dot(sampleLighting, float3(0.2126f, 0.7152f, 0.0722f));
			if (sampleLuma < 0.02f)
				continue;
			// Compress very bright direct highlights so they don't stamp hard white patches into GI.
			sampleLighting = sampleLighting / (1.0f + sampleLuma * 2.0f);
			sampleLighting = min(sampleLighting, 0.60f.xxx);
			sampleLighting *= saturate(sampleDiffuse.rgb);

			const float3 sampleNormal = normalize(sampleNormalDepth.xyz + float3(1e-5f, 1e-5f, 1e-5f));
			const float3 delta = samplePosWS - centerPosWS;
			const float dist2 = max(dot(delta, delta), 1e-4f);
			const float3 dir = delta * rsqrt(dist2);

			const float facingWeight = saturate(dot(centerNormal, dir));
			const float normalWeight = saturate(dot(centerNormal, sampleNormal));
			const float depthWeight = exp(-abs(sampleNormalDepth.w - centerDepth) * 0.025f);
			const float worldWeight = 1.0f / (1.0f + dist2 * 0.05f);
			const float weight = facingWeight * normalWeight * normalWeight * depthWeight * worldWeight;

			accum += sampleLighting * weight;
			accumWeight += weight;
		}

		if (accumWeight <= 1e-4f)
			return 0.0f.xxx;

		return (accum / accumWeight) * 0.10f;
	}

	void EvaluateClipContribution(
		uint clipIdx,
		float3 uvw,
		float3 jitterUVW,
		float3 worldNormal,
		float3 screenBounce,
		out float3 voxelRadianceOut,
		out float voxelOccOut,
		out float3 probeGiOut,
		out float3 giOut)
	{
		const float clipExtent = max(g_clipCenterExtent[clipIdx].w, 1e-3f);
		const float voxelSize = max(1e-4f, g_clipVoxelInfo[clipIdx].x);
		const float stepScale = voxelSize / max(1e-4f, clipExtent * 2.0f);
		const float invRes = rcp(max(g_clipVoxelInfo[clipIdx].z, 1.0f));
		const float3 voxelTexel = invRes.xxx;

		float3 voxelRadiance = 0.0f.xxx;
		float occAccum = 0.0f;
		float accumW = 0.0f;
		float3 albedoAccum = 0.0f.xxx;
		float albedoWeightAccum = 0.0f;

		const float3 localOffsets[7] =
		{
			float3(0.0, 0.0, 0.0),
			float3(1.0, 0.0, 0.0),
			float3(-1.0, 0.0, 0.0),
			float3(0.0, 1.0, 0.0),
			float3(0.0, -1.0, 0.0),
			float3(0.0, 0.0, 1.0),
			float3(0.0, 0.0, -1.0)
		};
		[unroll]
		for (uint n = 0; n < 7; ++n)
		{
			const float w = (n == 0) ? 1.0f : 0.70f;
			const float3 suv = saturate(uvw + jitterUVW + localOffsets[n] * voxelTexel * 1.5f);
			const float4 voxelData = SampleVoxelRadiance(clipIdx, suv);
			const float4 albedoData = SampleVoxelAlbedo(clipIdx, suv);
			const float occSample = (g_giParams2.w > 0.5f) ? voxelData.a : SampleVoxelOpacity(clipIdx, suv);
			voxelRadiance += voxelData.rgb * w;
			occAccum += occSample * w;
			accumW += w;
			const float albedoW = w * saturate(albedoData.a);
			albedoAccum += saturate(albedoData.rgb) * albedoW;
			albedoWeightAccum += albedoW;
		}

		float transmittance = 1.0f;
		[unroll]
		for (uint coneStep = 0; coneStep < 2; ++coneStep)
		{
			const float rayT = (float)(coneStep + 1u) * stepScale * 3.0f;
			const float3 rayUVW = saturate(uvw + jitterUVW * 0.5f + worldNormal * rayT);
			const float4 voxelData = SampleVoxelRadiance(clipIdx, rayUVW);
			const float4 albedoData = SampleVoxelAlbedo(clipIdx, rayUVW);
			const float occSample = (g_giParams2.w > 0.5f) ? voxelData.a : SampleVoxelOpacity(clipIdx, rayUVW);
			const float w = 0.85f * transmittance;
			voxelRadiance += voxelData.rgb * w;
			occAccum += occSample * w;
			accumW += w;
			const float albedoW = w * saturate(albedoData.a) * 0.75f;
			albedoAccum += saturate(albedoData.rgb) * albedoW;
			albedoWeightAccum += albedoW;
			transmittance *= (1.0f - saturate(occSample) * 0.40f);
		}

		voxelRadiance /= max(accumW, 1e-4f);
		float voxelOcc = saturate(occAccum / max(accumW, 1e-4f));
		float3 voxelAlbedo = (albedoWeightAccum > 1e-4f)
			? saturate(albedoAccum / albedoWeightAccum)
			: 1.0f.xxx;
		float voxelAlbedoConfidence = saturate(albedoWeightAccum / max(accumW, 1e-4f));

		// Optional neighbour smoothing to reduce visible voxel-grid patterning on large flat surfaces.
		const float neighbourBlend = saturate(g_giParams6.x);
		if (neighbourBlend > 0.0001f)
		{
			const float3 nOff[6] =
			{
				float3(1.0, 0.0, 0.0),
				float3(-1.0, 0.0, 0.0),
				float3(0.0, 1.0, 0.0),
				float3(0.0, -1.0, 0.0),
				float3(0.0, 0.0, 1.0),
				float3(0.0, 0.0, -1.0)
			};

			float3 neighRad = 0.0f.xxx;
			float neighOcc = 0.0f;
			float3 neighAlb = 0.0f.xxx;
			float neighAlbConf = 0.0f;
			[unroll]
			for (uint n = 0; n < 6; ++n)
			{
				const float3 nuv = saturate(uvw + nOff[n] * voxelTexel);
				const float4 nData = SampleVoxelRadiance(clipIdx, nuv);
				const float4 nAlb = SampleVoxelAlbedo(clipIdx, nuv);
				const float nOcc = (g_giParams2.w > 0.5f) ? nData.a : SampleVoxelOpacity(clipIdx, nuv);
				neighRad += nData.rgb;
				neighOcc += nOcc;
				neighAlb += saturate(nAlb.rgb) * saturate(nAlb.a);
				neighAlbConf += saturate(nAlb.a);
			}
			neighRad *= (1.0f / 6.0f);
			neighOcc *= (1.0f / 6.0f);
			const float neighAlbInv = (neighAlbConf > 1e-4f) ? rcp(neighAlbConf) : 0.0f;
			const float3 neighAlbAvg = (neighAlbConf > 1e-4f) ? (neighAlb * neighAlbInv) : 1.0f.xxx;
			const float neighAlbConfAvg = saturate(neighAlbConf * (1.0f / 6.0f));

			voxelRadiance = lerp(voxelRadiance, neighRad, neighbourBlend);
			voxelOcc = lerp(voxelOcc, saturate(neighOcc), neighbourBlend * 0.75f);
			voxelAlbedo = lerp(voxelAlbedo, neighAlbAvg, neighbourBlend * 0.65f);
			voxelAlbedoConfidence = lerp(voxelAlbedoConfidence, neighAlbConfAvg, neighbourBlend * 0.65f);
		}

		const float3 probeIrr = SampleProbeIrradianceTrilinear(clipIdx, uvw);
		const float probeVis = SampleProbeVisibilityTrilinear(clipIdx, uvw);
		const float3 probeGi = probeIrr * lerp(0.25f, 1.0f, probeVis) * 0.90f;
		const float voxelMax = max(voxelRadiance.r, max(voxelRadiance.g, voxelRadiance.b));
		const float voxelMin = min(voxelRadiance.r, min(voxelRadiance.g, voxelRadiance.b));
		const float voxelChroma = saturate(voxelMax - voxelMin);
		const float albedoMax = max(voxelAlbedo.r, max(voxelAlbedo.g, voxelAlbedo.b));
		const float albedoMin = min(voxelAlbedo.r, min(voxelAlbedo.g, voxelAlbedo.b));
		const float albedoChroma = saturate(albedoMax - albedoMin);
		const float chromaGuidance = max(voxelChroma, albedoChroma * voxelAlbedoConfidence);

		const float horizon = saturate(worldNormal.y * 0.5f + 0.5f);
		const float probeBlendBase = saturate(g_giParams2.y);
		// Keep probe contribution lower in strongly chromatic voxel regions (typical local colored lights)
		// to avoid desaturating into neutral/white halos.
		const float probeBlend = probeBlendBase * (1.0f - chromaGuidance * 0.75f);
		float3 gi = lerp(voxelRadiance * 0.85f, probeGi, saturate(probeBlend));
		gi *= lerp(0.55f, 1.0f, horizon);
		gi *= (1.0f - voxelOcc * 0.12f);
		gi += screenBounce * g_giParams2.x;
		const float albedoConfidenceSoft = smoothstep(0.15f, 0.80f, voxelAlbedoConfidence);
		const float albedoInfluence = saturate(g_giParams6.z) * albedoConfidenceSoft;
		const float3 stableAlbedo = max(voxelAlbedo, float3(0.35f, 0.35f, 0.35f));
		const float3 albedoTint = lerp(1.0f.xxx, stableAlbedo, albedoInfluence * 0.85f);
		const float tintLuma = max(dot(albedoTint, float3(0.2126f, 0.7152f, 0.0722f)), 0.35f);
		gi *= (albedoTint / tintLuma);

		// Preserve voxel tint in high-chroma regions after probe/screen blending.
		const float giLum = dot(gi, float3(0.2126f, 0.7152f, 0.0722f));
		const float voxelLum = dot(voxelRadiance, float3(0.2126f, 0.7152f, 0.0722f));
		if (voxelLum > 1e-4f && giLum > 1e-4f)
		{
			const float3 voxelTint = max(0.0f.xxx, voxelRadiance / voxelLum);
			const float3 giTinted = voxelTint * giLum;
			const float tintStrength = saturate(chromaGuidance * 1.8f) * saturate(voxelLum * 1.4f);
			gi = lerp(gi, giTinted, tintStrength * 0.75f);
		}

		voxelRadianceOut = voxelRadiance;
		voxelOccOut = voxelOcc;
		probeGiOut = probeGi;
		giOut = gi;
	}

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		const float2 uv = input.texcoord;

		const float4 pixelDiffuse = GBUFFER_DIFFUSE.Sample(g_pointSampler, uv);
		const float4 pixelNormalDepth = GBUFFER_NORMAL.Sample(g_pointSampler, uv);
		const float4 pixelPosWS = GBUFFER_POSITION.Sample(g_pointSampler, uv);
		const float2 pixelVelocity = GBUFFER_VELOCITY.Sample(g_pointSampler, uv).xy;

		const float depth = pixelNormalDepth.w;
		const bool skyPixel = (pixelDiffuse.a == -1.0f) || (depth <= 0.0f);
		if (skyPixel)
			return float4(0.0f, 0.0f, 0.0f, 1.0f);

		const float2 velPixels = abs(pixelVelocity) * float2((float)g_screenWidth, (float)g_screenHeight);
		const float pixelMotion = max(velPixels.x, velPixels.y);
		const float pixelMotionStart = max(g_giParams4.z, 0.0f);
		const float pixelMotionStrength = max(g_giParams4.w, 0.0f);
		const float motionFactor = saturate((pixelMotion - pixelMotionStart) * pixelMotionStrength);
		const float motionClipBias = saturate(g_giParams7.w);
		const float warmStabilize = saturate((g_giParams1.x - 0.84f) * 8.0f);
		const float shiftSettle = saturate(g_giParams6.y);

		const float3 worldNormal = normalize(pixelNormalDepth.xyz);
		const float3 screenBounce = (g_giParams2.x > 0.0001f)
			? ComputeScreenSpaceBounce(uv, pixelPosWS.xyz, worldNormal, depth)
			: 0.0f.xxx;
		const float debugMode = g_giParams0.z;
		float3 gi = 0.0f.xxx;
		float3 probeGi = 0.0f.xxx;
		float3 debugVoxelRadiance = 0.0f.xxx;
		float3 debugClipBlend = 0.0f.xxx;
		float voxelOcc = 0.0f;
		uint chosenClip = 0;
		float chosenClipWeight = 0.0f;
		float totalClipWeight = 0.0f;
		bool foundClip = false;
		float3 fallbackGi = 0.0f.xxx;
		float3 fallbackProbe = 0.0f.xxx;
		float3 fallbackVoxel = 0.0f.xxx;
		float fallbackOcc = 0.0f;
		uint fallbackClip = 0;

		[unroll]
		for (uint i = 0; i < 4; ++i)
		{
			const float3 clipCenter = g_clipCenterExtent[i].xyz;
			const float clipExtent = max(g_clipCenterExtent[i].w, 1e-3f);
			const float3 uvw = ((pixelPosWS.xyz - clipCenter) / (clipExtent * 2.0f)) + 0.5f;
			if (!IsInside01(uvw))
				continue;
			float3 voxelCurrent = 0.0f.xxx;
			float occCurrent = 0.0f;
			float3 probeCurrent = 0.0f.xxx;
			float3 giCurrent = 0.0f.xxx;
			const float invRes = rcp(max(g_clipVoxelInfo[i].z, 1.0f));
			// World-space seed keeps voxel sampling stable while the camera moves.
			const float2 worldSeed = float2(
				pixelPosWS.x * 0.173f + pixelPosWS.y * 0.097f,
				pixelPosWS.z * 0.191f + pixelPosWS.y * 0.113f);
			const float2 seed = worldSeed + float2((float)(i + 1u) * 13.17f, (float)(i + 1u) * 17.31f);
			const float jitterScale =
				max(g_giParams4.x, 0.0f) *
				lerp(1.0f, 0.05f, motionFactor) *
				lerp(1.0f, 0.20f, warmStabilize) *
				lerp(1.0f, 0.30f, shiftSettle);
			const float3 jitterUVW =
				(float3(
					Hash12(seed + float2(19.91f, 7.13f)),
					Hash12(seed.yx + float2(5.71f, 29.37f)),
					Hash12(seed + float2(41.27f, 3.97f))) - float3(0.5f, 0.5f, 0.5f)) * (invRes * jitterScale);
			EvaluateClipContribution(i, uvw, jitterUVW, worldNormal, screenBounce, voxelCurrent, occCurrent, probeCurrent, giCurrent);

			// Blend all overlapping clipmaps with normalized weights to avoid visible handoff rings.
			const float edgeDistanceX = min(uvw.x, 1.0f - uvw.x);
			const float edgeDistanceZ = min(uvw.z, 1.0f - uvw.z);
			const float edgeDistance = min(edgeDistanceX, edgeDistanceZ);
			const float blendWidth = max(0.001f, saturate(g_giParams4.y + motionFactor * 0.03f + warmStabilize * 0.08f + shiftSettle * 0.08f));
			const float edgeWeight = smoothstep(0.0f, blendWidth, edgeDistance);
			const float fidelityWeight = exp2(-2.0f * (float)i);
			float clipWeight = edgeWeight * fidelityWeight;
			// During clipmap shift settle, reduce cross-clip weight churn by biasing toward
			// higher-fidelity clipmaps (especially clip 0 when available).
			if (shiftSettle > 0.0001f)
			{
				const float settleBias = lerp(1.0f, exp2(-2.5f * (float)i), shiftSettle);
				clipWeight *= settleBias;
			}
			// Keep some preference for the near clip while moving, but avoid creating
			// a bright camera-centered bubble by collapsing almost entirely to clip 0.
			if (motionClipBias > 0.0001f)
			{
				const float motionBias = lerp(1.0f, exp2(-1.5f * (float)i), motionClipBias * 0.45f);
				clipWeight *= motionBias;
			}
			if (i == 3u)
			{
				clipWeight = max(clipWeight, 0.01f * fidelityWeight);
			}

			if (clipWeight > 0.0001f)
			{
				gi += giCurrent * clipWeight;
				probeGi += probeCurrent * clipWeight;
				debugVoxelRadiance += voxelCurrent * clipWeight;
				debugClipBlend += kClipDebugColours[i] * clipWeight;
				voxelOcc += occCurrent * clipWeight;
				totalClipWeight += clipWeight;
				if (clipWeight > chosenClipWeight)
				{
					chosenClipWeight = clipWeight;
					chosenClip = i;
				}
			}

			fallbackGi = giCurrent;
			fallbackProbe = probeCurrent;
			fallbackVoxel = voxelCurrent;
			fallbackOcc = occCurrent;
			fallbackClip = i;
			foundClip = true;
		}

		if (totalClipWeight > 1e-4f)
		{
			const float invWeight = rcp(totalClipWeight);
			gi *= invWeight;
			probeGi *= invWeight;
			debugVoxelRadiance *= invWeight;
			debugClipBlend *= invWeight;
			voxelOcc *= invWeight;
		}
		else if (foundClip)
		{
			gi = fallbackGi;
			probeGi = fallbackProbe;
			debugVoxelRadiance = fallbackVoxel;
			debugClipBlend = kClipDebugColours[fallbackClip];
			voxelOcc = fallbackOcc;
			chosenClip = fallbackClip;
		}

		if (debugMode == 2.0f)
		{
			return float4(probeGi, 1.0f);
		}
		if (debugMode == 3.0f)
		{
			const float3 vis = debugVoxelRadiance * 1.65f + voxelOcc * 0.08f;
			return float4(vis, 1.0f);
		}
		if (debugMode == 4.0f)
		{
			return foundClip ? float4(saturate(debugClipBlend), 1.0f) : float4(0.0f, 0.0f, 0.0f, 1.0f);
		}

		const float raysPerProbe = foundClip ? g_clipVoxelInfo[chosenClip].w : 1.0f;
		const float rayQuality = saturate((raysPerProbe - 1.0f) / 7.0f);
		gi *= lerp(0.85f, 1.0f, rayQuality);
		gi *= lerp(0.35f.xxx, 1.00f.xxx, saturate(pixelDiffuse.rgb));

		// Reduce indirect on strongly sun-facing receivers to avoid same-surface "self-bounce"
		// dominating over neighboring bounce transfer.
		// Intentionally avoid sampling full scene-lighting here to keep local direct lights from
		// leaking into GI modulation when they are not injecting into GI.
		const float3 sunDir = normalize(g_giParams3.xyz + float3(1e-5f, 1e-5f, 1e-5f));
		const float sunFacing = saturate(dot(worldNormal, -sunDir));
		const float sunDirectionality = saturate(g_giParams3.w);
		const float directMask = sunFacing * sunDirectionality;
		gi *= lerp(1.0f, 0.38f, directMask);
		// Do not scale GI intensity by camera-motion bias; this causes visible dimming while moving.
		// Motion handling should come from sampling/temporal stability, not energy attenuation.
		gi *= 1.0f;

		gi = gi / (1.0f + gi * 0.18f);

		gi *= g_giParams0.x;
		gi = min(gi, g_giParams0.y.xxx);
		return float4(gi, 1.0f);
	}
}
