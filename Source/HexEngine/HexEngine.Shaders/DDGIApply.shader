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
	EnvMapCommon
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
	// DDGI apply: indirect diffuse = albedo * (1 - metallic) * probe
	// irradiance, added onto the lit beauty target (DDGI.cpp sets additive
	// blend; debug views draw opaque). The deferred pass hands off ALL flat
	// ambient and the sky SH fill while DDGI is active - the probes already
	// contain the sky through their ray misses.

	GBUFFER_RESOURCE(0, 1, 2, 3, 4);
	// Half-res near-field bounce from DDGISSGI (rgb radiance, a = eye
	// distance of the gathering pixel; 0 = none). Bound only while SSGI is on.
	Texture2D g_ddgiSSGIHalf : register(t6);
	// Screen-space AO (HBAO+, white = open) for the probe term; bound only
	// while g_ddgiGround.z (strength) > 0.
	Texture2D g_ddgiAO : register(t7);
	// Sky SH (same texture and scale the probe trace uses for ray misses):
	// the open-sky fallback beyond the probe volume.
	Texture2D g_ddgiApplySkySH : register(t65);
	SamplerState g_pointSampler : register(s2);

	// Bilateral upsample: the 4 nearest half-res texels, bilinear weights
	// times an eye-distance similarity term so bounce does not smear across
	// depth edges (a sun-lit floor must not bleed onto the window frame in
	// front of it).
	float3 DdgiSSGIUpsample(float2 uv, float centerDist)
	{
		uint hw, hh;
		g_ddgiSSGIHalf.GetDimensions(hw, hh);
		const float2 hp = uv * float2((float)hw, (float)hh) - 0.5f;
		const int2 base = (int2)floor(hp);
		const float2 f = hp - floor(hp);
		const int2 maxCoord = int2((int)hw - 1, (int)hh - 1);
		const float tolerance = 0.03f * centerDist + 0.05f;
		float3 sum = 0.0f.xxx;
		float wsum = 0.0f;
		[unroll]
		for (int k = 0; k < 4; ++k)
		{
			const int2 o = int2(k & 1, k >> 1);
			const float4 s = g_ddgiSSGIHalf.Load(int3(clamp(base + o, int2(0, 0), maxCoord), 0));
			if (s.a <= 0.0f)
				continue;
			const float bw = (o.x != 0 ? f.x : 1.0f - f.x) * (o.y != 0 ? f.y : 1.0f - f.y);
			const float dz = (s.a - centerDist) / tolerance;
			const float w = (bw + 1e-3f) * exp(-dz * dz);
			sum += s.rgb * w;
			wsum += w;
		}
		return wsum > 1e-4f ? sum / wsum : 0.0f.xxx;
	}

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		const float2 uv = input.texcoord;
		const float4 albedo = GBUFFER_DIFFUSE.Sample(g_pointSampler, uv);
		const float4 normalDepth = GBUFFER_NORMAL.Sample(g_pointSampler, uv);
		const float4 posWS = GBUFFER_POSITION.Sample(g_pointSampler, uv);
		const float debugMode = g_ddgiParams2.z;

		const bool sky = albedo.a == -1.0f || normalDepth.w <= 0.0f || posWS.a > 0.0f;
		if (sky)
			return float4(0.0f, 0.0f, 0.0f, debugMode > 0.5f ? 1.0f : 0.0f);

		float3 n = normalDepth.xyz;
		const float nl = dot(n, n);
		if (nl <= 1e-6f)
			return 0.0f.xxxx;
		n *= rsqrt(nl);
		const float3 toEye = normalize(g_eyePos.xyz - posWS.xyz);

		// Stochastic probe interpolation. Plain trilinear between probes 1 m
		// apart turns real variation (probes nearer an opening see more sky
		// and sunlit ground) into soft-edged boxes on flat walls - the
		// "poster paint" lumps. Sampling at a point jittered within the
		// surface's own plane (so it stays on the same side of the wall), by
		// up to g_ddgiParams4.x of the covering cascade's spacing and rotated
		// every frame, convolves that blend with a disc once TAA resolves it:
		// a smooth falloff instead of a box, at no extra probe lookups.
		float3 samplePos = posWS.xyz;
		float jitterAmount = g_ddgiParams4.x;
		// Near corners and creases the in-plane jitter can carry the lookup
		// past the perpendicular wall into probes on the other side (sky
		// light leaking indoors). The screen-space AO marks exactly those
		// places (open = 1), so the jitter fades out there and keeps working
		// on open walls and floors, where the lumps show.
		if (jitterAmount > 0.0001f && g_ddgiGround.z > 0.0001f)
		{
			const float aoOpen = saturate(g_ddgiAO.SampleLevel(g_pointSampler, uv, 0).r);
			jitterAmount *= smoothstep(0.80f, 0.97f, aoOpen);
		}
		if (jitterAmount > 0.0001f)
		{
			float spacingJ = g_ddgiCascade[2].spacing.x;
			if (DdgiCascadeInset(0, posWS.xyz) >= 0.0f) spacingJ = g_ddgiCascade[0].spacing.x;
			else if (DdgiCascadeInset(1, posWS.xyz) >= 0.0f) spacingJ = g_ddgiCascade[1].spacing.x;
			const float3 tangent = normalize(abs(n.y) < 0.95f ? cross(n, float3(0.0f, 1.0f, 0.0f)) : cross(n, float3(1.0f, 0.0f, 0.0f)));
			const float3 bitangent = cross(n, tangent);
			// Interleaved gradient noise per pixel, advanced per frame by
			// golden-ratio steps (well spread for TAA to integrate).
			const float2 px = uv * float2((float)g_screenWidth, (float)g_screenHeight);
			const float ign = frac(52.9829189f * frac(dot(px, float2(0.06711056f, 0.00583715f))));
			const float ign2 = frac(52.9829189f * frac(dot(px + 17.0f, float2(0.00583715f, 0.06711056f))));
			const float frame = (float)(g_frame % 64u);
			const float angle = 6.2831853f * frac(ign + frame * 0.6180340f);
			const float radius = sqrt(frac(ign2 + frame * 0.7548777f)) * jitterAmount * spacingJ;
			samplePos += (tangent * cos(angle) + bitangent * sin(angle)) * radius;
		}
		// Beyond the outermost cascade the probes end; blend to what an
		// unoccluded probe would report there - the sky term the trace uses
		// for its ray misses - across the cascade's outer band.
		const float3 skyFallback = DdgiSkyTint(max(ShIrradiance(g_ddgiApplySkySH, g_pointSampler, n), 0.0f.xxx)) * g_ddgiParams1.x;
		float coverage;
		float3 irradiance = DdgiSampleDirectionalCov(samplePos, n, toEye, n, coverage).rgb;
		irradiance = lerp(skyFallback, irradiance, coverage);
		if (jitterAmount > 0.0001f)
		{
			// Leak guard: the jittered lookup may only reshape the local
			// value, not replace it. Where it crossed an opening or a wall
			// edge the AO does not mark (door frames, window reveals), the
			// far side's light is usually much brighter (sky) and gets
			// clamped; the moderate probe-to-probe variation that makes the
			// lumps stays inside the band and is smoothed.
			float coverageU;
			float3 unjittered = DdgiSampleDirectionalCov(posWS.xyz, n, toEye, n, coverageU).rgb;
			unjittered = lerp(skyFallback, unjittered, coverageU);
			irradiance = clamp(irradiance, unjittered * 0.6f, unjittered * 1.6f + 1e-4f);
		}
		// Contact AO on the probe term only: the probes cannot resolve it,
		// while SSGI below already gathers from the actual nearby surfaces.
		if (g_ddgiGround.z > 0.0001f)
			irradiance *= lerp(1.0f, saturate(g_ddgiAO.SampleLevel(g_pointSampler, uv, 0).r), saturate(g_ddgiGround.z));
		if (g_ddgiSSGI.x > 0.0001f)
			irradiance += DdgiSSGIUpsample(uv, max(length(posWS.xyz - g_eyePos.xyz), 1e-3f)) * g_ddgiSSGI.x;
		if (debugMode > 1.5f)
			return float4(irradiance, 1.0f);

		const float metallic = saturate(GBUFFER_SPECULAR.Sample(g_pointSampler, uv).r);
		const float3 indirect = albedo.rgb * (1.0f - metallic) * irradiance * g_ddgiParams2.y;
		return float4(max(indirect, 0.0f.xxx), debugMode > 0.5f ? 1.0f : 0.0f);
	}
}
