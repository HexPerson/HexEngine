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
	// Directional (SH L1) moment volumes, clip-major - bound only when
	// g_giParams13.w > 0.5 (r_giDirectionalVoxels).
	Texture3D g_voxelL1xTex0 : register(t26);
	Texture3D g_voxelL1yTex0 : register(t27);
	Texture3D g_voxelL1zTex0 : register(t28);
	Texture3D g_voxelL1xTex1 : register(t29);
	Texture3D g_voxelL1yTex1 : register(t30);
	Texture3D g_voxelL1zTex1 : register(t31);
	Texture3D g_voxelL1xTex2 : register(t32);
	Texture3D g_voxelL1yTex2 : register(t33);
	Texture3D g_voxelL1zTex2 : register(t34);
	Texture3D g_voxelL1xTex3 : register(t35);
	Texture3D g_voxelL1yTex3 : register(t36);
	Texture3D g_voxelL1zTex3 : register(t37);

	SamplerState g_pointSampler : register(s2);
	SamplerState g_linearSampler : register(s4);

	cbuffer GIConstants : register(b4)
	{
		float4 g_clipCenterExtent[4];
		float4 g_clipPreviousCenterExtent[4];
		float4 g_clipVoxelInfo[4];
		float4 g_giParams0; // x=intensity, y=energyClamp, z=debugMode, w=activeClipmap
		float4 g_giParams1; // x=hysteresis, y=historyReject, z=halfInvW, w=halfInvH
		float4 g_giParams2; // x=screenBounce, y=probeBlend, z=voxelDecay, w=useVoxelAlphaOpacity
		float4 g_giParams3; // xyz=sunDirectionWS, w=sunDirectionality
		float4 g_giParams4; // x=jitterScale, y=clipBlendWidth, z=pixelMotionStart, w=pixelMotionStrength
		float4 g_giParams5; // x=luminanceRejectScale, y=ditherDarkAmp, z=ditherBrightAmp, w=movementPreset
		float4 g_giParams6; // x=voxelNeighbourBlend, y=shiftSettle, z=voxelAlbedoInfluence, w=reserved
		float4 g_giParams7; // x=gpuMaterialProxyBlend, y=gpuComputeBaseSunEnabled, z=sunShadowPerVoxel, w=cameraMotionBlend
		float4 g_giParams8;
		float4 g_giParams9;
		float4 g_giParams10;
		float4 g_giParams11; // x=localLightInjection, y=clipAttenuation, z=receiverMinLuma, w=receiverRemapAmount
		float4 g_giParams12; // x=live triangle count, y=candidate routing, z=snap boost, w reserved
		float4 g_giParams13; // x=litInjection strength, y=litInjection maxLuma, z=feedback bound, w=directional voxels active
		float4 g_giParams14; // x=ssgi intensity (0=off), y=ssgi radius (world m), z/w reserved
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

	// Luminance-preserving compression. Compresses overall brightness through a Reinhard
	// curve while keeping channel ratios fixed - avoids the per-channel-Reinhard hue shift
	// where a saturated channel desaturates faster than its neighbours and forces the output
	// toward gray (or, with the prior `min(., k.xxx)`, lets the saturated channel pin to k).
	float3 ReinhardCompressLuminance(float3 rgb, float k)
	{
		const float lum = dot(rgb, float3(0.2126f, 0.7152f, 0.0722f));
		if (lum <= 1e-6f)
		{
			return rgb;
		}
		const float compressed = lum / (1.0f + lum * k);
		return rgb * (compressed / lum);
	}

	float3 LuminanceClamp(float3 rgb, float maxLuma)
	{
		// Dual cap with 1.0x channel ratio - both luminance and peak channel capped at the
		// same threshold. Earlier 1.5x ratio let saturated single-channel values like
		// (0, 8, 0) slip through unclamped (luma 5.7 < cap, channel 8 < 1.5*cap=12).
		const float lum = dot(rgb, float3(0.2126f, 0.7152f, 0.0722f));
		const float channelMax = max(max(rgb.r, rgb.g), rgb.b);
		float scale = 1.0f;
		if (lum > maxLuma && lum > 1e-6f)
			scale = min(scale, maxLuma / lum);
		if (channelMax > maxLuma && channelMax > 1e-6f)
			scale = min(scale, maxLuma / channelMax);
		return rgb * scale;
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

	// Directional evaluation factor: how much of this location's voxel
	// radiance actually exits toward a receiver facing N. SH band-1:
	// E(N) = max(0, 0.5*L0 + 0.5*L1.N); returned as a per-channel ratio
	// against L0 so the caller can scale its (multi-tap smoothed) radiance.
	// Fully aligned with the emitting surface -> 1, behind it -> 0,
	// side-on -> 0.5. This is what stops GI wrapping around silhouettes.
	float3 DirectionalVoxelFactor(uint clipIdx, float3 uvw, float3 receiverNormal)
	{
		float3 l1x;
		float3 l1y;
		float3 l1z;
		float3 l0;
		switch (clipIdx)
		{
		case 0:
			l0 = g_voxelRadianceTex0.SampleLevel(g_linearSampler, uvw, 0.0f).rgb;
			l1x = g_voxelL1xTex0.SampleLevel(g_linearSampler, uvw, 0.0f).rgb;
			l1y = g_voxelL1yTex0.SampleLevel(g_linearSampler, uvw, 0.0f).rgb;
			l1z = g_voxelL1zTex0.SampleLevel(g_linearSampler, uvw, 0.0f).rgb;
			break;
		case 1:
			l0 = g_voxelRadianceTex1.SampleLevel(g_linearSampler, uvw, 0.0f).rgb;
			l1x = g_voxelL1xTex1.SampleLevel(g_linearSampler, uvw, 0.0f).rgb;
			l1y = g_voxelL1yTex1.SampleLevel(g_linearSampler, uvw, 0.0f).rgb;
			l1z = g_voxelL1zTex1.SampleLevel(g_linearSampler, uvw, 0.0f).rgb;
			break;
		case 2:
			l0 = g_voxelRadianceTex2.SampleLevel(g_linearSampler, uvw, 0.0f).rgb;
			l1x = g_voxelL1xTex2.SampleLevel(g_linearSampler, uvw, 0.0f).rgb;
			l1y = g_voxelL1yTex2.SampleLevel(g_linearSampler, uvw, 0.0f).rgb;
			l1z = g_voxelL1zTex2.SampleLevel(g_linearSampler, uvw, 0.0f).rgb;
			break;
		default:
			l0 = g_voxelRadianceTex3.SampleLevel(g_linearSampler, uvw, 0.0f).rgb;
			l1x = g_voxelL1xTex3.SampleLevel(g_linearSampler, uvw, 0.0f).rgb;
			l1y = g_voxelL1yTex3.SampleLevel(g_linearSampler, uvw, 0.0f).rgb;
			l1z = g_voxelL1zTex3.SampleLevel(g_linearSampler, uvw, 0.0f).rgb;
			break;
		}
		// L1 stores the EXIT (propagation) direction of the radiance. A
		// receiver with normal N is hit by light travelling INTO its surface,
		// i.e. propagation directions opposing N - so the arrival lobe is
		// evaluated at -N: light exiting straight toward the receiver scores
		// 1, light exiting away (the wrap-around leak) scores 0, side-on 0.5.
		const float3 e = max(0.0f.xxx, 0.5f * l0 - 0.5f * (l1x * receiverNormal.x + l1y * receiverNormal.y + l1z * receiverNormal.z));
		return saturate(e / max(l0, 1e-4f.xxx));
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

	// SSGI: screen-space short-range irradiance gather. A 12-tap golden-angle
	// disc over a WORLD-space radius, using real gbuffer positions so the
	// falloff and cosine terms are geometrically correct - this supplies the
	// contact-scale bounce detail the metre-scale voxel field cannot carry.
	// Runs at the GI half-res trace and inherits the existing resolve
	// temporal filtering + bilateral upsample for free.
	float3 ComputeSSGI(float2 uv, float3 centerPosWS, float3 centerNormal)
	{
		const float2 fullTexel = float2(
			1.0f / max(1.0f, (float)g_screenWidth),
			1.0f / max(1.0f, (float)g_screenHeight));
		const float radiusWs = max(g_giParams14.y, 0.25f);

		// World-metres-per-pixel estimated from the position buffer; clamped
		// hard because depth edges make the estimate spiky.
		const float3 posRight = GBUFFER_POSITION.Sample(g_pointSampler, saturate(uv + float2(fullTexel.x * 4.0f, 0.0f))).xyz;
		const float worldPerPixel = clamp(length(posRight - centerPosWS) * 0.25f, 1e-4f, 0.5f);
		const float radiusPixels = clamp(radiusWs / worldPerPixel, 4.0f, 160.0f);

		// Golden-angle spiral, 12 taps. Per-pixel rotation from a screen hash
		// decorrelates the pattern; the resolve's temporal pass integrates it.
		const float hash = frac(sin(dot(uv, float2(12.9898f, 78.233f))) * 43758.5453f);
		const float baseAngle = hash * 6.2831853f;
		const float falloffR2 = radiusWs * radiusWs * 0.25f;

		float3 accum = 0.0f.xxx;

		[unroll]
		for (uint i = 0u; i < 12u; ++i)
		{
			const float t = ((float)i + 0.5f) / 12.0f;
			const float ringRadius = radiusPixels * sqrt(t);
			const float angle = baseAngle + (float)i * 2.3999632f; // golden angle
			const float2 sampleUv = saturate(uv + float2(cos(angle), sin(angle)) * ringRadius * fullTexel);

			const float4 sampleDiffuse = GBUFFER_DIFFUSE.Sample(g_pointSampler, sampleUv);
			const float4 sampleNormalDepth = GBUFFER_NORMAL.Sample(g_pointSampler, sampleUv);
			if (sampleDiffuse.a == -1.0f || sampleNormalDepth.w <= 0.0f)
				continue;

			const float3 samplePosWS = GBUFFER_POSITION.Sample(g_pointSampler, sampleUv).xyz;
			const float3 delta = samplePosWS - centerPosWS;
			const float dist2 = dot(delta, delta);
			// Reject samples outside the world radius (screen disc can catch
			// distant geometry across depth discontinuities).
			if (dist2 > radiusWs * radiusWs || dist2 < 1e-6f)
				continue;
			const float3 dir = delta * rsqrt(dist2);

			// Receiver cosine: light arriving from the sample direction.
			const float receiverCos = saturate(dot(centerNormal, dir));
			if (receiverCos <= 0.001f)
				continue;
			// Emitter cosine: the sample surface must face the receiver.
			const float3 sampleNormal = normalize(sampleNormalDepth.xyz + float3(1e-5f, 1e-5f, 1e-5f));
			const float emitterCos = saturate(dot(sampleNormal, -dir));
			if (emitterCos <= 0.001f)
				continue;

			float3 sampleLighting = g_sceneLightingTex.Sample(g_linearSampler, sampleUv).rgb;
			const float sampleLuma = dot(sampleLighting, float3(0.2126f, 0.7152f, 0.0722f));
			// Compress bright direct highlights so sun pools don't stamp hard
			// patches into the gather (same treatment as the screen bounce).
			sampleLighting = sampleLighting / (1.0f + sampleLuma * 1.5f);
			sampleLighting = min(sampleLighting, 0.8f.xxx);

			const float falloff = falloffR2 / (falloffR2 + dist2);
			const float w = receiverCos * emitterCos * falloff;
			accum += sampleLighting * w;
		}

		// Normalise by the taps, not the surviving weight - empty
		// surroundings must mean LESS gathered light, not the same.
		return accum * (1.0f / 12.0f) * 2.5f;
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
		out float3 giOut,
		out float3 voxelAlbedoOut,
		out float voxelAlbedoConfOut)
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

		// Directional (SH-1) arrival factor, sampled once at the tap centre.
		// Applied ONLY to low-occupancy (air) samples: air voxels hold
		// PROPAGATED radiance from other surfaces - the carrier of the
		// silhouette leak - and their moment says which way it travels. The
		// receiver's own SURFACE voxels hold its accumulated exitant bounce,
		// whose moment points along the receiver's own normal; filtering
		// those by arrival direction cancels the receiver's own GI (the
		// builds-up-then-resolves-to-nothing collapse as the moment field
		// converges). Occupancy gates the two regimes per tap.
		const bool directionalArrival = g_giParams13.w > 0.5f;
		float3 arrivalFactor = 1.0f.xxx;
		if (directionalArrival)
		{
			arrivalFactor = DirectionalVoxelFactor(clipIdx, saturate(uvw + jitterUVW), worldNormal);
		}

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
			float3 tapRadiance = voxelData.rgb;
			if (directionalArrival)
			{
				tapRadiance *= lerp(max(arrivalFactor, 0.35f.xxx), 1.0f.xxx, saturate(occSample * 2.0f));
			}
			voxelRadiance += tapRadiance * w;
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
			float3 coneRadiance = voxelData.rgb;
			if (directionalArrival)
			{
				coneRadiance *= lerp(max(arrivalFactor, 0.35f.xxx), 1.0f.xxx, saturate(occSample * 2.0f));
			}
			voxelRadiance += coneRadiance * w;
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
		float3 gi = lerp(voxelRadiance * 0.92f, probeGi, saturate(probeBlend));
		gi *= lerp(0.55f, 1.0f, horizon);
		gi *= (1.0f - voxelOcc * 0.12f);
		gi += screenBounce * g_giParams2.x;

		voxelRadianceOut = voxelRadiance;
		voxelOccOut = voxelOcc;
		probeGiOut = probeGi;
		giOut = gi;
		voxelAlbedoOut = voxelAlbedo;
		voxelAlbedoConfOut = voxelAlbedoConfidence;
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
		// Sky pixels: zero GI contribution AND zero alpha so the AO consumer
		// reads "no occlusion" here. With the v1 layout (alpha used as
		// occlusion factor) returning alpha=1 here would tell the AO apply
		// pass to fully occlude every sky pixel, blacking out the sky.
		if (skyPixel)
			return float4(0.0f, 0.0f, 0.0f, 0.0f);

		const float2 velPixels = abs(pixelVelocity) * float2((float)g_screenWidth, (float)g_screenHeight);
		const float pixelMotion = max(velPixels.x, velPixels.y);
		const float pixelMotionStart = max(g_giParams4.z, 0.0f);
		const float pixelMotionStrength = max(g_giParams4.w, 0.0f);
		const float motionFactor = saturate((pixelMotion - pixelMotionStart) * pixelMotionStrength);
		// Keep a small baseline near-clip preference even when stationary.
		// Without this, fully neutral clip blending can over-weight broad sparse clips and
		// make local GI fade toward black after motion settles.
		const float motionClipBias = saturate(0.25f + g_giParams7.w * 0.75f);
		const float warmStabilize = saturate((g_giParams1.x - 0.84f) * 8.0f);
		const float shiftSettle = saturate(g_giParams6.y);
		const float clip0Block = shiftSettle;

		const float3 worldNormal = normalize(pixelNormalDepth.xyz);
		const float3 screenBounce = (g_giParams2.x > 0.0001f)
			? ComputeScreenSpaceBounce(uv, pixelPosWS.xyz, worldNormal, depth)
			: 0.0f.xxx;
		const float debugMode = g_giParams0.z;
		float3 gi = 0.0f.xxx;
		float3 probeGi = 0.0f.xxx;
		float3 debugVoxelRadiance = 0.0f.xxx;
		float3 debugVoxelAlbedo = 0.0f.xxx;
		float debugVoxelAlbedoConf = 0.0f;
		float3 debugClipBlend = 0.0f.xxx;
		float voxelOcc = 0.0f;
		uint chosenClip = 0;
		float chosenClipWeight = 0.0f;
		float totalClipWeight = 0.0f;
		bool foundClip = false;
		float3 fallbackGi = 0.0f.xxx;
		float3 fallbackProbe = 0.0f.xxx;
		float3 fallbackVoxel = 0.0f.xxx;
		float3 fallbackAlbedo = 0.0f.xxx;
		float fallbackAlbedoConf = 0.0f;
		float fallbackOcc = 0.0f;
		uint fallbackClip = 0;

		[unroll]
		for (uint i = 0; i < 4; ++i)
		{
			const float3 clipCenter = g_clipCenterExtent[i].xyz;
			const float3 previousClipCenter = g_clipPreviousCenterExtent[i].xyz;
			const float clipSettle = saturate(g_clipPreviousCenterExtent[i].w);
			const float clipExtent = max(g_clipCenterExtent[i].w, 1e-3f);
			const float3 uvw = ((pixelPosWS.xyz - clipCenter) / (clipExtent * 2.0f)) + 0.5f;
			if (!IsInside01(uvw))
				continue;
			float3 voxelCurrent = 0.0f.xxx;
			float occCurrent = 0.0f;
			float3 probeCurrent = 0.0f.xxx;
			float3 giCurrent = 0.0f.xxx;
			float3 albedoCurrent = 0.0f.xxx;
			float albedoConfCurrent = 0.0f;
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
			EvaluateClipContribution(i, uvw, jitterUVW, worldNormal, screenBounce, voxelCurrent, occCurrent, probeCurrent, giCurrent, albedoCurrent, albedoConfCurrent);
			// Blend all overlapping clipmaps with normalized weights to avoid visible handoff rings.
			const float edgeDistanceX = min(uvw.x, 1.0f - uvw.x);
			const float edgeDistanceZ = min(uvw.z, 1.0f - uvw.z);
			const float edgeDistance = min(edgeDistanceX, edgeDistanceZ);
			const float blendWidth = max(0.001f, saturate(g_giParams4.y + motionFactor * 0.03f + warmStabilize * 0.08f + shiftSettle * 0.08f));
			const float edgeWeight = smoothstep(0.0f, blendWidth, edgeDistance);
			const float fidelityWeight = rcp(1.0f + 0.35f * (float)i);
			float clipWeight = edgeWeight * fidelityWeight;
			// Per-clipmap stabilityScale rebalance during shift frames was removed: it scaled
			// clip 0 down to 0.93x and clip 1 up to 1.05x for the duration of a pending shift,
			// which biased the trace toward the larger-volume clip 1 (typically carrying more
			// accumulated bounced light) every time a shift triggered. With camera movement
			// continually triggering shifts, this contributed a small but visible motion-state
			// brightening on top of the propagation-iteration effect. Removed for consistent
			// GI magnitude across motion states.
			// Clip 0 / clip 1 rebalancing during shifts also removed: it scaled clip 0 down to
			// 0.85x and clip 1 up to 1.10x while clip0Block (= shiftSettle) was non-zero, which
			// is exactly during the frames camera movement triggers a voxel-field shift. Same
			// motion-state-brightness inconsistency as the per-clip stability rebalance above.
			// Keep some preference for the near clip while moving, but avoid creating
			// a bright camera-centered bubble by collapsing almost entirely to clip 0.
			if (motionClipBias > 0.0001f && clip0Block < 0.001f)
			{
				const float motionBias = lerp(1.0f, rcp(1.0f + 0.20f * (float)i), motionClipBias * 0.35f);
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
				debugVoxelAlbedo += albedoCurrent * clipWeight;
				debugVoxelAlbedoConf += albedoConfCurrent * clipWeight;
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
			fallbackAlbedo = albedoCurrent;
			fallbackAlbedoConf = albedoConfCurrent;
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
			debugVoxelAlbedo *= invWeight;
			debugVoxelAlbedoConf *= invWeight;
			debugClipBlend *= invWeight;
			voxelOcc *= invWeight;
		}
		else if (foundClip)
		{
			gi = fallbackGi;
			probeGi = fallbackProbe;
			debugVoxelRadiance = fallbackVoxel;
			debugVoxelAlbedo = fallbackAlbedo;
			debugVoxelAlbedoConf = fallbackAlbedoConf;
			debugClipBlend = kClipDebugColours[fallbackClip];
			voxelOcc = fallbackOcc;
			chosenClip = fallbackClip;
		}

		// SSGI (opt-in): contact-scale screen-space gather layered on the
		// voxel far field. Added to incident gi BEFORE the receiver-albedo
		// remap below, so it tints like any other arriving light.
		if (g_giParams14.x > 0.0001f)
		{
			gi += ComputeSSGI(uv, pixelPosWS.xyz, worldNormal) * g_giParams14.x;
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
		if (debugMode == 5.0f)
		{
			// Voxel ALBEDO volume, confidence-scaled: black = albedo never written,
			// grey/white = written but untinted (injection-side bug), coloured = healthy.
			return float4(saturate(debugVoxelAlbedo) * saturate(debugVoxelAlbedoConf * 2.0f), 1.0f);
		}

		const float raysPerProbe = foundClip ? g_clipVoxelInfo[chosenClip].w : 1.0f;
		const float rayQuality = saturate((raysPerProbe - 1.0f) / 7.0f);
		const float probeUsage = saturate(g_giParams2.y * 4.0f);
		gi *= lerp(1.0f, lerp(0.85f, 1.0f, rayQuality), probeUsage);
		const float3 receiverAlbedo = saturate(pixelDiffuse.rgb);
		const float receiverLuma = max(dot(receiverAlbedo, float3(0.2126f, 0.7152f, 0.0722f)), 1e-4f);
		const float receiverMinLuma = saturate(g_giParams11.z);
		const float receiverRemapAmount = saturate(g_giParams11.w);
		const float receiverLiftedLuma = max(receiverLuma, receiverMinLuma);
		const float receiverTransportLuma = lerp(receiverLuma, receiverLiftedLuma, receiverRemapAmount);
		const float3 receiverChroma = receiverAlbedo / receiverLuma;
		const float3 receiverIndirectAlbedo = saturate(receiverChroma * receiverTransportLuma);
		gi *= receiverIndirectAlbedo;

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

		// Luminance Reinhard (was per-channel `gi / (1 + gi * 0.18)`). The per-channel form
		// compressed each channel independently, so a saturated single-channel input like
		// (24, 1, 1) collapsed to (4.4, 0.85, 0.85) - a strong hue shift toward white that
		// then drove the temporal history clamp to bake in the desaturated colour. Compressing
		// by luminance preserves the (R,G,B) ratio.
		gi = ReinhardCompressLuminance(gi, 0.18f);

		gi *= g_giParams0.x;
		// Final safety clamp - was a per-channel min, which under saturated-channel inputs
		// would pin one channel to the cap and leave the others below it, producing the
		// R/G/B blowouts seen in-game.
		gi = LuminanceClamp(gi, g_giParams0.y);

		// Pack the per-pixel accumulated voxel occlusion into the alpha channel.
		// voxelOcc is already in [0, 1] after all the EvaluateClipContribution +
		// fallback resolves above; storing it here lets the resolve pass run
		// the same temporal accumulation on it as on the RGB GI, and lets the
		// DiffuseGIAOProvider read it back as a free SSAO signal without an
		// extra trace pass. (1 - alpha) at sample time becomes the AO factor.
		return float4(gi, saturate(voxelOcc));
	}
}
