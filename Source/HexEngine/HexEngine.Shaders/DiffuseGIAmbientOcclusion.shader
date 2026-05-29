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
	// Reads the resolved GI texture's alpha (accumulated voxel occlusion from
	// the cone trace + temporal accumulation done in DiffuseGITrace.shader and
	// DiffuseGIResolve.shader) and outputs a per-pixel AO multiplier in [0, 1].
	// The provider draws this fullscreen with BlendState::Multiplicative so the
	// existing beauty RT gets dst = dst * src. Source is greyscale AO, so RGB
	// channels all carry the same multiplier; alpha is left at 1 to avoid
	// disturbing whatever the beauty RT's alpha is being used for downstream.
	Texture2D g_giResolved : register(t0);
	SamplerState g_pointSampler : register(s2);

	// Cbuffer at b4 - DecalConstants / GIConstants slot is free during this
	// post-process. Single float4: (intensity, contrast, _, _).
	cbuffer GiAOConstants : register(b4)
	{
		float4 g_giAoParams; // x = intensity, y = contrast, z/w reserved
	};

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		// Voxel occlusion was packed into .a by DiffuseGITrace.shader. Higher
		// alpha = more occlusion (closer to 1 = fully occluded).
		const float occlusion = saturate(g_giResolved.Sample(g_pointSampler, input.texcoord).a);

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
