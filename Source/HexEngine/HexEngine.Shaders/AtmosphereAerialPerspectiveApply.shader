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
	AtmosphereCommon
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
	// Apply pass for the aerial-perspective froxel volume produced by
	// AtmosphereAerialPerspectiveLUT.shader. Reads beauty + gbuffer
	// view-space depth, samples the 3D AP volume at the pixel's froxel,
	// composites:
	//   final = beauty * transmittance + inscatter
	// Skips sky pixels (their colour IS the atmospheric scattering -
	// applying AP on top would double-haze the sky).
	//
	// Run as a fullscreen quad after SubsurfaceScattering and before
	// transparency / fog so lit opaque geometry gets distance haze, and
	// the existing artistic fog stack still composes on top.

	GBUFFER_RESOURCE(0, 1, 2, 3, 4);
	Texture2D    g_beauty                : register(t5);
	Texture3D    g_aerialPerspectiveLUT  : register(t6);
	Texture2D    g_atmSkyViewLUT         : register(t7);
	SamplerState g_pointSampler          : register(s2);
	SamplerState g_linearSampler         : register(s4);

	static const float MAX_DIST_M       = 100000.0f;
	// Distance at which we force the AP composite to fully match the sky
	// LUT colour in the view direction. Beyond this the geometry pixel
	// should be visually indistinguishable from the sky behind it.
	// Clear-air physics alone doesn't attenuate a 10 km mountain enough
	// to produce that silhouette-fade in real life either, but production
	// engines (UE5 SkyAtmosphere, Frostbite, Horizon) all explicitly
	// blend toward the sky LUT at far distance to sell the look.
	static const float SKY_MATCH_DIST_M = 10000.0f;

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		const float2 uv = input.texcoord;

		const float4 beauty = g_beauty.Sample(g_pointSampler, uv);
		const float4 diff   = GBUFFER_DIFFUSE.Sample(g_pointSampler, uv);
		const float4 nd     = GBUFFER_NORMAL.Sample(g_pointSampler, uv);

		// Sky / no-geometry guard. SkySphere writes diff.a == -1 and
		// nd.w == frustum-far. Either signals "this pixel IS the sky"
		// and shouldn't get a second layer of AP on top of the LUT.
		const bool skyPixel = (diff.a < -0.5f) || (nd.w <= 0.0f);
		if (skyPixel)
			return beauty;

		// View-space depth is packed in normal.w (positive forward, metres).
		const float depthVS = nd.w;

		// Volume W axis is linear distance in [0, MAX_DIST_M]. Pixels past
		// the AP range take the far-most slice (which is the deepest
		// integration result) so distant geometry receives full atmospheric
		// fade rather than abruptly stopping at 32 km.
		const float w = saturate(depthVS / MAX_DIST_M);

		// Sample the volume with linear filter for cross-froxel smoothing.
		float4 ap = g_aerialPerspectiveLUT.SampleLevel(g_linearSampler, float3(uv, w), 0);

		// Fade AP toward identity (transmittance=1, inscatter=0) for pixels
		// closer than the first froxel's depth (~500 m, since the volume
		// has 32 slices over 32 km). Without this, every pixel within
		// 500 m samples texel 0 with CLAMP - meaning a 5 m wall and a
		// 500 m wall both get the same "500 m of atmospheric haze" tint.
		// The first-slice inscatter is dominated by multi-scattering at
		// low altitude which is non-trivial, hence the visible blue cast
		// on the foreground. apNearFade ramps in linearly from camera to
		// the first-slice depth.
		const float FIRST_SLICE_DEPTH_M = MAX_DIST_M * (0.5f / 32.0f);
		const float apNearFade = saturate(depthVS / FIRST_SLICE_DEPTH_M);
		ap.rgb *= apNearFade;
		ap.a    = lerp(1.0f, ap.a, apNearFade);

		// Volume-only composite: beauty * t + I.
		const float3 volumeFinal = beauty.rgb * ap.a + ap.rgb;

		// Sky LUT match for far distances. Reconstruct the view direction
		// from the gbuffer's world position so we can sample the SkyView
		// LUT at the exact ray the pixel was rendered from. Then lerp
		// the volume composite toward the sky colour proportional to
		// distance - at SKY_MATCH_DIST_M the pixel reads as pure sky,
		// guaranteeing the geometry silhouette dissolves into the sky
		// background.
		const float3 pixelWorld = GBUFFER_POSITION.Sample(g_pointSampler, uv).xyz;
		const float3 viewDir = normalize(pixelWorld - g_eyePos.xyz);
		const float3 sunDir  = normalize(-g_lightDirection.xyz);
		const float2 skyUv   = SkyViewLutParamsToUv(viewDir, sunDir);
		const float3 skyLutColour = g_atmSkyViewLUT.SampleLevel(g_linearSampler, skyUv, 0).rgb;

		const float skyMatchT = pow(saturate(depthVS / SKY_MATCH_DIST_M), 1.6f);
		const float3 final = lerp(volumeFinal, skyLutColour, skyMatchT);

		return float4(final, beauty.a);
	}
}
