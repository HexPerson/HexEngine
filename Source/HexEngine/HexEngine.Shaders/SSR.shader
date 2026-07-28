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
		ShadowUtils
		LightingUtils
		Atmosphere
		// SampleEnvAtlas / ProbeWeight / ProbeSpecularDir, so a specular ray that
		// finds nothing can return the environment itself rather than leaving a
		// hole for the resolve to patch.
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

		return output;
	}
}
"PixelShader"
{
	GBUFFER_RESOURCE(0, 1, 2, 3, 4);
	Texture2D g_beautyTexture : register(t5);
	Texture2D g_noiseTexture : register(t6);
	Texture2D g_historyTexture : register(t7);
	Texture2D g_velocityTexture : register(t8);

	// Voxel GI clipmaps (radiance/opacity/albedo per clipmap, 4 clipmaps).
	// Layout must match DiffuseGI::BindVoxelsForReflection().
	Texture3D g_voxelRadianceTex0 : register(t9);
	Texture3D g_voxelOpacityTex0  : register(t10);
	Texture3D g_voxelAlbedoTex0   : register(t11);
	Texture3D g_voxelRadianceTex1 : register(t12);
	Texture3D g_voxelOpacityTex1  : register(t13);
	Texture3D g_voxelAlbedoTex1   : register(t14);
	Texture3D g_voxelRadianceTex2 : register(t15);
	Texture3D g_voxelOpacityTex2  : register(t16);
	Texture3D g_voxelAlbedoTex2   : register(t17);
	Texture3D g_voxelRadianceTex3 : register(t18);
	Texture3D g_voxelOpacityTex3  : register(t19);
	Texture3D g_voxelAlbedoTex3   : register(t20);

	// Environment atlases for the specular miss path. t21 is the sky-view LUT
	// (bound explicitly by RenderSSR), so these start at t22. Null binds read as
	// black, which degrades to the old "miss contributes nothing" behaviour.
	Texture2D g_ssrSkyEnvAtlas : register(t22);
	Texture2D g_ssrProbeAtlas  : register(t23);
	Texture2D g_ssrProbeAtlas2 : register(t24);

	SamplerState g_textureSampler : register(s0);
	SamplerComparisonState g_cmpSampler : register(s1);
	SamplerState g_pointSampler : register(s2);
	SamplerState g_linearSampler : register(s4);

	// Mirrors DiffuseGI::GIConstants. Only the fields SSR needs are used; the rest are kept
	// in layout so the constant buffer reads correctly.
	cbuffer GIConstants : register(b4)
	{
		float4 g_clipCenterExtent[4];
		float4 g_clipPreviousCenterExtent[4];
		float4 g_clipVoxelInfo[4];
		float4 g_giParams0;
		float4 g_giParams1;
		float4 g_giParams2;
		float4 g_giParams3;
		float4 g_giParams4;
		float4 g_giParams5;
		float4 g_giParams6;
		float4 g_giParams7;
		float4 g_giParams8;
		float4 g_giParams9;
		float4 g_giParams10;
		float4 g_giParams11;
	};

	static const float PI = 3.1415926f;

	uint NextRandom(inout uint state)
	{
		state = state * 747796405 + 2891336453;
		uint result = ((state >> ((state >> 28) + 4)) ^ state) * 277803737;
		result = (result >> 22) ^ result;
		return result;
	}

	float RandomValue(inout uint state)
	{
		return NextRandom(state) / 4294967295.0;
	}

	float RandomValueNormalDistribution(inout uint state)
	{
		float theta = 2.0f * PI * RandomValue(state);
		float rho = sqrt(-2.0f * log(max(RandomValue(state), 1e-6f)));
		return rho * cos(theta);
	}

	// lowbias32 integer hash (Chris Wellons). One-shot mixer that produces well-decorrelated
	// outputs for nearly-sequential input integers - which is critical here because our rng
	// seed comes from pixelIndex + small noise contributions, i.e. inputs that differ by ~1
	// between adjacent pixels. The PCG advancement used by RandomValue is good for long
	// sequences from a single pixel, but for adjacent pixels' first call its output differs
	// by a roughly constant value (around 0.065 in [0,1] units after divide), which produces
	// smoothly-varying directions rather than per-pixel noise. Mixing the seed through
	// lowbias32 first decorrelates adjacent pixels so the rng's first output is uncorrelated.
	uint Hash32(uint x)
	{
		x ^= x >> 16;
		x *= 0x7feb352du;
		x ^= x >> 15;
		x *= 0x846ca68bu;
		x ^= x >> 16;
		return x;
	}

	// Uniform random direction in `normal`'s hemisphere, generated in WORLD SPACE directly.
	// The world-space direction is computed from two uniforms via spherical coords
	// (cosTheta, phi), NOT through a tangent frame built from `normal`. Tangent-frame
	// approaches (cosine-weighted Malley etc.) inherently rotate the sampled local-space
	// direction by the per-pixel normal, so any normal-map detail on the surface "leaks"
	// into the output. With this direct world-space generation, only the hemisphere-flip
	// touches `normal`, and that's a binary sign decision.
	float3 RandomDirectionInDirectionOfNormal(float3 normal, inout uint state)
	{
		// Pre-mix the state with lowbias32 so adjacent-pixel inputs decorrelate before
		// we extract the first two uniforms (see Hash32 comment above).
		state = Hash32(state);

		const float u1 = RandomValue(state);
		const float u2 = RandomValue(state);

		// Uniform-on-sphere via direct spherical coordinates.
		const float cosTheta = 1.0f - 2.0f * u1;             // uniform in [-1, 1]
		const float sinTheta = sqrt(max(0.0f, 1.0f - cosTheta * cosTheta));
		const float phi = 2.0f * PI * u2;
		float3 worldDir = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);

		// Flip into the normal's hemisphere. Binary decision, doesn't carry texture detail.
		if (dot(worldDir, normal) < 0.0f)
			worldDir = -worldDir;

		return worldDir;
	}

	bool IsInsideUnit(float3 uvw)
	{
		return all(uvw >= 0.0f.xxx) && all(uvw <= 1.0f.xxx);
	}

	float4 SampleVoxelRadianceClip(uint clipIdx, float3 uvw)
	{
		switch (clipIdx)
		{
		case 0: return g_voxelRadianceTex0.SampleLevel(g_linearSampler, uvw, 0.0f);
		case 1: return g_voxelRadianceTex1.SampleLevel(g_linearSampler, uvw, 0.0f);
		case 2: return g_voxelRadianceTex2.SampleLevel(g_linearSampler, uvw, 0.0f);
		default: return g_voxelRadianceTex3.SampleLevel(g_linearSampler, uvw, 0.0f);
		}
	}

	float SampleVoxelOpacityClip(uint clipIdx, float3 uvw)
	{
		switch (clipIdx)
		{
		case 0: return g_voxelOpacityTex0.SampleLevel(g_linearSampler, uvw, 0.0f).r;
		case 1: return g_voxelOpacityTex1.SampleLevel(g_linearSampler, uvw, 0.0f).r;
		case 2: return g_voxelOpacityTex2.SampleLevel(g_linearSampler, uvw, 0.0f).r;
		default: return g_voxelOpacityTex3.SampleLevel(g_linearSampler, uvw, 0.0f).r;
		}
	}

	// Tiny cone trace along rayDir using the voxel clipmaps for a fallback "indirect bounce"
	// in directions where SSR didn't find a screen-space hit. Steps until the voxel field becomes
	// opaque or we leave all clipmaps.
	float3 ConeTraceVoxelGI(float3 originWs, float3 rayDir, out float traceDistance)
	{
		traceDistance = 0.0f;

		float3 accumRadiance = 0.0f.xxx;
		float transmittance = 1.0f;

		// Use the finest clipmap's voxel size as the base step, fall back to a metre-ish step
		// if the constants haven't been populated yet.
		const float voxelSize = max(g_clipVoxelInfo[0].x, 0.5f);
		const float startOffset = voxelSize * 1.5f;

		const uint stepCount = 12;
		[loop]
		for (uint s = 0; s < stepCount; ++s)
		{
			// Step length grows geometrically so the trace covers far clipmaps cheaply.
			const float t = startOffset + voxelSize * (1.0f + (float)s * 1.5f);
			const float3 samplePos = originWs + rayDir * t;
			traceDistance = t;

			bool sampled = false;
			[unroll]
			for (uint i = 0; i < 4; ++i)
			{
				const float3 clipCenter = g_clipCenterExtent[i].xyz;
				const float clipExtent = max(g_clipCenterExtent[i].w, 1e-3f);
				const float3 uvw = ((samplePos - clipCenter) / (clipExtent * 2.0f)) + 0.5f;
				if (!IsInsideUnit(uvw))
					continue;

				const float4 voxel = SampleVoxelRadianceClip(i, uvw);
				const float opacity = (g_giParams2.w > 0.5f) ? voxel.a : SampleVoxelOpacityClip(i, uvw);
				accumRadiance += voxel.rgb * transmittance;
				transmittance *= saturate(1.0f - opacity);
				sampled = true;
				break;
			}

			if (!sampled)
				break; // left all clipmaps - nothing more to gather

			if (transmittance < 0.05f)
				break;
		}

		return accumRadiance * max(g_giParams0.x, 0.0f);
	}

	struct HitResult
	{
		bool didHit;       // true if a real screen-space hit was found
		bool didFallback;  // true when we fell back to the last in-screen tex (not a true hit)
		float3 colour;     // radiance to write
		float hitDistance; // world-space distance from rayStart to the hit
		float2 hitTex;     // screen UV the result was read from (debug)
	};

	HitResult RaymarchReflection(
		float3 rayStart,
		float3 rayDir,
		float3 sourceNormal,
		uint sourceInstanceID,
		float jitter,
		float rayRoughness,
		// Depth of the surface the ray starts from, for the loop-exhaustion
		// fallback's "is what I am looking at farther than me" test - water's
		// `actualDepth > currentDepth`.
		float sourceDepth)
	{
		HitResult result;
		result.didHit = false;
		result.didFallback = false;
		result.colour = 0.0f.xxx;
		result.hitDistance = 0.0f;
		result.hitTex = float2(0.0f, 0.0f);

		// Push the start point off the source surface to avoid immediate self-intersection.
		// Use both a normal bias and a small along-ray bias so the first sample is unambiguously
		// off-surface even at grazing angles where the normal bias alone wouldn't help.
		const float3 origin = rayStart + sourceNormal * 0.25f + rayDir * 0.10f;

		const int stepCount = 28;
		const int refinementStepCount = 6;
		const float minStepLen = 0.3f;
		// How far the march can reach, in world units, is what decides which
		// reflections exist at all:
		//
		//     reach ~= stepCount * (minStepLen + (maxStepLen - minStepLen) / 3)
		//
		// because the step length ramps quadratically. At the old maxStepLen of
		// 3.0 that is ~34 units, against water.shader's ~96 (24 steps, 2..8).
		// The Whereabouts hall's probe box alone is ~56 x 36 x 49 units, so a
		// floor pixel reflecting anything high on the far wall simply ran out of
		// budget partway and returned the wall it happened to be crossing. That
		// is why the LOWER window row reflected correctly - short rays - while
		// the upper row did not, and why the run-out point banded across the
		// floor as visible artifacts.
		//
		// Larger steps trade near-field precision for reach, but the 6-step
		// binary refinement below re-localises any hit once it is bracketed, and
		// the thickness test already widens with distance travelled.
		const float maxStepLen = max(minStepLen, g_ssrMaxStepLength);

		float3 fragPos = origin;
		float totalDistance = 0.0f;

		// Previous-step state for binary refinement.
		float3 prevFragPos = origin;
		float prevTotalDistance = 0.0f;

		// Last in-screen sample state - used as a best-effort fallback ONLY when the loop
		// exhausts its budget while still inside the screen (water.shader pattern). This is
		// deliberately NOT used on off-screen exits: for a floor-pixel reflection going
		// up+forward, the ray inevitably exits the top of the screen, and the "last in-screen
		// tex" was just below the top edge - i.e. the back wall - so every front-floor pixel
		// would end up reflecting the back wall and produce the long stripe artifact.
		float2 lastInScreenTex = float2(-1.0f, -1.0f);
		float lastInScreenDistance = 0.0f;
		float lastInScreenDepth = 0.0f;
		bool exitedScreen = false;
		bool hitSkyOnce = false;

		[loop]
		for (int i = 0; i < stepCount; ++i)
		{
			const float marchFraction = saturate((float)i / (float)(stepCount - 1));
			const float stepLen = lerp(minStepLen, maxStepLen, marchFraction * marchFraction);

			// Acceptance thickness, and it must be at least THIS STEP'S LENGTH.
			//
			// The depth uncertainty a march introduces is exactly how far it
			// jumped: a surface lying between two samples is invisible to a test
			// tighter than the gap. This was lerp(0.6, 2.0, marchFraction) - tied
			// to the step INDEX and capped at 2.0 - while a late step is several
			// units long, so rays sailed past real geometry and fell through to
			// the best-effort last-in-screen fallback instead of registering a
			// hit. The path-classify view showed the hall floor almost entirely
			// on that fallback rather than on real hits, which is what made the
			// reflections smeary and banded: a fallback sample is wherever the
			// ray happened to be, not what it was aimed at.
			//
			// The 6-step binary refinement below re-localises the surface once
			// bracketed, so a generous bracket costs precision nothing.
			const float thickness = max(lerp(0.6f, 2.0f, marchFraction), stepLen);

			prevFragPos = fragPos;
			prevTotalDistance = totalDistance;

			// Sub-step jitter on the very first iteration. Scaled by roughness so mirror
			// surfaces (roughness=0) have zero per-pixel/frame variance - otherwise adjacent
			// mirror-floor pixels would take slightly different first steps and hit slightly
			// different points on the back wall, producing speckle noise that NRD can't fully
			// recover from. Glossy surfaces still get jitter to decorrelate the cone samples
			// across pixels (which NRD needs for temporal accumulation).
			const float firstStepScale = (i == 0) ? lerp(1.0f, lerp(0.5f, 1.0f, jitter), rayRoughness) : 1.0f;
			fragPos += rayDir * stepLen * firstStepScale;
			totalDistance += stepLen * firstStepScale;

			// Match the gbuffer's TAA-jittered rasterisation: the floor/walls were rendered with
			// `clip.xy += g_jitterOffsets * w` in their vertex shaders, so every world point's
			// data lives at clip + jitter*w (= jittered pixel) in the gbuffer. To sample the
			// gbuffer consistently with where this world point actually lives, apply the same
			// jitter offset to the SSR projection. Without this, SSR projects to the canonical
			// (un-jittered) pixel and reads gbuffer data of a sub-pixel-different world point
			// each frame -> shimmer. Water.shader doesn't have this problem because its own
			// vertex shader never applies the jitter to begin with.
			float4 fragView = mul(float4(fragPos, 1.0f), g_viewMatrix);
			float4 fragClip = mul(fragView, g_projectionMatrix);
			fragClip.xy += g_jitterOffsets * fragClip.w;
			fragClip.xyz /= fragClip.w;
			const float fragDepth = -fragView.z;
			const float2 fragNDC = fragClip.xy * 0.5f + 0.5f;
			const float2 fragTex = float2(fragNDC.x, 1.0f - fragNDC.y);

			// Off-screen: stop marching and skip the last-tex fallback entirely. The pure
			// voxel-GI cone trace path will handle directional radiance for these rays.
			if (any(fragTex < 0.0f) || any(fragTex > 1.0f))
			{
				exitedScreen = true;
				break;
			}

			// SampleLevel(..., 0) instead of Sample() because we're inside a
			// variable-iteration ray-march loop. HLSL release-mode (warnings ->
			// errors) flags Sample()'s implicit ddx/ddy gradient calculations
			// as undefined in this context: lanes can be at different iterations
			// so cross-lane derivative reads are garbage. Explicit mip 0 is what
			// we want anyway - the GBuffer is screen-resolution and we never
			// want a mipped read.
			const float4 normalDepth = GBUFFER_NORMAL.SampleLevel(g_pointSampler, fragTex, 0);
			const float actualDepth = normalDepth.w;			

			

			// if(actualDepth == g_frustumDepths[3])
			// {
			// 	exitedScreen = false;
			// 	break;
			// }

			// Note: we deliberately do NOT special-case sky pixels here. Sky's actualDepth is
			// the frustum-far value (very large), so the depth check below naturally rejects
			// "hits" on sky pixels - the ray's depth never gets close enough. This matches
			// water.shader. The previous didHitSky early-return was the cause of the bright
			// blue streaks: vertical rays from the floor immediately saw sky-through-window
			// pixels and returned sky colour, before the ray had any chance to actually reach
			// the wall geometry in 3D.

			const bool didHitSky = (actualDepth == g_frustumDepths[3]) && (fragDepth > sourceDepth) && !hitSkyOnce;

			if(didHitSky)
				hitSkyOnce = true;

			// Ray has passed behind the surface within the thickness window - candidate hit.
			const float depthDelta = fragDepth - actualDepth;
			if ((depthDelta > 0.0f && (depthDelta < thickness)) || didHitSky)
			{
				// Binary-search refine between previous (in-front) and current (behind) samples.
				float3 a = prevFragPos;
				float3 b = fragPos;
				float da = prevTotalDistance;
				float db = totalDistance;
				float2 refinedTex = fragTex;
				float refinedDistance = totalDistance;

				[loop]
				for (int j = 0; j < refinementStepCount; ++j)
				{
					const float3 mid = (a + b) * 0.5f;
					const float dmid = (da + db) * 0.5f;

					float4 midView = mul(float4(mid, 1.0f), g_viewMatrix);
					float4 midClip = mul(midView, g_projectionMatrix);
					midClip.xy += g_jitterOffsets * midClip.w;
					midClip.xyz /= midClip.w;
					const float midDepth = -midView.z;
					const float2 midNDC = midClip.xy * 0.5f + 0.5f;
					const float2 midTex = float2(midNDC.x, 1.0f - midNDC.y);

					if (any(midTex < 0.0f) || any(midTex > 1.0f))
					{
						b = mid;
						db = dmid;
						continue;
					}

					// SampleLevel(..., 0) - same rationale as the outer loop: implicit
					// derivatives inside variable-iteration loops are undefined under
					// HLSL release mode.
					const float midActual = GBUFFER_NORMAL.SampleLevel(g_pointSampler, midTex, 0).w;

					if (midDepth >= midActual)
					{
						b = mid;
						db = dmid;
						refinedTex = midTex;
						refinedDistance = dmid;
					}
					else
					{
						a = mid;
						da = dmid;
					}
				}

				// Reject self-hits at the very source surface; the next iteration will progress
				// further along the ray.
				const uint hitInstance = (uint)GBUFFER_DIFFUSE.SampleLevel(g_pointSampler, refinedTex, 0).w;
				if (hitInstance == sourceInstanceID && refinedDistance < 0.2f)
					continue;

				const float4 hitPosWS = GBUFFER_POSITION.SampleLevel(g_pointSampler, refinedTex, 0);
				// Linear-sample the beauty at the hit point. Note: the beauty texture is rendered
				// with TAA jitter per-vertex, so its sub-pixel content rotates frame-to-frame
				// through the 16-sample Halton sequence. No single-frame sample is fully stable;
				// linear sampling smooths sub-pixel boundaries at high-contrast edges (windows
				// vs frames) but residual frame-to-frame variance is inherent to reading a
				// jittered texture at a non-jittered projection result. The proper resolver for
				// that residual variance is NRD's temporal accumulation (or TAA on the SSR
				// output) - shader-side single-frame tricks (jitter UV offset etc.) just trade
				// one set of sub-pixel weights for another.
				const float3 hitColour = g_beautyTexture.SampleLevel(g_textureSampler, refinedTex, 0).rgb;

				result.didHit = true;
				result.colour = hitColour;
				result.hitDistance = max(length(hitPosWS.xyz - rayStart), refinedDistance);
				result.hitTex = refinedTex;

				if(didHitSky)
					continue;

				return result;
			}

			// Remember this in-screen sample for the loop-exhaustion fallback only.
			lastInScreenTex = fragTex;
			lastInScreenDistance = totalDistance;
			lastInScreenDepth = actualDepth;
		}

		// No real screen-space hit. Only use the water.shader last-in-screen-tex fallback when
		// the loop exhausted INSIDE the screen (i.e. the ray was still progressing across valid
		// geometry but ran out of step budget). For rays that exited screen, leave didFallback
		// false and rely on the pure voxel-GI cone trace in GetReflection - that direction is
		// genuinely "outside what we can see", so the last in-screen tex is meaningless and
		// would produce the stripe artifact along surfaces whose reflections all exit the same
		// screen edge.
		// The `lastInScreenDepth > sourceDepth` guard is water.shader's
		// `actualDepth > currentDepth`: only trust the last in-screen sample when
		// the surface it landed on is FARTHER from the eye than the surface we
		// are reflecting from. That is what makes the sample meaningful rather
		// than arbitrary - the ray is still travelling toward something real that
		// the camera can see, instead of having wandered across nearer geometry.
		if (!exitedScreen && lastInScreenTex.x >= 0.0f && lastInScreenDepth > sourceDepth && false)
		{
			result.didHit = true;
			result.didFallback = true;
			result.colour = g_beautyTexture.SampleLevel(g_textureSampler, lastInScreenTex, 0).rgb;
			result.hitTex = lastInScreenTex;
			result.hitDistance = max(lastInScreenDistance, 1.0f);
		}

		return result;
	}

	// Screen-space DDA ray march (McGuire & Mara, "Efficient GPU Screen-Space
	// Ray Tracing", JCGT 2014). r_ssrMarchMode 1.
	//
	// The legacy marcher above steps in WORLD space and projects every step to
	// screen, so a step's screen footprint is unpredictable: near the camera one
	// step spans many pixels (hits get skipped - surfaces thinner than the gap
	// are invisible), far away many steps land in the same pixel (wasted work).
	// The acceptance thickness then has to absorb that error, coupling two knobs
	// that should be independent; today's history of trading smears for glow by
	// tuning either one is that coupling at work.
	//
	// Here the ray is clipped and projected ONCE, then the 2D line is walked in
	// fixed pixel-space increments. Attributes that are linear in screen space -
	// 1/viewZ among them - are interpolated directly, so depth along the ray is
	// perspective-correct at every pixel with no per-step matrix work. Every
	// pixel the ray crosses is visited once. Thickness now models only real
	// geometric thickness.
	//
	// Sky handling preserved from the hand fix that proved it out: a sky texel
	// in front of the ray depth is recorded as a PROVISIONAL hit and the march
	// continues, so real geometry found later along the ray wins. That is what
	// makes a floor ray aimed through a window return the sky seen through it,
	// without resurrecting the historical "first sky texel wins" streaking.
	HitResult RaymarchReflectionDDA(
		float3 rayStart,
		float3 rayDir,
		float3 sourceNormal,
		uint sourceInstanceID,
		float jitter,
		float rayRoughness,
		float sourceDepth)
	{
		HitResult result;
		result.didHit = false;
		result.didFallback = false;
		result.colour = 0.0f.xxx;
		result.hitDistance = 0.0f;
		result.hitTex = float2(0.0f, 0.0f);

		// Same self-intersection bias as the legacy marcher.
		const float3 origin = rayStart + sourceNormal * 0.25f + rayDir * 0.10f;

		// Total world-space length to consider. Generous: unlike the legacy
		// marcher, unreachable far ends cost nothing here because the walk is
		// bounded in PIXELS, not world units - a long ray that crosses few
		// pixels is cheap by construction.
		const float maxRayDistance = 300.0f;

		// Clip the ray to the near plane in VIEW space before projecting - a
		// segment crossing z=0 projects to garbage.
		float4 v0 = mul(float4(origin, 1.0f), g_viewMatrix);
		float4 v1 = mul(float4(origin + rayDir * maxRayDistance, 1.0f), g_viewMatrix);
		// This engine's view space looks down -Z (fragDepth = -fragView.z in the
		// legacy marcher). Clamp the far end to just inside the near plane.
		const float nearZ = -0.11f;
		if (v1.z > nearZ)
		{
			const float t = (nearZ - v0.z) / (v1.z - v0.z);
			v1 = lerp(v0, v1, saturate(t));
		}
		if (v0.z > nearZ)
			return result; // start behind the near plane - nothing to march

		float4 c0 = mul(v0, g_projectionMatrix);
		float4 c1 = mul(v1, g_projectionMatrix);
		c0.xy += g_jitterOffsets * c0.w;
		c1.xy += g_jitterOffsets * c1.w;

		// Screen-space endpoints in PIXELS, plus the attributes that interpolate
		// linearly in screen space: 1/w-scaled position is not needed, only
		// 1/viewZ for the depth test and the world-space distance parameter for
		// the hit report.
		const float2 screenSize = float2((float)g_screenWidth, (float)g_screenHeight);
		const float invW0 = 1.0f / c0.w;
		const float invW1 = 1.0f / c1.w;
		float2 p0 = (c0.xy * invW0 * 0.5f + 0.5f);
		float2 p1 = (c1.xy * invW1 * 0.5f + 0.5f);
		p0 = float2(p0.x, 1.0f - p0.y) * screenSize;
		p1 = float2(p1.x, 1.0f - p1.y) * screenSize;
		// Last sky texel the walk crossed, if any. This is the RIGHT place to
		// answer "does this ray see sky" from - not the ray's endpoint. The sky
		// seen through a window occupies the WINDOW's screen rectangle (the ray
		// crosses its plane at finite distance), while the ray's vanishing point
		// projects beyond the pane - onto the wall above it, or off-frame -
		// because the room is enclosed. Testing the endpoint therefore reported
		// "wall" for exactly the rays that genuinely escape through the glass and
		// killed their reflections into environment grey. The last crossing is
		// nearest the escape point, so it wins.
		bool sawSky = false;
		float2 skyT = float2(0.0f, 0.0f);

		// viewZ is positive-depth (negated view z), matching gbuffer normal.w.
		const float z0 = -v0.z;
		const float z1 = -v1.z;
		const float invZ0 = 1.0f / z0;
		const float invZ1 = 1.0f / z1;

		// Degenerate projection (ray nearly along the view axis): the whole
		// march lands in a handful of pixels. Nudge the end one pixel so the
		// DDA still advances; the depth interpolation stays correct.
		if (distance(p0, p1) < 1.0f)
			p1 += float2(1.0f, 1.0f);

		const float2 delta = p1 - p0;
		const float pixelLength = length(delta);
		const float2 stepDir = delta / pixelLength;

		// Pixel stride. 1 visits literally every pixel; that is exact but at
		// 4K a long ray is thousands of taps. Stride s visits every s-th pixel,
		// bounding the worst-case skip at s pixels - a known, uniform, SCREEN
		// SPACE quantity, unlike the legacy marcher's world-space skips. The
		// stride grows with distance along the ray (reflections far from the
		// reflector get progressively coarser, which roughness masks anyway)
		// and the loop is capped at a fixed sample budget.
		const int sampleBudget = 96;
		const float baseStride = max(1.0f, pixelLength / (float)sampleBudget);

		// Sub-pixel jitter decorrelates adjacent rays' sample phase. Scaled by
		// roughness exactly like the legacy marcher: mirror surfaces need
		// deterministic sampling or adjacent pixels speckle.
		const float ditherPhase = lerp(0.0f, jitter, rayRoughness);

		float prevT = 0.0f;
		bool exitedScreenDDA = false;

		[loop]
		for (int i = 0; i < sampleBudget; ++i)
		{
			// Parameter along the 2D line in [0,1]. Quadratic ramp like the
			// legacy marcher: dense near the reflector where detail lives,
			// coarser far away.
			const float f = ((float)i + 0.5f + ditherPhase) / (float)sampleBudget;
			const float t = f * f;
			const float2 pixel = p0 + stepDir * (t * pixelLength);
			const float2 fragTex = pixel / screenSize;

			if (any(fragTex < 0.0f) || any(fragTex > 1.0f))
			{
				exitedScreenDDA = true;
				break;
			}

			// Perspective-correct depth at this pixel: 1/z interpolates
			// linearly along the screen-space line.
			const float invZ = lerp(invZ0, invZ1, t);
			const float rayDepth = 1.0f / invZ;

			const float4 normalDepth = GBUFFER_NORMAL.SampleLevel(g_pointSampler, fragTex, 0);
			const float surfaceDepth = normalDepth.w;

			// Sky texel: remember it and keep marching, so geometry later along
			// the ray still wins. Three variants of this were tried and only this
			// one is correct:
			//   first crossing wins  - stamped every pane the 2D line clipped
			//                          into the reflection (phantom pane rows)
			//   ray endpoint decides - the vanishing point projects BEYOND the
			//                          pane (onto the wall above it, or off
			//                          frame) because the room is enclosed, so
			//                          rays genuinely escaping through glass
			//                          reported "wall" and went grey
			//   LAST crossing wins   - nearest the ray's actual escape point,
			//                          right colour, right place
			// Geometry found after the last sky crossing still returns normally,
			// which is what keeps walls from being replaced by panes.
			if (surfaceDepth >= g_frustumDepths[3] * 0.999f)
			{
				prevT = t;
				sawSky = true;
				skyT = fragTex;
				continue;
			}

			const float depthDelta = rayDepth - surfaceDepth;

			// Thickness models GEOMETRY now, not marching error: how thick we
			// assume the surface behind a depth sample to be. Grows mildly with
			// depth so distant thin geometry (window frames) still registers
			// against depth-buffer precision.
			const float thickness = 0.5f + surfaceDepth * 0.02f;

			if (depthDelta > 0.0f && depthDelta < thickness)
			{
				// Refine between the previous and current parameter. The DDA
				// analogue of the legacy binary search: bisect t, not world
				// position.
				float ta = prevT;
				float tb = t;
				float2 refinedTex = fragTex;

				[loop]
				for (int j = 0; j < 5; ++j)
				{
					const float tm = (ta + tb) * 0.5f;
					const float2 mPix = p0 + stepDir * (tm * pixelLength);
					const float2 mTex = mPix / screenSize;
					const float mDepth = 1.0f / lerp(invZ0, invZ1, tm);
					const float mSurface = GBUFFER_NORMAL.SampleLevel(g_pointSampler, mTex, 0).w;
					if (mDepth > mSurface) { tb = tm; refinedTex = mTex; }
					else                   { ta = tm; }
				}

				// Self-hit rejection, sky-exempt per the hand fix: sky can
				// never be a self-hit, and the blanket version of this guard
				// was blocking legitimate hits.
				const uint hitInstance = (uint)GBUFFER_DIFFUSE.SampleLevel(g_pointSampler, refinedTex, 0).w;
				const float hitWorldDist = distance(
					GBUFFER_POSITION.SampleLevel(g_pointSampler, refinedTex, 0).xyz, rayStart);
				// if (hitInstance == sourceInstanceID && hitWorldDist < 2.0f)
				// {
				// 	prevT = t;
				// 	continue;
				// }

				result.didHit = true;
				result.colour = g_beautyTexture.SampleLevel(g_textureSampler, refinedTex, 0).rgb;
				result.hitTex = refinedTex;
				result.hitDistance = max(hitWorldDist, 0.5f);
				return result;
			}

			prevT = t;
		}

		// No geometry hit anywhere along the ray. The ray sees sky if and only
		// if ITS OWN ENDPOINT - the vanishing point of the 3D direction, where
		// the ray "is" at 300 units - lands on a sky texel:
		//
		//   endpoint on a pane's sky        -> that sky, correct hue and place
		//   endpoint on wall/frame geometry -> the march missed real geometry
		//                                      (stride skip); environment, whose
		//                                      grey is honest, not a stolen pane
		//   endpoint off screen             -> ray leaves the frame (steep
		//                                      near-camera rays aimed at the
		//                                      off-screen ceiling); environment
		//
		// This is what kills the phantom-pane artifact: no intermediate crossing
		// can ever be promoted to an answer.
		if (!exitedScreenDDA && sawSky)
		{
			result.didHit = true;
			result.colour = g_beautyTexture.SampleLevel(g_textureSampler, skyT, 0).rgb;
			result.hitTex = skyT;
			result.hitDistance = max(z1, 8.0f);
		}

		return result;
	}

	float4 GetReflection(
		float3 eyeDir,
		float3 worldPos,
		float3 worldNormal,
		float currentDepth,
		out bool didReflect,
		out float hitDistance,
		inout uint rngState,
		float smoothness,
		// Perceptual roughness from the gbuffer's own channel, for the
		// environment lookup. NOT 1 - smoothness: this engine stores roughness
		// (.g) and smoothness (.b) as INDEPENDENT values - the hall floor is
		// authored smoothness 0.0 with roughnessFactor 0.625 - so deriving one
		// from the other picked the sharpest atlas row for a rough surface and
		// turned the fallback into hard bright blobs.
		float perceptualRoughness,
		bool wantSpecular,
		uint instanceID,
		// Which path produced the result, for r_ssrDebugSkyHits:
		//   0 = real screen-space hit
		//   1 = environment fill (the march found nothing)
		//   2 = in-screen loop exhaustion / voxel-GI fallback
		out float pathId,
		// Screen-space position the ray actually resolved to, for
		// r_ssrDebugSkyHits 3. "It hit something" and "it hit the thing you
		// think it did" are different claims.
		out float2 hitUv)
	{
		pathId = 0.0f;
		hitUv = float2(0.0f, 0.0f);
		didReflect = false;
		hitDistance = 0.0f;

		// Diffuse path: stochastic single-direction hemisphere raymarch contributing ONLY
		// the screen-space DELTA over DiffuseGI's voxel-cone baseline.
		//
		// Why a delta and not a full hemisphere integral: DiffuseGI runs immediately before
		// SSR and writes a hemisphere-integrated voxel-GI value to beauty for every diffuse
		// pixel. SSR is additively composited onto beauty (SceneRenderer::RenderSSR), so any
		// full re-integration of the voxel hemisphere here would literally double-count GI -
		// which is exactly what blew matte surfaces out in the previous version. Instead we
		// fire one screen-space ray per pixel per frame in a random hemisphere direction and
		// add (screenHit - voxelBaselineInSameDirection) when there is a true screen hit:
		//   - When voxel already had a good estimate for that direction, delta ~= 0.
		//   - When the screen sees something sharper/brighter than the voxel approximated
		//     (high-freq lit geometry that doesn't fit the clipmap resolution), the delta
		//     contributes that extra detail on top of GI.
		// Clamped non-negative so we never punch dark holes in GI when the voxel happened
		// to over-estimate that direction.
		//
		// Single-sample-per-pixel variance is denoised by NRD's diffuse channel; many pixels
		// and frames together approximate the hemisphere integral of the delta.
		if (!wantSpecular)
		{
			didReflect = true;

			const float3 diffuseDir = RandomDirectionInDirectionOfNormal(worldNormal, rngState);
			const float NdotL = saturate(dot(diffuseDir, worldNormal));
			const float jitter = RandomValue(rngState);

			// rayRoughness=1.0 -> wide first-step jitter, which is what we want for a diffuse
			// stochastic sample (decorrelates adjacent pixels so NRD can integrate spatially).
			// if/else, not a ternary: FXC's DXBC path rejects ?: between two
			// struct-valued calls (X3020) even when the types match.
			HitResult hit;
			if (g_ssrMarchMode > 0.5f)
				hit = RaymarchReflectionDDA(worldPos, diffuseDir, worldNormal, instanceID, jitter, 1.0f, currentDepth);
			else
				hit = RaymarchReflection(worldPos, diffuseDir, worldNormal, instanceID, jitter, 1.0f, currentDepth);

			

			// Only true screen-space hits contribute - skip the in-screen loop-exhaustion
			// fallback (hit.didFallback) here because that path returns last-in-screen beauty
			// regardless of actual ray geometry, which for random hemisphere directions is
			// directionally meaningless and would just stamp screen-edge colour onto matte
			// surfaces.
			if (hit.didHit && !hit.didFallback)
			{
				// Voxel-GI cone in the same direction = the baseline DiffuseGI's cone trace
				// already added to beauty. Subtract so we only contribute the screen-space
				// delta. Clamp non-negative.
				float voxelTraceDist;
				const float3 voxelBaseline = ConeTraceVoxelGI(worldPos + worldNormal * 0.25f, diffuseDir, voxelTraceDist);
				const float3 delta = max(0.0f.xxx, hit.colour  - voxelBaseline);

				hitDistance = max(hit.hitDistance, 1.0f);
				return float4(delta * NdotL, 1.0f);
			}

			// Miss (off-screen exit or pure no-hit): contribute nothing. DiffuseGI already
			// covered this direction at low frequency; the voxel cone-trace fallback that the
			// specular path uses is redundant here and would re-introduce the double-count.
			hitDistance = 4.0f;
			return float4(0.0f.xxx, 1.0f);
		}

		// Specular: ray-marched mirror reflection with roughness-scaled cone perturbation.
		// rayRoughness is in [0,1]; 0 = perfect mirror (no perturbation), 1 = full hemisphere.
		// For specular rays we sample a cone around the mirror direction whose half-angle
		// scales with roughness. At roughness=0 the cone collapses to the exact mirror
		// reflection (deterministic, sharp). At higher roughness the cone widens, giving the
		// glossy lobe that NRD's roughness-aware kernel then resolves to a stable blurred
		// reflection. Without this, the spec direction was always mirror regardless of
		// smoothness, so "smoothness" never affected reflection sharpness - it was just an
		// intensity scalar.
		const float3 specularDir = normalize(reflect(eyeDir, worldNormal));
		const float rayRoughness = saturate(1.0f - smoothness);
		const float3 randomOffset = RandomDirectionInDirectionOfNormal(specularDir, rngState);
		float3 rayDir = normalize(specularDir + randomOffset * rayRoughness * rayRoughness);
		// If perturbation pushed the ray below the surface, snap back to the mirror dir.
		if (dot(rayDir, worldNormal) < 0.0f)
			rayDir = specularDir;

		const float jitter = RandomValue(rngState);

		HitResult hit;
		if (g_ssrMarchMode > 0.5f)
			hit = RaymarchReflectionDDA(worldPos, rayDir, worldNormal, instanceID, jitter, rayRoughness, currentDepth);
		else
			hit = RaymarchReflection(worldPos, rayDir, worldNormal, instanceID, jitter, rayRoughness, currentDepth);

		if (hit.didHit && !hit.didFallback)
		{
			// True screen-space hit (sky or geometry) - report exact world-space distance.
			didReflect = true;
			hitDistance = max(hit.hitDistance, 0.0f);
			hitUv = hit.hitTex;
			return float4(hit.colour, 1.0f);
		}

		// Loop exhausted while still on screen, looking at something FARTHER than
		// the reflecting surface: take the beauty there, exactly as water.shader
		// does. This is how water gets a correct sky reflection, and dropping it
		// is why the reflections it used to get right regressed.
		//
		// It was removed for specular over a "long stripe artifact" - a road pixel
		// reflecting upward marches through empty air, stays on screen the whole
		// way, and lands on an arbitrary sample. The real defect there was that
		// ANY last-in-screen sample was accepted; water guards it with
		// `actualDepth > currentDepth`, now mirrored in RaymarchReflection. A ray
		// that wandered across nearer geometry fails that test and falls through
		// to the environment, while a ray still travelling toward something the
		// camera can genuinely see is trusted.
		//
		// This is what fixes the reflected window panes, and it needs no
		// environment at all: the outdoors behind the glass is ALREADY in the
		// beauty buffer at those pixels. Transparent glass writes no opaque
		// gbuffer, so the surface behind it is far away and the march can never
		// satisfy the strict hit test - but the ray is pointing straight at a
		// bright, visible, on-screen window the whole time. Falling back to a
		// room-average probe threw that away and returned something much darker,
		// which is what read as black panes.
		if (hit.didHit && hit.didFallback && g_ssrInScreenFallback > 0.5f)
		{
			didReflect = true;
			hitDistance = max(hit.hitDistance, 1.0f);
			hitUv = hit.hitTex;
			pathId = 3.0f;
			return float4(hit.colour, 1.0f);
		}

		// Composition path: a ray that finds nothing returns the ENVIRONMENT along
		// its own direction, right here, rather than leaving a hole for the
		// resolve to patch afterwards.
		//
		// This is the case that rendered window panes black in the floor's
		// reflection: transparent glass never writes the opaque gbuffer, so a
		// floor ray aimed at a pane marches straight through and finds nothing,
		// and the voxel cone trace below returns black whenever GI is off (which
		// is also when its clipmaps are deliberately left unbound - see
		// RenderSSR). Environment is a far better estimate of "what is off-screen
		// in this direction" than a cone trace of a possibly-empty clipmap.
		//
		// Emitting it HERE rather than in the resolve is what kills the
		// silhouettes. The resolve version gated environment on a confidence mask
		// that was computed fresh each frame from one stochastic ray, while the
		// radiance it gated had been through NRD's spatial AND temporal filter.
		// The two disagreed at every hit/miss boundary - NRD spreads a hit's
		// radiance outward, and during camera motion disoccluded pixels have
		// almost no history - so the mask still said "hit" where the radiance had
		// gone dark, suppressing the environment and leaving a hard dark rim that
		// faded as history rebuilt. Measured: with r_ssrDenoise 0 the rims
		// largely vanish, which is the signature of exactly that mismatch.
		//
		// Feeding the environment in per-ray means the signal NRD receives is
		// already complete, so there is no unfiltered weight left to disagree
		// with it, and the hit/miss transition gets denoised like everything
		// else. Note this is only safe because the deferred pass no longer adds
		// environment when composing - doing this while it did would trade black
		// panes for double-bright ones.
		if (g_iblComposeInResolve > 0.5f)
		{
			// Report a hit so the resolve adds nothing further for this pixel.
			// Pixels SSR never traced at all (matte, sky) keep confidence 0 and
			// are still filled by the resolve - that boundary follows whole
			// surfaces rather than cutting through one, so it has no edge to
			// shimmer.
			didReflect = true;
			hitDistance = 8.0f;
			pathId = 1.0f;

			const float envRoughness = saturate(perceptualRoughness);

			float3 env = SampleEnvAtlas(g_ssrSkyEnvAtlas, g_textureSampler, rayDir, envRoughness)
				* saturate(rayDir.y * 3.0f + 0.35f) * g_iblSkySpecular;

			// Probes replace the sky inside their box, matching
			// EnvMapCommon::EvaluateEnvSpecular so the two estimates agree.
			const float w1 = ProbeWeight(worldPos, g_probeCenter,  g_probeExtents);
			const float w2 = ProbeWeight(worldPos, g_probeCenter2, g_probeExtents2);
			const float wSum = w1 + w2;
			if (wSum > 0.0f)
			{
				const float coverage = saturate(wSum);
				float3 probeEnv = 0.0f.xxx;
				if (w1 > 0.0f)
					probeEnv += (w1 / wSum) * SampleEnvAtlas(g_ssrProbeAtlas, g_textureSampler,
						ProbeSpecularDir(rayDir, worldPos, g_probeCenter, g_probeExtents), envRoughness);
				if (w2 > 0.0f)
					probeEnv += (w2 / wSum) * SampleEnvAtlas(g_ssrProbeAtlas2, g_textureSampler,
						ProbeSpecularDir(rayDir, worldPos, g_probeCenter2, g_probeExtents2), envRoughness);

				env = lerp(env, probeEnv * g_iblParams.z, coverage);
			}

			return float4(env, 1.0f);
		}

		// Miss path - cone-trace the voxel GI clipmaps in the ray direction for an indirect-
		// bounce fallback.
		//
		// Previously, when the raymarch loop exhausted INSIDE the screen (hit.didFallback=true),
		// we blended 65% lastInScreenTex beauty with 35% voxel GI. The intent was to keep some
		// "screen-space hit information when it's available" - but lastInScreenTex is only
		// meaningful when the ray was close to a real hit (e.g. ran out of refinement budget).
		// When the ray ran its full step budget through empty space (e.g. a road pixel
		// reflecting upward, the ray marches up-and-forward through empty air for 28 steps,
		// stays in-screen the whole time because there's nothing above to exit it past), the
		// "last in-screen tex" is just whatever screen pixel happened to be sampled at iteration
		// 27 - directionally meaningless and per-pixel coherent (every road pixel sees the same
		// vertical column smeared down).
		//
		// The diffuse path explicitly skips didFallback for this exact reason
		// (RaymarchReflection comment also flags the "long stripe artifact"). Specular must
		// do the same: hand off to the voxel GI cone trace uniformly for any non-real-hit case.
		// The cone trace is a genuine directional radiance estimate from the same ray direction,
		// which is the right answer for "no screen-space hit found".
		float traceDistance = 0.0f;
		const float3 giRadiance = ConeTraceVoxelGI(worldPos + worldNormal * 0.25f, rayDir, traceDistance);

		didReflect = true;
		hitDistance = max(traceDistance, 8.0f);
		pathId = 2.0f;
		return float4(giRadiance, 1.0f);
	}

	SSROut ShaderMain(UIPixelInput input)
	{
		SSROut ssr = (SSROut)0;

		const float2 screenPosCanonical = float2(input.position.x / (float)g_screenWidth, input.position.y / (float)g_screenHeight);

		// Compensate the source gbuffer reads for TAA jitter. The gbuffer was rasterised with
		// `clip.xy += g_jitterOffsets * w` in the vertex shader, so the canonical world point
		// at screenPosCanonical actually has its data stored at screenPosCanonical + jitterUv
		// in the gbuffer this frame (and that position rotates frame-to-frame as the Halton
		// jitter rotates). Reading at the un-jittered screenPos gives the data of a slightly
		// different world point each frame, which makes the SSR ray's start position wobble
		// sub-pixel between frames and is the upstream source of the shimmer water.shader
		// avoids by not applying TAA jitter to its own geometry.
		// jitterUv: X follows clip directly, Y flips because clip-Y-up -> screen-UV-Y-down.
		const float2 jitterUv = float2(g_jitterOffsets.x * 0.5f, -g_jitterOffsets.y * 0.5f);
		const float2 screenPos = screenPosCanonical;// + jitterUv;

		// Material and instance data must stay point-sampled - pixelDiffuse.w encodes the
		// instance ID as a float (nonsensical to interpolate) and pixelSpecular packs per-pixel
		// material flags that shouldn't be blurred across material boundaries.
		const float4 pixelSpecular = GBUFFER_SPECULAR.Sample(g_pointSampler, screenPos);
		const float4 pixelDiffuse  = GBUFFER_DIFFUSE.Sample(g_pointSampler, screenPos);
		// Normal+depth and worldPos use linear sampling for sub-texel smoothing on top of the
		// jitter compensation. Renormalise the normal after the blend.
		const float4 pixelNormalRaw = GBUFFER_NORMAL.Sample(g_textureSampler, screenPos);
		const float4 pixelNormal = float4(normalize(pixelNormalRaw.xyz), pixelNormalRaw.w);
		const float4 pixelPosWS  = GBUFFER_POSITION.Sample(g_textureSampler, screenPos);

		const float smoothness = pixelSpecular.b;
		// Roughness is its OWN gbuffer channel here, independent of smoothness.
		const float perceptualRoughness = clamp(pixelSpecular.g, 0.04f, 1.0f);
		const float metalness = pixelSpecular.r;
		const float3 diffuseSurfaceColour = saturate(pixelDiffuse.rgb);

		// Skip SSR for sky pixels and pure-matte surfaces. Voxel GI already handles indirect
		// lighting for matte surfaces; firing diffuse SSR rays on every matte pixel produces
		// variance that NRD can't fully smooth and would just stamp texture detail back over GI.
		if (pixelNormal.w == g_frustumDepths[3] || smoothness <= 0.0f)
			return ssr;

		const uint instanceID = (uint)pixelDiffuse.w;
		const uint2 numPixels = uint2(g_screenWidth, g_screenHeight);
		const uint2 pixelCoord = uint2(screenPos * numPixels);
		const uint pixelIndex = pixelCoord.y * numPixels.x + pixelCoord.x;
		const float3 eyeVector = normalize(pixelPosWS.xyz - g_eyePos.xyz);

		// PBR weighting: Fresnel-Schlick using F0 from metalness/base-colour.
		// - F0 = lerp(0.04, baseColour, metalness): non-metals reflect ~4% white at normal incidence;
		//   metals reflect their tinted base colour.
		// - Fresnel = F0 + (1-F0) * (1-NdotV)^5: increases toward 1 at grazing angles.
		// - Specular contribution scales by Fresnel (intensity AND tint).
		// - Diffuse contribution scales by (1 - Fresnel) * (1 - metalness): metals have no diffuse,
		//   non-metals share energy with specular via Fresnel.
		// This replaces the old `specularWeight = smoothness` (which made smoothness an intensity
		// slider instead of a roughness control) and gives metals a tinted reflection that
		// changes with viewing angle instead of just turning diffuse off.
		const float3 F0 = lerp(0.04f.xxx, diffuseSurfaceColour, metalness);
		const float NdotV = saturate(dot(pixelNormal.xyz, -eyeVector));
		const float fresnelExp = pow(1.0f - NdotV, 5.0f);
		const float3 fresnel = F0 + (1.0f.xxx - F0) * fresnelExp;
		const float3 specularWeight = fresnel;
		const float3 diffuseWeightRGB = ((1.0f.xxx - fresnel) * (1.0f -  metalness));
		// Scalar threshold for the diffuse-ray gate, using the luminance of the weight.
		const float diffuseWeightLuma = dot(diffuseWeightRGB, float3(0.2126f, 0.7152f, 0.0722f));

		// Pixel-deterministic, frame-stable noise lookup. The earlier `+ frac(g_time) * 100.0f`
		// rotated the sample each frame, which makes sense if you have a temporal denoiser that
		// can average over the rotated samples - but with NRD off (or struggling) the rotation
		// shows as per-frame shimmer on glossy reflections (each frame's GGX-cone sample picks
		// a different direction, hitting a different part of the wall). Removing the time term
		// gives each pixel one fixed sample direction in the cone; NRD's (or TAA's) SPATIAL
		// filter then integrates the cone across neighbours, while the temporal axis stays
		// stable. Trade-off: slower convergence inside any single pixel's lobe, but no shimmer.
		float2 noiseSamplePos = screenPos * 128.0f;
		const float3 noise = g_noiseTexture.Sample(g_pointSampler, noiseSamplePos).rgb;

		uint baseRngState = pixelIndex + 719393u + (uint)(noise.r * 3654.0f) + (uint)(noise.g * 1232.0f) + (uint)(noise.b * 1540.0f);
		const float depth = pixelNormal.w;

		float3 diffuseAccum = 0.0f.xxx;
		float3 specularAccum = 0.0f.xxx;
		float diffuseHitDistAccum = 0.0f;
		float specularHitDistAccum = 0.0f;
		float diffuseSamples = 0.0f;
		float specularSamples = 0.0f;
		// Diagnostics for r_ssrDebugSkyHits. Which path the specular ray took and
		// what radiance it produced, so "the reflection is black here" can be
		// answered with "the ray missed and the environment it fell back to is
		// this dark" instead of a guess.
		float specPathId = 0.0f;
		float2 specHitUv = float2(0.0f, 0.0f);
		float3 specDebugRadiance = 0.0f.xxx;
		// Fraction of this pixel's specular rays that found real screen-space data.
		// The resolve blends the environment in by (1 - this), so a pixel whose ray
		// left the screen or passed through glass gets the environment rather than
		// the black that a failed march used to leave behind.
		float specularConfidenceAccum = 0.0f;

		// Diffuse SSR: one stochastic hemisphere ray per pixel per frame. The diffuse path in
		// GetReflection contributes only the screen-space DELTA over DiffuseGI's voxel-cone
		// baseline (see the long comment there), so this loop is safe to leave at 1 - it does
		// NOT double-count voxel GI even though DiffuseGI runs immediately before SSR.
		// More rays per pixel would converge faster but NRD's spatial+temporal denoising on
		// the diffuse channel already integrates across pixels and frames, so 1 is enough.
		const uint DiffuseRays = 1u;
		const uint SpecularRays = 1u;

		// Gate diffuse SSR on Fresnel-derived diffuse weight luminance. Skip pure metals and
		// highly Fresnel-dominated grazing pixels where the diffuse contribution to the final
		// composite is negligible; the threshold is low because diffuse SSR contributes only
		// a screen-space delta over DiffuseGI now, not a full integration, so it's cheap and
		// safe to fire on most diffuse surfaces.
		if (diffuseWeightLuma > 0.05f)
		{
			uint rng = baseRngState ^ 0x68bc21ebu;
			// DiffuseRays is a const 1 right now so HLSL release-mode flags
			// the [loop] attribute as overkill (the compiler proves the loop
			// runs exactly once). Dropped - if we bump DiffuseRays > 1 later
			// the compiler will unroll a small fixed count cleanly without it.
			for (uint i = 0; i < DiffuseRays; ++i)
			{
				bool didReflect = false;
				float hitDistance = 0.0f;
				float diffusePathIdUnused = 0.0f;
				float2 diffuseHitUvUnused = float2(0.0f, 0.0f);
				float4 reflected = GetReflection(
					eyeVector,
					pixelPosWS.xyz,
					pixelNormal.xyz,
					depth,
					didReflect,
					hitDistance,
					rng,
					smoothness,
					perceptualRoughness,
					false,
					instanceID,
					diffusePathIdUnused,
					diffuseHitUvUnused);

					//if(didReflect)
					{
						diffuseAccum += reflected.rgb;
						diffuseHitDistAccum += hitDistance;
						diffuseSamples += 1.0f;
					}
			}
		}

		// Spec rays fire whenever the Fresnel reflectance is non-trivial. F0 >= 0.04 for any
		// surface (Schlick floor), so this is always true for non-sky/non-matte pixels - which
		// is what we want: even mostly-diffuse surfaces have a small specular response.
		if (true)
		{
			uint rng = baseRngState ^ 0x2c1b3c6du;
			[loop]
			for (uint i = 0; i < SpecularRays; ++i)
			{
				bool didReflect = false;
				float hitDistance = 0.0f;
				float4 reflected = GetReflection(
					eyeVector,
					pixelPosWS.xyz,
					pixelNormal.xyz,
					depth,
					didReflect,
					hitDistance,
					rng,
					smoothness,
					perceptualRoughness,
					true,
					instanceID,
					specPathId,
					specHitUv);

				//if(didReflect)
				{
					specularAccum += reflected.rgb;
					specularHitDistAccum += hitDistance;
					specularSamples += 1.0f;
					specularConfidenceAccum += didReflect ? 1.0f : 0.0f;
					specDebugRadiance = reflected.rgb;
				}
			}
		}

		const float3 diffuseRadiance = diffuseSamples > 0.0f ? (diffuseAccum / diffuseSamples) : 0.0f.xxx;
		const float3 specularRadiance = specularSamples > 0.0f ? (specularAccum / specularSamples) : 0.0f.xxx;
		const float averageDiffuseHitDistance = diffuseSamples > 0.0f ? (diffuseHitDistAccum / diffuseSamples) : 0.0f;
		const float averageSpecularHitDistance = specularSamples > 0.0f ? (specularHitDistAccum / specularSamples) : 0.0f;

		// NRD's RELAX expects radiance + hit distance per channel. Pre-modulate by the Fresnel-
		// derived weights so the composite (additive blit) puts the right brightness/tint on
		// each surface. Specular: weighted by Fresnel (metals get tinted reflection, viewing
		// angle ramps the intensity). Diffuse: weighted by (1-Fresnel)*(1-metalness)*albedo
		// (energy-conserving, metals have no diffuse).
		ssr.diff = float4(diffuseRadiance * diffuseSurfaceColour * diffuseWeightRGB, diffuseSamples > 0.0f ? 1.0f : 0.0f);
		//ssr.diff = float4(pixelNormal.rgb, 1.0f);
		ssr.diffHitInfo = float4(0.0f, 0.0f, 0.0f, averageDiffuseHitDistance);
		ssr.spec = float4(specularRadiance * specularWeight, specularSamples > 0.0f ? 1.0f : 0.0f);
		// .r carries the screen-space confidence for SSRResolve's environment
		// composition; NRD reads only .w of this target (see NRDInterface's
		// preprocess, which packs specularHitDistance.w), so .rgb are free.
		const float specularConfidence =
			specularSamples > 0.0f ? (specularConfidenceAccum / specularSamples) : 0.0f;
		ssr.specHitInfo = float4(specularConfidence, 0.0f, 0.0f, averageSpecularHitDistance);

		// r_ssrDebugSkyHits, written last so it overrides the real output.
		//   1 = path classify: RED real screen hit / GREEN environment fill /
		//       BLUE voxel-GI fallback / YELLOW water-style in-screen fallback.
		//       Answers "did this pixel miss, and how did it recover?".
		//   2 = the specular radiance the ray produced, x50. Answers "and how
		//       bright was what it fell back to?" - a black result under mode 2
		//       with green under mode 1 means the environment itself is dark,
		//       not that the fallback failed to run.
		if (g_ssrDebugSkyHits > 0.5f)
		{
			if (g_ssrDebugSkyHits < 1.5f)
			{
				const float3 classify = (specPathId < 0.5f) ? float3(1, 0, 0)
					: (specPathId < 1.5f) ? float3(0, 1, 0)
					: (specPathId < 2.5f) ? float3(0, 0, 1) : float3(1, 1, 0);
				ssr.spec = float4(classify, 1.0f);
			}
			else if (g_ssrDebugSkyHits < 2.5f)
			{
				ssr.spec = float4(specDebugRadiance * 50.0f, 1.0f);
			}
			else
			{
				// Mode 3: WHERE the ray landed, as screen UV. Red = horizontal,
				// green = vertical. A reflection can be a genuine hit and still
				// be reading the wrong part of the screen.
				//
				// CAVEAT, and it matters: every mode here is written into
				// ssr.spec, which then goes through the resolve's composite AND
				// the tonemapper before it reaches a screenshot. Mode 1 survives
				// that because it only needs which of four colours is largest,
				// but modes 2 and 3 are QUANTITIES - read them back off a
				// captured frame and you get tonemapped values, not the numbers
				// this shader wrote. Reading exact UVs needs a debug path that
				// writes the final image directly, bypassing the tonemap.
				ssr.spec = float4(specHitUv.x, specHitUv.y, 0.0f, 1.0f);
			}
			ssr.diff = 0.0f.xxxx;
		}

		return ssr;
	}
}
