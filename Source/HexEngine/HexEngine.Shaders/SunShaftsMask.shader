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
	// Sun shaft occlusion mask (RDR2 sky S7), half res.
	//
	// Seeds radiance where the SKY is visible (gbuffer diffuse .a == -1),
	// attenuated by the cloud alpha at that pixel (full-res cloud composite
	// still sitting in _fogBuffer when this runs) and windowed to a cone
	// around the sun. Geometry pixels seed ZERO - the radial blur then
	// smears the bright sky through the dark silhouettes, which is what
	// draws the ray structure (crepuscular rays ARE the shadows of cloud
	// edges and geometry cast through haze).

	GBUFFER_RESOURCE(0, 1, 2, 3, 4);
	Texture2D g_cloudComposite : register(t5);

	SamplerState g_pointSampler : register(s2);
	SamplerState g_linearSampler : register(s4);

	cbuffer SunShaftParams : register(b6)
	{
		float4 g_shaftP0; // xy = sun screen uv, z = blur length, w = off-screen fade
		float4 g_shaftP1; // x = intensity, y = pass index, z/w = reserved
	};

	float3 GetWorldRayDir(float2 uv)
	{
		float4 clipPos = float4(uv * 2.0f - 1.0f, 1.0f, 1.0f);
		clipPos.y *= -1.0f;
		float4 worldPos = mul(clipPos, g_viewProjectionMatrixInverse);
		worldPos.xyz /= max(1e-5f, worldPos.w);
		return normalize(worldPos.xyz - g_eyePos.xyz);
	}

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		const float2 uv = input.texcoord;

		// Sky test - same marker every other pass uses. Geometry = no seed.
		const float skyFlag = GBUFFER_DIFFUSE.Sample(g_pointSampler, uv).a;
		if (skyFlag != -1.0f)
			return 0.0f.xxxx;

		// Cloud occlusion: dense cloud blocks the seed, thin cloud dims it.
		const float cloudAlpha = saturate(g_cloudComposite.Sample(g_linearSampler, uv).a);

		// Cone window around the sun: shafts radiate from the sun's
		// neighbourhood; seeding the whole sky just adds a global haze and
		// washes the ray contrast out.
		const float3 rayDir = GetWorldRayDir(uv);
		const float3 sunDir = normalize(-g_lightDirection.xyz + 1e-5f.xxx);
		const float mu = dot(rayDir, sunDir);
		const float cone = smoothstep(0.72f, 0.97f, mu);

		const float seed = cone * (1.0f - cloudAlpha);
		return float4(seed.xxx, 1.0f);
	}
}
