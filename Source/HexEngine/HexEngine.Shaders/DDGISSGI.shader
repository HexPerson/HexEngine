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
	Global
	DDGICommon
}
"VertexShader"
{
	UIPixelInput ShaderMain(UIVertexInput input)
	{
		UIPixelInput output;
		output.position = input.position;
		output.texcoord = input.texcoord;
		return output;
	}
}
"PixelShader"
{
	// DDGI near-field screen-space bounce, gathered at HALF resolution
	// (DDGI.cpp) and bilaterally upsampled by DDGIApply. Full-res it cost
	// ~0.35 ms per tap at 1440p (scattered 16-byte G-buffer reads), so 16
	// taps were ~5.7 ms; half-res is a quarter of that.
	//
	// Output: rgb = gathered radiance (before r_ddgiSSGIIntensity),
	//         a   = eye distance of the gathering pixel (0 = no data / sky),
	//               used by the upsample as its edge-stopping key.
	//
	// Adapted from DiffuseGITrace's ComputeSSGI, minus its highlight crush:
	// that version divided by (1 + 1.5 * luma) and capped at 0.8, which
	// flattened exactly the bright sun pools whose bounce this is for; here
	// only a high per-tap safety cap remains. Geometry terms are unchanged:
	// wrapped receiver/emitter cosines, softened inverse-square falloff, and
	// normalisation by tap count (empty surroundings gather less). The
	// per-frame rotation is integrated by TAA.

	GBUFFER_RESOURCE(0, 1, 2, 3, 4);
	// Direct-lit beauty (with DDGI active the deferred pass adds no flat
	// ambient or sky fill, so this is direct light + emissive only).
	Texture2D g_ddgiLitScene : register(t5);
	SamplerState g_pointSampler : register(s2);
	SamplerState g_linearSampler : register(s4);

	float3 DdgiSSGI(float2 uv, float3 centerPosWS, float3 centerNormal)
	{
		const float2 fullTexel = float2(1.0f / max(1.0f, (float)g_screenWidth), 1.0f / max(1.0f, (float)g_screenHeight));
		const float radiusWs = max(g_ddgiSSGI.y, 0.25f);
		const float3 posRight = GBUFFER_POSITION.Sample(g_pointSampler, saturate(uv + float2(fullTexel.x * 4.0f, 0.0f))).xyz;
		const float worldPerPixel = clamp(length(posRight - centerPosWS) * 0.25f, 1e-4f, 0.5f);
		const float radiusPixels = clamp(radiusWs / worldPerPixel, 4.0f, 200.0f);
		const uint taps = (uint)clamp(g_ddgiSSGI.w, 4.0f, 32.0f);

		const float frameJitter = (float)(g_frame % 16u) * 0.3926991f;
		// Interleaved gradient noise (Jimenez 2014), not a white-noise hash:
		// neighbouring pixels get well-spread rotations, so the 5x5
		// bilateral denoise (DDGISSGIBlur) averages the pattern out instead
		// of leaving clumped white-noise fizz.
		const float2 px = floor(uv * float2((float)g_screenWidth, (float)g_screenHeight) * 0.5f);
		const float hash = frac(52.9829189f * frac(dot(px, float2(0.06711056f, 0.00583715f))));
		const float baseAngle = hash * 6.2831853f + frameJitter;
		const float radius2 = radiusWs * radiusWs;
		const float falloffR2 = radius2 * 0.25f;
		const float cap = max(g_ddgiSSGI.z, 0.1f);

		float3 accum = 0.0f.xxx;
		[loop]
		for (uint i = 0u; i < taps; ++i)
		{
			const float t = ((float)i + 0.5f) / (float)taps;
			const float ringRadius = radiusPixels * sqrt(t);
			const float angle = baseAngle + (float)i * 2.3999632f;
			const float2 sampleUv = saturate(uv + float2(cos(angle), sin(angle)) * ringRadius * fullTexel);
			// Position first: it rejects sky and out-of-range taps before
			// any other G-buffer read (the old order read albedo + normal
			// for every tap).
			const float4 samplePos = GBUFFER_POSITION.Sample(g_pointSampler, sampleUv);
			if (samplePos.a > 0.0f)
				continue;
			const float3 delta = samplePos.xyz - centerPosWS;
			const float dist2 = dot(delta, delta);
			if (dist2 > radius2 || dist2 < 1e-6f)
				continue;
			const float3 dir = delta * rsqrt(dist2);
			const float receiverCos = saturate((dot(centerNormal, dir) + 0.35f) / 1.35f);
			if (receiverCos <= 0.001f)
				continue;
			const float4 sampleNormalDepth = GBUFFER_NORMAL.Sample(g_pointSampler, sampleUv);
			if (sampleNormalDepth.w <= 0.0f)
				continue;
			const float3 sampleNormal = normalize(sampleNormalDepth.xyz + float3(1e-5f, 1e-5f, 1e-5f));
			const float emitterCos = saturate((dot(sampleNormal, -dir) + 0.20f) / 1.20f);
			if (emitterCos <= 0.001f)
				continue;
			float3 L = g_ddgiLitScene.SampleLevel(g_pointSampler, sampleUv, 0).rgb;
			const float lum = dot(L, float3(0.2126f, 0.7152f, 0.0722f));
			if (lum > cap)
				L *= cap / lum;
			const float falloff = falloffR2 / (falloffR2 + dist2);
			accum += L * (receiverCos * emitterCos * falloff);
		}
		return accum * (2.5f / (float)taps);
	}

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		const float2 uv = input.texcoord;
		const float4 albedo = GBUFFER_DIFFUSE.Sample(g_pointSampler, uv);
		const float4 normalDepth = GBUFFER_NORMAL.Sample(g_pointSampler, uv);
		const float4 posWS = GBUFFER_POSITION.Sample(g_pointSampler, uv);
		if (albedo.a == -1.0f || normalDepth.w <= 0.0f || posWS.a > 0.0f)
			return 0.0f.xxxx;
		float3 n = normalDepth.xyz;
		const float nl = dot(n, n);
		if (nl <= 1e-6f)
			return 0.0f.xxxx;
		n *= rsqrt(nl);
		const float eyeDist = max(length(posWS.xyz - g_eyePos.xyz), 1e-3f);
		return float4(DdgiSSGI(uv, posWS.xyz, n), eyeDist);
	}
}
