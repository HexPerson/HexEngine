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
	// Phase 4 bloom chain: energy-normalised composite. Replaces the old
	// BlendTo_Additive full-quad (which added the unnormalised gaussian sum
	// on top of the undiminished scene - created energy, and made the
	// intensity cvar resolution/radius dependent).
	//
	// The accumulated chain top holds the SUM of N tent-upsampled levels, so
	// dividing by N (g_bloomPass2.x = 1/levelCount) makes it an average and
	// g_bloom.bloomIntensity means the same thing at any chain length:
	// "fraction of light the lens scatters" - Cyberpunk-restrained values
	// live around 0.03-0.07.
	//
	// t0 = scene HDR, t1 = bloom chain top (half res, upscaled bilinearly).

	Texture2D g_scene : register(t0);
	Texture2D g_bloomChain : register(t1);
	SamplerState g_pointSampler : register(s2);
	SamplerState g_linearSampler : register(s4);

	cbuffer BloomPassConstants : register(b6)
	{
		float4 g_bloomPass;
		float4 g_bloomPass2;
	};

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		const float2 uv = input.texcoord;

		const float4 scene = g_scene.SampleLevel(g_pointSampler, uv, 0);

		// s4 wraps in-frame - clamp against the CHAIN-TOP texel size (x/y).
		const float2 halfTexel = 0.5f * g_bloomPass.xy;
		const float3 bloom = g_bloomChain.SampleLevel(
			g_linearSampler, clamp(uv, halfTexel, 1.0f - halfTexel), 0).rgb;

		const float normalised = max(g_bloomPass2.x, 0.0f); // 1/levelCount
		const float3 result = scene.rgb + bloom * normalised * g_bloom.bloomIntensity;

		return float4(result, scene.a);
	}
}
