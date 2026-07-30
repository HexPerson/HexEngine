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
	// NRD guide decimation for half-res SSR (r_ssrHalfRes).
	//
	// NRD requires its guide textures (normal+depth, material, motion
	// vectors) at the SAME resolution as the radiance being denoised. When
	// the SSR march runs at half resolution, this pass point-samples the
	// full-res gbuffer down to the SSR size - nearest-texel decimation, no
	// filtering: normals must stay unit-length, depth must stay a real
	// surface depth, and velocity must stay a real pixel's motion. Averaging
	// any of them across a depth edge would hand NRD a guide describing a
	// surface that does not exist.

	Texture2D g_srcNormal   : register(t0);
	Texture2D g_srcMaterial : register(t1);
	Texture2D g_srcVelocity : register(t2);

	SamplerState g_pointSampler : register(s2);

	struct GuideOut
	{
		float4 normal   : SV_Target0;
		float4 material : SV_Target1;
		float4 velocity : SV_Target2;
	};

	GuideOut ShaderMain(UIPixelInput input)
	{
		GuideOut o;
		o.normal   = g_srcNormal.SampleLevel(g_pointSampler, input.texcoord, 0);
		o.material = g_srcMaterial.SampleLevel(g_pointSampler, input.texcoord, 0);
		o.velocity = g_srcVelocity.SampleLevel(g_pointSampler, input.texcoord, 0);
		return o;
	}
}
