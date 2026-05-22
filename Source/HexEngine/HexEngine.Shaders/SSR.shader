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
	};

	HitResult RaymarchReflection(
		float3 rayStart,
		float3 rayDir,
		float3 sourceNormal,
		uint sourceInstanceID,
		float jitter,
		float rayRoughness)
	{
		HitResult result;
		result.didHit = false;
		result.didFallback = false;
		result.colour = 0.0f.xxx;
		result.hitDistance = 0.0f;

		// Push the start point off the source surface to avoid immediate self-intersection.
		// Use both a normal bias and a small along-ray bias so the first sample is unambiguously
		// off-surface even at grazing angles where the normal bias alone wouldn't help.
		const float3 origin = rayStart + sourceNormal * 0.25f + rayDir * 0.10f;

		const int stepCount = 28;
		const int refinementStepCount = 6;
		const float minStepLen = 0.3f;
		const float maxStepLen = 3.0f;

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
		bool exitedScreen = false;

		[loop]
		for (int i = 0; i < stepCount; ++i)
		{
			const float marchFraction = saturate((float)i / (float)(stepCount - 1));
			const float stepLen = lerp(minStepLen, maxStepLen, marchFraction * marchFraction);

			// Thickness grows with distance from the camera so distant pixels still register;
			// keep it tight near the source to avoid false self-hits.
			const float thickness = lerp(0.6f, 2.0f, marchFraction);

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

			const float4 normalDepth = GBUFFER_NORMAL.Sample(g_pointSampler, fragTex);
			const float actualDepth = normalDepth.w;

			// Remember this in-screen sample for the loop-exhaustion fallback only.
			lastInScreenTex = fragTex;
			lastInScreenDistance = totalDistance;

			// Note: we deliberately do NOT special-case sky pixels here. Sky's actualDepth is
			// the frustum-far value (very large), so the depth check below naturally rejects
			// "hits" on sky pixels - the ray's depth never gets close enough. This matches
			// water.shader. The previous didHitSky early-return was the cause of the bright
			// blue streaks: vertical rays from the floor immediately saw sky-through-window
			// pixels and returned sky colour, before the ray had any chance to actually reach
			// the wall geometry in 3D.

			// Ray has passed behind the surface within the thickness window - candidate hit.
			const float depthDelta = fragDepth - actualDepth;
			if (depthDelta > 0.0f && depthDelta < thickness)
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

					const float midActual = GBUFFER_NORMAL.Sample(g_pointSampler, midTex).w;

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
				const uint hitInstance = (uint)GBUFFER_DIFFUSE.Sample(g_pointSampler, refinedTex).w;
				if (hitInstance == sourceInstanceID && refinedDistance < 4.0f)
					continue;

				const float4 hitPosWS = GBUFFER_POSITION.Sample(g_pointSampler, refinedTex);
				// Linear-sample the beauty at the hit point. Note: the beauty texture is rendered
				// with TAA jitter per-vertex, so its sub-pixel content rotates frame-to-frame
				// through the 16-sample Halton sequence. No single-frame sample is fully stable;
				// linear sampling smooths sub-pixel boundaries at high-contrast edges (windows
				// vs frames) but residual frame-to-frame variance is inherent to reading a
				// jittered texture at a non-jittered projection result. The proper resolver for
				// that residual variance is NRD's temporal accumulation (or TAA on the SSR
				// output) - shader-side single-frame tricks (jitter UV offset etc.) just trade
				// one set of sub-pixel weights for another.
				const float3 hitColour = g_beautyTexture.Sample(g_textureSampler, refinedTex).rgb;

				result.didHit = true;
				result.colour = hitColour;
				result.hitDistance = max(length(hitPosWS.xyz - rayStart), refinedDistance);
				return result;
			}
		}

		// No real screen-space hit. Only use the water.shader last-in-screen-tex fallback when
		// the loop exhausted INSIDE the screen (i.e. the ray was still progressing across valid
		// geometry but ran out of step budget). For rays that exited screen, leave didFallback
		// false and rely on the pure voxel-GI cone trace in GetReflection - that direction is
		// genuinely "outside what we can see", so the last in-screen tex is meaningless and
		// would produce the stripe artifact along surfaces whose reflections all exit the same
		// screen edge.
		if (!exitedScreen && lastInScreenTex.x >= 0.0f)
		{
			result.didHit = true;
			result.didFallback = true;
			result.colour = g_beautyTexture.Sample(g_textureSampler, lastInScreenTex).rgb;
			result.hitDistance = max(lastInScreenDistance, 1.0f);
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
		bool wantSpecular,
		uint instanceID)
	{
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
			const HitResult hit = RaymarchReflection(worldPos, diffuseDir, worldNormal, instanceID, jitter, 1.0f);

			

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

		const HitResult hit = RaymarchReflection(worldPos, rayDir, worldNormal, instanceID, jitter, rayRoughness);

		if (hit.didHit && !hit.didFallback)
		{
			// True screen-space hit (sky or geometry) - report exact world-space distance.
			didReflect = true;
			hitDistance = max(hit.hitDistance, 0.0f);
			return float4(hit.colour, 1.0f);
		}

		// Miss path - cone-trace the voxel GI clipmaps in the ray direction for an indirect-
		// bounce fallback. Both the in-screen loop-exhaustion and screen-exit cases hit this.
		// For loop-exhaustion (didFallback=true) we still have a last in-screen beauty value
		// from the march, which is directionally accurate; blend it with the voxel trace so
		// we keep that screen-space hit information when it's available. For pure miss we
		// just use the voxel trace alone.
		//
		// Note: prior versions wrote zero-alpha here to avoid NRD's 3x3 minHitDist sampling
		// leaking miscomputed virtual reprojection positions onto fallback pixels. The voxel
		// trace gives a real radiance from the ray direction so it's a closer match to what
		// NRD's spec virtual-MV path assumes (signal lives along the ray at hitDistance), and
		// the cone trace's own distance fits that better than the previous source-beauty
		// fallback ever did.
		float traceDistance = 0.0f;
		const float3 giRadiance = ConeTraceVoxelGI(worldPos + worldNormal * 0.25f, rayDir, traceDistance);

		didReflect = true;

		if (hit.didFallback)
		{
			const float3 blended = lerp(giRadiance, hit.colour, 0.65f);
			hitDistance = max(hit.hitDistance, 1.0f);
			return float4(blended, 1.0f);
		}

		hitDistance = max(traceDistance, 8.0f);
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
			[loop]
			for (uint i = 0; i < DiffuseRays; ++i)
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
					false,
					instanceID);

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
					true,
					instanceID);

				//if(didReflect)
				{
					specularAccum += reflected.rgb;
					specularHitDistAccum += hitDistance;
					specularSamples += 1.0f;
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
		ssr.specHitInfo = float4(0.0f, 0.0f, 0.0f, averageSpecularHitDistance);

		return ssr;
	}
}
