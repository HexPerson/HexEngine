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
	// Reads a single-channel AO buffer (DiffuseGI::_giAoBlurred - the wide
	// bilateral-blurred voxel occlusion from DiffuseGIAOBlur.shader) and
	// outputs a per-pixel AO multiplier in [0, 1]. The provider draws this
	// fullscreen with BlendState::Multiplicative so the existing beauty RT
	// gets dst = dst * src. Source is greyscale AO, so RGB channels all
	// carry the same multiplier; alpha is left at 1 to avoid disturbing
	// whatever the beauty RT's alpha is being used for downstream.
	//
	// The raw cone-trace AO (_giResolved.a) had per-voxel grid artifacts
	// when applied at screen resolution. The bilateral blur smooths the
	// signal across flat surfaces while preserving geometric edges via
	// depth/normal weighting. The R8 blurred target lands in .r here.
	GBUFFER_RESOURCE(0, 1, 2, 3, 4);
	Texture2D g_giAo : register(t5);
	SamplerState g_pointSampler : register(s2);

	// Cbuffer at b4 - DecalConstants / GIConstants slot is free during this
	// post-process. Single float4: (intensity, contrast, _, _).
	cbuffer GiAOConstants : register(b4)
	{
		float4 g_giAoParams; // x = intensity, y = contrast, z/w reserved
	};

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		// Sky / no-geometry guard. The GI trace shader emits alpha=0 for sky
		// pixels (no occlusion) but be defensive in case the resolve / temporal
		// path bleeds something else in: also check the gbuffer signals that
		// the rest of the deferred pipeline uses to identify the sky. Returning
		// 1.0 multiplier here leaves the beauty RT unchanged for sky pixels -
		// without this we silently multiplied the sky toward black whenever
		// the GI's alpha drifted above 0.
		const float4 diff = GBUFFER_DIFFUSE.Sample(g_pointSampler, input.texcoord);
		const float4 nd   = GBUFFER_NORMAL.Sample(g_pointSampler, input.texcoord);
		const bool skyPixel = (diff.a == -1.0f) || (nd.w <= 0.0f);
		if (skyPixel)
			return float4(1.0f, 1.0f, 1.0f, 1.0f);

		// Blurred occlusion is in the .r channel of the bound single-channel
		// R8 target. Higher value = more occlusion (closer to 1 = fully occluded).
		const float occlusion = saturate(g_giAo.Sample(g_pointSampler, input.texcoord).r);

		// Optional contrast boost - voxel cone tracing tends to produce gentle
		// occlusion at the medium scale (walls, eaves) and miss small crevices.
		// Pushing contrast slightly tightens the look without raising the
		// average too far.
		const float contrastedOcc = saturate(pow(occlusion, max(g_giAoParams.y, 0.001f)));

		// Convert to a multiplier in [1 - intensity, 1.0]. intensity 0 = no AO
		// (all white), intensity 1 = full AO (down to 0 in fully-occluded
		// pixels). Same convention as HBAOPlus's blend output.
		const float aoMul = saturate(1.0f - contrastedOcc * saturate(g_giAoParams.x));

		// Multiplicative blend will compute dst = dst * src so this RGB IS the
		// AO multiplier. Alpha = 1 means leave the existing alpha intact.
		return float4(aoMul.xxx, 1.0f);
	}
}
