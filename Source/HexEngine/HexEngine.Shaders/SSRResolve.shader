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
	// Octahedral environment atlas + the shared EvaluateEnvSpecular that the
	// deferred lighting pass calls with the same arguments.
	EnvMapCommon
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
	// SSR composition pass.
	//
	// Screen-space reflection and image-based lighting are two estimates of the
	// SAME quantity - the radiance arriving along the reflection vector. They have
	// to be COMPOSED, and this is the only pass where both exist at once.
	//
	// Before this, the deferred pass added environment specular and then this
	// resolve blended the screen-space reflection additively on top. That stacked
	// them: where a ray hit, the pixel got environment + screen and was
	// double-bright; where a ray missed - a floor ray aimed at a window pane finds
	// nothing, because transparent glass never writes the opaque gbuffer - the
	// SSR term was black and the pane rendered black in the floor's reflection.
	//
	// Now the deferred pass leaves specular alone (g_iblComposeInResolve) and this
	// pass writes lerp(environment, screen, confidence). Screen data wins where it
	// exists; the environment fills every miss. Expressed additively, since the
	// draw blends onto beauty:
	//
	//     beauty += ssr + environment * (1 - confidence)
	//
	// which is the lerp, because SSR's specular output is already ~zero wherever
	// its confidence is zero (the miss path returns nothing so this pass can fill
	// it - see SSR.shader).
	GBUFFER_RESOURCE(0, 1, 2, 3, 4);

	// The SSR signal to composite. Two sources so both paths through
	// SceneRenderer::RenderSSR are one draw:
	//   denoised     - A = NRD's resolved diffuse+specular, B = null (black)
	//   not denoised - A = raw SSR diffuse, B = raw SSR specular
	// A null bind reads as black, so the unused source contributes nothing.
	Texture2D g_ssrSourceA : register(t5);
	Texture2D g_ssrSourceB : register(t6);

	// Specular hit info straight from the SSR pass (NOT denoised): .r is the
	// fraction of this pixel's specular rays that found real screen-space data.
	Texture2D g_ssrSpecHitInfo : register(t7);

	// Same slots the deferred pass binds these at, deliberately - the two passes
	// call EvaluateEnvSpecular with the same textures and must not drift.
	Texture2D g_iblSkyEnvAtlas : register(t15);
	Texture2D g_iblProbeAtlas  : register(t16);
	Texture2D g_iblProbeAtlas2 : register(t17);
	Texture2D g_dfgLut         : register(t21);

	SamplerState g_textureSampler : register(s0);
	SamplerState g_pointSampler   : register(s2);

	// "Did SSR trace this pixel at all", NOT "did the ray hit something".
	//
	// SSR now returns the environment along its own ray direction when the march
	// finds nothing (see SSR.shader), so a traced pixel already carries a
	// complete reflection whether it hit or missed. What is left for this pass is
	// the pixels SSR skipped entirely - matte surfaces, sky - and that boundary
	// follows whole surfaces rather than cutting through one.
	//
	// Deliberately a single tap. This used to be a 3x3 box, back when the value
	// was a per-ray hit mask that speckled and had to be softened. Blurring a
	// per-surface flag would instead bleed it across geometry edges, and since
	// the traced side already has its environment from SSR, any value below 1
	// there makes this pass add a SECOND copy - a bright fringe, the mirror image
	// of the dark rim this change removes.
	float SampleConfidence(float2 screenPos)
	{
		// Linear, not point: with r_ssrHalfRes the hit-info texture is half
		// the resolve resolution, and point-sampling it would quantise the
		// confidence mask into visible 2x2 blocks at hit/miss boundaries.
		return saturate(g_ssrSpecHitInfo.SampleLevel(g_textureSampler, screenPos, 0).r);
	}

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		const float2 screenPos = float2(
			input.position.x / (float)g_screenWidth,
			input.position.y / (float)g_screenHeight);

		const float3 ssr =
			g_ssrSourceA.SampleLevel(g_textureSampler, screenPos, 0).rgb +
			g_ssrSourceB.SampleLevel(g_textureSampler, screenPos, 0).rgb;

		// Alpha is the fraction of the DESTINATION to replace (the pass draws with
		// PremultipliedAlpha: src + dst * (1 - src.a)). Every early-out below
		// therefore has to return alpha 0, which degrades exactly to the additive
		// blit this pass used to be. Returning 1 would wipe beauty.

		// Legacy stacking behaviour, kept for A/B: the deferred pass still owns the
		// environment term, so this pass is the plain additive blit it always was.
		if (g_iblComposeInResolve < 0.5f)
			return float4(ssr, 0.0f);

		const float4 pixelPosWS = GBUFFER_POSITION.Sample(g_pointSampler, screenPos);
		const float4 pixelNormal = GBUFFER_NORMAL.Sample(g_pointSampler, screenPos);

		// Sky. Without this the resolve would paint environment specular over the
		// sky itself - the pixels most likely to have zero SSR confidence and so
		// the ones the composition would push hardest toward environment.
		//
		// Both tests, because the engine has two spellings of "this pixel is sky"
		// and they are not redundant: pixelPosWS.a is what the deferred pass keys
		// on (so the pixel set that gets an environment term stays identical
		// whichever pass owns it), and normal.w at the frustum far plane is what
		// SSR keys on (so a pixel SSR declined to trace can't be handed a full
		// environment term here instead).
		if (pixelPosWS.a > 0.0f || pixelNormal.w == g_frustumDepths[3])
			return float4(ssr, 0.0f);

		const float4 pixelColour = GBUFFER_DIFFUSE.Sample(g_pointSampler, screenPos);
		const float4 matSample   = GBUFFER_SPECULAR.Sample(g_pointSampler, screenPos);

		const float metallic = matSample.r;

		const float3 N = normalize(pixelNormal.xyz);
		const float3 V = normalize(g_eyePos.xyz - pixelPosWS.xyz);

		float3 envRadiance;
		float3 specularReflectance;
		const float3 envSpecular = EvaluateEnvSpecular(
			g_iblSkyEnvAtlas, g_iblProbeAtlas, g_iblProbeAtlas2, g_dfgLut,
			g_textureSampler,
			N, V, pixelPosWS.xyz,
			pixelColour.rgb, metallic, matSample.g,
			g_iblParams,
			float2(g_useDfgLut, g_useMultiScatter),
			g_probeCenter, g_probeExtents, g_probeCenter2, g_probeExtents2,
			envRadiance,
			specularReflectance);

		const float confidence = SampleConfidence(screenPos);

		// Energy split. The reflection is light the surface sends toward the eye
		// INSTEAD of the light it already emitted, not on top of it - so the base
		// layer has to lose the reflected fraction.
		//
		// Beauty arrives holding the surface's full diffuse and direct lighting,
		// and this pass used to be a plain additive blend, giving
		//     diffuse + F * reflection
		// where energy conservation wants
		//     (1 - F) * diffuse + F * reflection.
		// Nothing anywhere scaled the base down; the only (1-F)-shaped term in
		// the deferred pass is a constant (1 - f0). The error tracks Fresnel, so
		// it is largest exactly where the reflection is most visible - measured
		// on a wet floor at +19% near the camera rising to +30% toward the
		// horizon, and tending to 2x at true grazing.
		//
		// The alpha channel carries that fraction and the pass draws with
		// PremultipliedAlpha (src + dst * (1 - src.a)), so one draw does both.
		// Note this deliberately attenuates ALL of beauty, including the direct
		// specular highlight: that highlight and the environment reflection are
		// the same lobe, so leaving it at full strength while adding a mirror
		// reflection would double-count the same energy again.
		// The attenuation has to match what was actually ADDED, and the two
		// sources are weighted differently:
		//
		//   screen reflection - SSR.shader premodulates by raw Schlick Fresnel,
		//                       which goes to 1 at grazing incidence
		//   environment       - weighted by the split-sum reflectance, which for
		//                       a rough surface stays low
		//
		// Attenuating everything by the split-sum term looked right on paper and
		// measured as a flat 3.3% reduction at every depth - no grazing rise at
		// all, because this floor's roughness is 0.625 and the split-sum value
		// barely moves with angle. Meanwhile the screen reflection was still
		// going in at nearly full Fresnel. So blend the two weights by the same
		// confidence that decides which reflection the pixel actually got.
		const float3 F0 = lerp(0.04f.xxx, saturate(pixelColour.rgb), metallic);
		const float NdotV = saturate(dot(N, V));
		const float3 schlick = F0 + (1.0f.xxx - F0) * pow(1.0f - NdotV, 5.0f);

		const float3 appliedReflectance = lerp(specularReflectance, schlick, confidence);

		const float reflectance = g_ssrEnergyConserve > 0.5f
			? saturate(dot(appliedReflectance, float3(0.2126f, 0.7152f, 0.0722f)))
			: 0.0f;

		return float4(ssr + envSpecular * (1.0f - confidence), reflectance);
	}
}
