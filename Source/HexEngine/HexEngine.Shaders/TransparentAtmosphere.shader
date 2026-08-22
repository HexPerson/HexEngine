"Global"
{
	// Per-fragment atmosphere for TRANSPARENT surfaces (glass, water). The
	// froxel fog and aerial-perspective applies run on the opaque frame at the
	// OPAQUE depth, before transparents are drawn; a transparent fragment must
	// therefore fog itself at its own depth, exactly as lit particles do:
	//   colour' = colour * T(d) + inscatter(d)
	// The background it blends over already carries the haze out to the opaque
	// surface behind it, so the inscatter between the fragment and that surface
	// is attributed to the background term.
	//
	// Pulled in with #include by DefaultPixel.shader and Water.shader. The
	// including shader declares the two volumes (t24 froxel integration,
	// t21 aerial perspective) and passes a linear sampler.
	//   g_transparentFogParams: x = froxel volume bound, y = AP volume bound,
	//                           z = froxel far depth (m), w = AP max distance (m)
	float3 ApplyTransparentAtmosphere(float3 colour, float3 positionWS, float2 pixelPos,
		Texture3D fogVolume, Texture3D apVolume, SamplerState linearSampler)
	{
		const float2 screenUv = pixelPos / float2((float)g_screenWidth, (float)g_screenHeight);
		const float viewDepth = max(-mul(float4(positionWS, 1.0f), g_viewMatrix).z, 0.0f);

		// Aerial perspective first (outermost medium), same mapping and near-fade
		// as AtmosphereAerialPerspectiveApply.
		if (g_transparentFogParams.y > 0.5f)
		{
			const float maxDist = max(g_transparentFogParams.w, 1.0f);
			const float w = saturate(viewDepth / maxDist);
			float4 ap = apVolume.SampleLevel(linearSampler, float3(screenUv, w), 0);
			const float nearFade = saturate(viewDepth / (maxDist * (0.5f / 32.0f)));
			ap.rgb *= nearFade;
			ap.a = lerp(1.0f, ap.a, nearFade);
			colour = colour * ap.a + ap.rgb;
		}

		// Froxel fog: exp depth mapping (near 0.1 m) over the volume's far depth,
		// mirroring VolumetricScatterApply / ParticleBillboardLit.
		if (g_transparentFogParams.x > 0.5f)
		{
			const float farDepth = max(g_transparentFogParams.z, 1.0f);
			const float dist = length(positionWS - g_eyePos.xyz);
			const float wz = saturate(log(max(dist, 0.1f) / 0.1f) / log(farDepth / 0.1f));
			const float4 fog = fogVolume.SampleLevel(linearSampler, float3(screenUv, wz), 0);
			colour = colour * fog.a + fog.rgb;
		}

		return colour;
	}
}
