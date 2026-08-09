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
	// Phase 4 bloom chain: one hop of the progressive tent upsample. Drawn
	// ADDITIVELY into the next-larger chain level (BlendState::Additive set
	// by the C++ side), so the accumulated result at the chain top is the
	// sum of every level's tent-filtered contribution - the wide, smooth
	// falloff the single hard-cut gaussian could never produce.
	//
	// g_bloomPass: x = 1/srcWidth, y = 1/srcHeight (of the SMALLER source
	//              level being upsampled), w = radius scale (r_bloomRadius).

	Texture2D g_source : register(t0);
	SamplerState g_linearSampler : register(s4);

	cbuffer BloomPassConstants : register(b6)
	{
		float4 g_bloomPass;
		float4 g_bloomPass2;
	};

	// s4 wraps in-frame - clamp manually.
	float3 SampleSrc(float2 uv)
	{
		const float2 halfTexel = 0.5f * g_bloomPass.xy;
		return g_source.SampleLevel(g_linearSampler, clamp(uv, halfTexel, 1.0f - halfTexel), 0).rgb;
	}

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		const float2 uv = input.texcoord;
		const float2 ts = g_bloomPass.xy * max(g_bloomPass.w, 0.01f);

		// 3x3 tent (1-2-1 weights, /16).
		float3 colour;
		colour  = SampleSrc(uv + ts * float2(-1.0f, -1.0f)) * 1.0f;
		colour += SampleSrc(uv + ts * float2( 0.0f, -1.0f)) * 2.0f;
		colour += SampleSrc(uv + ts * float2( 1.0f, -1.0f)) * 1.0f;
		colour += SampleSrc(uv + ts * float2(-1.0f,  0.0f)) * 2.0f;
		colour += SampleSrc(uv)                              * 4.0f;
		colour += SampleSrc(uv + ts * float2( 1.0f,  0.0f)) * 2.0f;
		colour += SampleSrc(uv + ts * float2(-1.0f,  1.0f)) * 1.0f;
		colour += SampleSrc(uv + ts * float2( 0.0f,  1.0f)) * 2.0f;
		colour += SampleSrc(uv + ts * float2( 1.0f,  1.0f)) * 1.0f;
		colour /= 16.0f;

		return float4(colour, 1.0f);
	}
}
