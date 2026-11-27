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
	RtCommon
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
	// r_rtDebug: trace a primary ray from the eye toward each GBuffer pixel
	// through the compute BVH and compare with what the rasteriser drew.
	//   1 = depth agreement: green = hit within 2 cm of the raster surface,
	//       yellow = within 20 cm, red = wrong / beyond, blue = BVH missed
	//   2 = hit instance (hashed colour)
	//   3 = hit world normal (front faces), magenta = back face
	//   4 = baked triangle albedo, 5 = baked emissive
	// Sky pixels stay black.

	GBUFFER_RESOURCE(0, 1, 2, 3, 4);
	SamplerState g_pointSampler : register(s2);

	cbuffer RtDebugConstants : register(b8)
	{
		float4 g_rtDebugParams; // x: mode
	};

	float3 HashColour(uint v)
	{
		v ^= v >> 16; v *= 0x7feb352du; v ^= v >> 15; v *= 0x846ca68bu; v ^= v >> 16;
		return float3((v & 0xffu), (v >> 8) & 0xffu, (v >> 16) & 0xffu) / 255.0f;
	}

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		const float2 uv = input.texcoord;
		const float4 albedo = GBUFFER_DIFFUSE.Sample(g_pointSampler, uv);
		const float4 normalDepth = GBUFFER_NORMAL.Sample(g_pointSampler, uv);
		const float4 posWS = GBUFFER_POSITION.Sample(g_pointSampler, uv);
		if (albedo.a == -1.0f || normalDepth.w <= 0.0f || posWS.a > 0.0f)
			return float4(0.0f, 0.0f, 0.0f, 1.0f);

		const float3 eye = g_eyePos.xyz;
		const float3 toSurface = posWS.xyz - eye;
		const float rasterDist = length(toSurface);
		const float3 dir = toSurface / max(rasterDist, 1e-4f);
		const RtHit hit = RtTrace(eye, dir, rasterDist + 5.0f, false);
		const uint mode = (uint)g_rtDebugParams.x;

		if (hit.instance == 0xffffffffu)
			return float4(0.0f, 0.0f, 1.0f, 1.0f);

		if (mode == 1u)
		{
			const float err = abs(hit.t - rasterDist);
			const float3 c = err < 0.02f ? float3(0.0f, 1.0f, 0.0f) : (err < 0.2f ? float3(1.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f));
			return float4(c * (0.35f + 0.65f * saturate(dot(normalize(normalDepth.xyz), -dir))), 1.0f);
		}
		if (mode == 2u)
			return float4(HashColour(hit.instance + 1u), 1.0f);

		const RtSurface s = RtGetSurface(hit, eye, dir);
		if (mode == 3u)
			return s.frontFace ? float4(s.normal * 0.5f + 0.5f, 1.0f) : float4(1.0f, 0.0f, 1.0f, 1.0f);
		if (mode == 4u)
			return float4(s.albedo, 1.0f);
		return float4(s.emissive, 1.0f);
	}
}
