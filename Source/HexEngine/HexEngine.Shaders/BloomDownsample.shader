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
		return output;
	}
}
"PixelShader"
{
	// Phase 4 bloom chain: one hop of the Jimenez/COD 13-tap progressive
	// downsample. The FIRST hop (full-res beauty -> half res) additionally:
	//   - Karis luma-weights each 4-tap quad so a single blazing pixel can't
	//     dominate its neighbourhood (the source of single-pixel bloom
	//     flicker), and
	//   - applies the physical prefilter: scatter = 1 - exp(-luma/ref)
	//     (bright pixels scatter proportionally more in the lens), with an
	//     optional firefly clamp.
	// Subsequent hops are the plain 13-tap.
	//
	// g_bloomPass:  x = 1/srcWidth, y = 1/srcHeight,
	//               z = 1 on the first (prefilter/Karis) hop, w = unused
	// g_bloomPass2: x = unused here, y = unused, zw reserved
	// Prefilter reference luminance + firefly clamp come from g_bloom
	// (luminosityThreshold / bloomClamp - same per-frame fields as before).

	Texture2D g_source : register(t0);
	SamplerState g_linearSampler : register(s4);

	cbuffer BloomPassConstants : register(b6)
	{
		float4 g_bloomPass;
		float4 g_bloomPass2;
	};

	float Luma(float3 c)
	{
		return dot(c, float3(0.2126f, 0.7152f, 0.0722f));
	}

	// s4 is a WRAP sampler in-frame (fixed device-side slots, no sampler
	// API) - clamp UVs manually or the filter leaks across edges.
	float3 SampleSrc(float2 uv)
	{
		const float2 halfTexel = 0.5f * g_bloomPass.xy;
		return g_source.SampleLevel(g_linearSampler, clamp(uv, halfTexel, 1.0f - halfTexel), 0).rgb;
	}

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		const float2 uv = input.texcoord;
		const float2 ts = g_bloomPass.xy;
		const bool firstHop = g_bloomPass.z > 0.5f;

		// 13 taps on the source grid (offsets in source texels).
		const float3 a = SampleSrc(uv + ts * float2(-2.0f, -2.0f));
		const float3 b = SampleSrc(uv + ts * float2( 0.0f, -2.0f));
		const float3 c = SampleSrc(uv + ts * float2( 2.0f, -2.0f));
		const float3 d = SampleSrc(uv + ts * float2(-2.0f,  0.0f));
		const float3 e = SampleSrc(uv);
		const float3 f = SampleSrc(uv + ts * float2( 2.0f,  0.0f));
		const float3 g = SampleSrc(uv + ts * float2(-2.0f,  2.0f));
		const float3 h = SampleSrc(uv + ts * float2( 0.0f,  2.0f));
		const float3 i = SampleSrc(uv + ts * float2( 2.0f,  2.0f));
		const float3 j = SampleSrc(uv + ts * float2(-1.0f, -1.0f));
		const float3 k = SampleSrc(uv + ts * float2( 1.0f, -1.0f));
		const float3 l = SampleSrc(uv + ts * float2(-1.0f,  1.0f));
		const float3 m = SampleSrc(uv + ts * float2( 1.0f,  1.0f));

		// Five overlapping 4-tap boxes: inner quad carries half the energy,
		// the four outer boxes an eighth each - the partial-overlap weighting
		// that kills the pulsing the naive box chain shows under motion.
		const float3 box0 = (a + b + d + e) * 0.25f;
		const float3 box1 = (b + c + e + f) * 0.25f;
		const float3 box2 = (d + e + g + h) * 0.25f;
		const float3 box3 = (e + f + h + i) * 0.25f;
		const float3 box4 = (j + k + l + m) * 0.25f;

		float3 colour;
		if (firstHop)
		{
			// Karis average: weight each box by 1/(1+luma) so fireflies are
			// tamed BEFORE they enter the chain.
			const float w0 = 0.125f / (1.0f + Luma(box0));
			const float w1 = 0.125f / (1.0f + Luma(box1));
			const float w2 = 0.125f / (1.0f + Luma(box2));
			const float w3 = 0.125f / (1.0f + Luma(box3));
			const float w4 = 0.5f   / (1.0f + Luma(box4));
			colour = (box0 * w0 + box1 * w1 + box2 * w2 + box3 * w3 + box4 * w4)
				/ max(w0 + w1 + w2 + w3 + w4, 1e-5f);

			// Physical prefilter (same curve the old BloomPhysical used):
			// fraction of light the lens scatters rises with luminance.
			const float referenceLuma = max(g_bloom.luminosityThreshold, 0.05f);
			const float scatter = 1.0f - exp(-Luma(colour) / referenceLuma);
			colour *= scatter;

			// Optional firefly clamp (0 = off), applied after the curve.
			if (g_bloom.bloomClamp > 0.0f)
				colour = min(colour, g_bloom.bloomClamp.xxx);
		}
		else
		{
			colour = box4 * 0.5f + (box0 + box1 + box2 + box3) * 0.125f;
		}

		return float4(colour, 1.0f);
	}
}
