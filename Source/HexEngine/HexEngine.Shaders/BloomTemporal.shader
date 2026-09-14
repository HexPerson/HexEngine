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
	// Bloom temporal stabiliser. The accumulated chain is rendered into a
	// persistent history RT under BlendState::Transparency with a constant
	// alpha, so the history becomes an exponential moving average without a
	// second texture read:
	//   history = current * a + history * (1 - a),  a = g_bloomPass.z (1-keep)
	// Thin ultra-bright features (neon strips, sun slivers at grazing angles)
	// survive TAA with residual subpixel shimmer; the bloom threshold and the
	// flare knee then amplify that shimmer into visible intermittent pops.
	// Bloom is a low-frequency veil, so a few frames of EMA latency are
	// invisible while the flicker averages away.

	Texture2D g_source : register(t0);
	SamplerState g_linearSampler : register(s4);

	cbuffer BloomPassConstants : register(b6)
	{
		float4 g_bloomPass;
		float4 g_bloomPass2;
	};

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		const float2 uv = input.texcoord;
		const float3 current = g_source.SampleLevel(g_linearSampler, uv, 0).rgb;

		// g_bloomPass.z is the base EMA alpha (1 - keep) blended into the
		// history. The history is NOT reprojected, so a moving - especially
		// rotating - camera drags bright bloom points across the accumulator
		// and leaves firefly trails. Estimate this pixel's screen motion by
		// reprojecting its view ray into the previous frame (rotation-dominant,
		// exactly as the cloud temporal pass does) and push alpha toward 1
		// (all current, no history) as the motion grows. A still camera keeps
		// the full EMA that suppresses the thin-bright-feature flicker this
		// pass exists for.
		float alpha = saturate(g_bloomPass.z);
		const float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
		const float4 farH = mul(float4(ndc, 1.0f, 1.0f), g_viewProjectionMatrixInverse);
		const float3 dir = normalize(farH.xyz / max(farH.w, 1e-6f) - g_eyePos.xyz);
		const float4 prevClip = mul(float4(dir, 0.0f), g_viewProjectionMatrixPrev);
		if (prevClip.w > 1e-4f)
		{
			const float2 prevNdc = prevClip.xy / prevClip.w;
			const float2 prevUv = float2(prevNdc.x * 0.5f + 0.5f, 0.5f - prevNdc.y * 0.5f);
			const float motion = length(prevUv - uv);
			// ~3-4% screen motion per frame fully rejects the history.
			alpha = saturate(alpha + motion * 30.0f);
		}
		return float4(current, alpha);
	}
}
