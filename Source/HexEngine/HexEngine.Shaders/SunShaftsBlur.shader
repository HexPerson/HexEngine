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
	AtmosphereCommon
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
	// Sun shaft radial blur + composite (RDR2 sky S7). Run twice:
	//   pass 1 (g_shaftP1.y == 1): half-res A -> B, short reach - builds the
	//     local ray structure out of the mask.
	//   pass 2 (g_shaftP1.y == 2): B -> beauty, ADDITIVE, full reach -
	//     stretches the structure across the screen and colours it with the
	//     transmittance-LUT sun colour (orange shafts at sunset for free).
	// Two chained passes give taps^2 effective samples along the ray.

	Texture2D g_source : register(t0);
	Texture2D g_atmTransmittanceLUT : register(t1); // pass 2 only (null-safe)

	SamplerState g_linearSampler : register(s4);

	cbuffer SunShaftParams : register(b6)
	{
		float4 g_shaftP0; // xy = sun screen uv, z = blur length, w = off-screen fade
		float4 g_shaftP1; // x = intensity, y = pass index, z = high-sun scale, w = reserved
	};

	static const int kTaps = 20;

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		const float2 uv = input.texcoord;
		const float2 sunUv = g_shaftP0.xy;
		const bool composite = g_shaftP1.y > 1.5f;

		// Reach: pass 1 builds short local streaks, pass 2 extends them the
		// full authored length toward the sun.
		const float reach = g_shaftP0.z * (composite ? 1.0f : 0.28f);
		const float2 toSun = (sunUv - uv) * reach;
		const float2 stepUv = toSun / (float)kTaps;

		// The engine's s4 sampler WRAPS - clamp every tap inside the target
		// or streaks wrap round the screen edge.
		const float2 uvMin = 0.001f.xx;
		const float2 uvMax = 0.999f.xx;

		float3 accum = 0.0f.xxx;
		float weightSum = 0.0f;
		[unroll]
		for (int i = 0; i < kTaps; ++i)
		{
			const float t = ((float)i + 0.5f) / (float)kTaps;
			// Exponential decay along the march: samples near the pixel
			// dominate, the far end feathers out.
			const float w = exp(-2.6f * t);
			const float2 tapUv = clamp(uv + stepUv * (float)i, uvMin, uvMax);
			accum += g_source.SampleLevel(g_linearSampler, tapUv, 0).rgb * w;
			weightSum += w;
		}
		float3 shaft = accum / max(weightSum, 1e-4f);

		if (!composite)
			return float4(shaft, 1.0f);

		// ---- Composite pass: colour + intensity, written additively. ----
		const float3 sunDir = normalize(-g_lightDirection.xyz + 1e-5f.xxx);
		const float cameraAltMM = WorldYToAtmosphereAltitudeMM(g_eyePos.y);
		const float2 tuv = TransmittanceLutParamsToUv(cameraAltMM, sunDir.y);
		float3 sunTrans = g_atmTransmittanceLUT.SampleLevel(g_linearSampler, tuv, 0).rgb;
		// LUT unbound / subsystem off reads 0 - fall back to a warm white so
		// the shafts stay visible on the analytic path.
		if (dot(sunTrans, 1.0f.xxx) < 1e-4f)
			sunTrans = float3(1.0f, 0.82f, 0.62f);
		const float sunEnergy = lerp(18.0f, 30.0f, saturate(sunDir.y * 0.5f + 0.5f)) * max(g_globalLight[0], 0.0f);

		// 0.02 base scale: the shaft term is a blurred 0..1 mask; scaled
		// against the LUT-energy sun colour it must stay a modest additive
		// glow, not a second sun. r_sunShaftsIntensity is the artist lever.
		const float3 colour = sunTrans * float3(1.0f, 0.985f, 0.965f) * sunEnergy * 0.02f;
		// Elevation fade: the LUT sun is ~4x brighter and white overhead, so
		// the golden-hour scale is a hot wash at noon. Blend from full at
		// ~6 deg to r_sunShaftsHighSunScale by ~30 deg (a zero cbuffer slot
		// - shader ahead of the C++ build - falls back to the default 0.22).
		const float highSunScale = (g_shaftP1.z > 0.0f) ? g_shaftP1.z : 0.22f;
		const float elevScale = lerp(1.0f, highSunScale, smoothstep(0.10f, 0.50f, sunDir.y));
		return float4(shaft * colour * (g_shaftP1.x * g_shaftP0.w * elevScale), 1.0f);
	}
}
