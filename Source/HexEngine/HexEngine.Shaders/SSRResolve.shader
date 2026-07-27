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

	// Confidence arrives from a single stochastic ray per pixel, so it is close to
	// binary and speckles on rough surfaces where the GGX cone jitters. The
	// radiance it gates has been through NRD's spatial filter, so an unfiltered
	// mask would fight it at every hit/miss boundary. A 3x3 box is the cheapest
	// thing that turns 0-or-1 into ten levels and roughly tracks the denoiser's
	// own footprint; it is not an attempt to match NRD exactly.
	float SampleConfidence(float2 screenPos)
	{
		const float2 texel = float2(1.0f / (float)g_screenWidth, 1.0f / (float)g_screenHeight);

		float total = 0.0f;
		[unroll]
		for (int y = -1; y <= 1; ++y)
		{
			[unroll]
			for (int x = -1; x <= 1; ++x)
			{
				const float2 uv = screenPos + float2((float)x, (float)y) * texel;
				total += g_ssrSpecHitInfo.SampleLevel(g_pointSampler, uv, 0).r;
			}
		}

		return saturate(total * (1.0f / 9.0f));
	}

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		const float2 screenPos = float2(
			input.position.x / (float)g_screenWidth,
			input.position.y / (float)g_screenHeight);

		const float3 ssr =
			g_ssrSourceA.SampleLevel(g_textureSampler, screenPos, 0).rgb +
			g_ssrSourceB.SampleLevel(g_textureSampler, screenPos, 0).rgb;

		// Legacy stacking behaviour, kept for A/B: the deferred pass still owns the
		// environment term, so this pass is the plain additive blit it always was.
		if (g_iblComposeInResolve < 0.5f)
			return float4(ssr, 1.0f);

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
			return float4(ssr, 1.0f);

		const float4 pixelColour = GBUFFER_DIFFUSE.Sample(g_pointSampler, screenPos);
		const float4 matSample   = GBUFFER_SPECULAR.Sample(g_pointSampler, screenPos);

		const float metallic = matSample.r;

		const float3 N = normalize(pixelNormal.xyz);
		const float3 V = normalize(g_eyePos.xyz - pixelPosWS.xyz);

		float3 envRadiance;
		const float3 envSpecular = EvaluateEnvSpecular(
			g_iblSkyEnvAtlas, g_iblProbeAtlas, g_iblProbeAtlas2, g_dfgLut,
			g_textureSampler,
			N, V, pixelPosWS.xyz,
			pixelColour.rgb, metallic, matSample.g,
			g_iblParams,
			float2(g_useDfgLut, g_useMultiScatter),
			g_probeCenter, g_probeExtents, g_probeCenter2, g_probeExtents2,
			envRadiance);

		const float confidence = SampleConfidence(screenPos);

		return float4(ssr + envSpecular * (1.0f - confidence), 1.0f);
	}
}
