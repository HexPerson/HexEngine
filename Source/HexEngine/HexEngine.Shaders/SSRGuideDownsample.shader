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

	struct GuideOut
	{
		float4 normal   : SV_Target0;
		float4 material : SV_Target1;
		float4 velocity : SV_Target2;
	};

	GuideOut ShaderMain(UIPixelInput input)
	{
		// Explicit Load of the top-left texel of each 2x2 quad, NOT a point
		// sample: at exactly half resolution, every output texcoord lands on
		// the BOUNDARY between two source texels, and point-sampler rounding
		// at exact boundaries is not guaranteed stable. A guide normal/depth
		// that alternates between neighbouring texels frame to frame makes
		// NRD's reprojection consistency tests disagree with themselves under
		// camera motion - reflections visibly warp while panning.
		const int3 src = int3(int2(input.position.xy) * 2, 0);
		GuideOut o;
		o.normal   = g_srcNormal.Load(src);
		o.material = g_srcMaterial.Load(src);
		o.velocity = g_srcVelocity.Load(src);
		return o;
	}
}
