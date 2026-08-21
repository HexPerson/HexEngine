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

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		// Volume far plane: camera-far derived, shared with the LUT generator
		// via AtmosphereApMaxDistM() in AtmosphereCommon (the two must agree
		// exactly or every pixel samples the wrong slice).
		const float maxDistM = AtmosphereApMaxDistM();
		// Distance at which we force the AP composite to fully match the sky
		// LUT colour in the view direction. Beyond this the geometry pixel
		// should be visually indistinguishable from the sky behind it.
		// Clear-air physics alone doesn't attenuate distant geometry enough
		// to produce that silhouette-fade in real life either, but production
		// engines (UE5 SkyAtmosphere, Frostbite, Horizon) all explicitly
		// blend toward the sky LUT at far distance to sell the look. Tied to
		// the camera far plane - geometry stops existing there, so completing
		// the dissolve exactly at that distance hides far-plane pop-in
		// regardless of scene scale (the old hardcoded 10 km made mid-ground
		// buildings dissolve in scenes with a short far plane, and was never
		// reached at all in scenes with a shorter one).
		const float skyMatchDistM = maxDistM;

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

		// Volume W axis is linear distance in [0, maxDistM]. Pixels past
		// the AP range take the far-most slice (which is the deepest
		// integration result) so distant geometry receives full atmospheric
		// fade rather than abruptly stopping at the volume far plane.
		const float w = saturate(depthVS / maxDistM);

		// Sample the volume with linear filter for cross-froxel smoothing.
		float4 ap = g_aerialPerspectiveLUT.SampleLevel(g_linearSampler, float3(uv, w), 0);

		// Fade AP toward identity (transmittance=1, inscatter=0) for pixels
		// closer than the first froxel's depth (half a slice of the 32-slice
		// volume - with the camera-far-derived range this is farZ/64, e.g.
		// ~31 m at a 2 km far plane instead of the old fixed 500 m). Without
		// this, every pixel nearer than the first slice samples texel 0 with
		// CLAMP - meaning a 5 m wall and a first-slice-distance wall both get
		// the same slice-0 haze tint. The first-slice inscatter is dominated
		// by multi-scattering at low altitude which is non-trivial, hence the
		// visible blue cast on the foreground. apNearFade ramps in linearly
		// from camera to the first-slice depth.
		const float firstSliceDepthM = maxDistM * (0.5f / 32.0f);
		const float apNearFade = saturate(depthVS / firstSliceDepthM);
		ap.rgb *= apNearFade;
		ap.a    = lerp(1.0f, ap.a, apNearFade);

		// Volume-only composite: beauty * t + I.
		const float3 volumeFinal = beauty.rgb * ap.a + ap.rgb;

		// Sky LUT match near the far plane. Reconstruct the view direction
		// from the gbuffer's world position so we can sample the SkyView
		// LUT at the ray the pixel was rendered from, then dissolve the
		// volume composite toward that sky colour as the pixel approaches
		// skyMatchDistM (the camera far plane), so the silhouette melts into
		// the sky instead of popping.
		const float3 pixelWorld = GBUFFER_POSITION.Sample(g_pointSampler, uv).xyz;
		const float3 viewDir = normalize(pixelWorld - g_eyePos.xyz);
		const float3 sunDir  = normalize(-g_lightDirection.xyz);
		// Sample the sky AT OR ABOVE the horizon. A ray to a distant mountain's
		// lower slopes points below the eye horizon, where the sky-view LUT
		// holds its ground/ocean colour - matching toward that painted the sea
		// horizon line straight through the mountain. The dissolve target must
		// always be sky; horizon sky is the closest physically sensible colour
		// for a below-horizon ray that ends on geometry.
		float3 matchDir = viewDir;
		matchDir.y = max(matchDir.y, 0.02f);
		matchDir = normalize(matchDir);
		const float2 skyUv   = SkyViewLutParamsToUv(matchDir, sunDir);
		const float3 skyLutColour = g_atmSkyViewLUT.SampleLevel(g_linearSampler, skyUv, 0).rgb;

		// Far-plane DISSOLVE, not haze: the volume composite above already
		// carries the physical distance haze (inscatter + transmittance
		// integrated along the ray, no horizon seam). The explicit sky match
		// exists only to hide geometry popping at the far plane, so it engages
		// over the last stretch before it - mid-range objects keep their own
		// shading instead of going translucent to the sky behind them.
		const float skyMatchT = smoothstep(0.80f, 1.0f, depthVS / skyMatchDistM);
		const float3 final = lerp(volumeFinal, skyLutColour, skyMatchT);

		return float4(final, beauty.a);
	}
}
