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

	float3 RandomDirectionInDirectionOfNormal(float3 normal, inout uint state)
	{
		float x = RandomValueNormalDistribution(state);
		float y = RandomValueNormalDistribution(state);
		float z = RandomValueNormalDistribution(state);

		float3 generated = normalize(float3(x, y, z));

		if (dot(generated, normal) < 0.0f)
			generated = -generated;

		return generated;
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

	// Sample voxel radiance at a world-space position by picking the finest clipmap that contains it.
	float3 SampleVoxelRadianceAtWorld(float3 worldPos)
	{
		[unroll]
		for (uint i = 0; i < 4; ++i)
		{
			const float3 clipCenter = g_clipCenterExtent[i].xyz;
			const float clipExtent = max(g_clipCenterExtent[i].w, 1e-3f);
			const float3 uvw = ((worldPos - clipCenter) / (clipExtent * 2.0f)) + 0.5f;
			if (IsInsideUnit(uvw))
			{
				const float4 voxel = SampleVoxelRadianceClip(i, uvw);
				return voxel.rgb * max(g_giParams0.x, 0.0f);
			}
		}
		return 0.0f.xxx;
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
		bool didHit;       // true if a real screen-space hit was found (sky or geometry)
		bool didHitSky;    // true if the screen-space hit was sky
		bool didFallback;  // true when we fell back to the last in-screen tex (not a true hit)
		float3 colour;     // radiance to write
		float hitDistance; // world-space distance from rayStart to the hit
	};

	HitResult RaymarchReflection(
		float3 rayStart,
		float3 rayDir,
		float3 sourceNormal,
		uint sourceInstanceID,
		float jitter)
	{
		HitResult result;
		result.didHit = false;
		result.didHitSky = false;
		result.didFallback = false;
		result.colour = 0.0f.xxx;
		result.hitDistance = 0.0f;

		// Push the start point off the source surface to avoid immediate self-intersection.
		// Use both a normal bias and a small along-ray bias so the first sample is unambiguously
		// off-surface even at grazing angles where the normal bias alone wouldn't help.
		const float3 origin = rayStart + sourceNormal * 0.25f + rayDir * 0.10f;

		const int stepCount = 28;
		const int refinementStepCount = 6;
		const float minStepLen = 0.6f;
		const float maxStepLen = 12.0f;

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
			const float thickness = lerp(0.6f, 4.0f, marchFraction);

			prevFragPos = fragPos;
			prevTotalDistance = totalDistance;

			// Add a sub-step jitter on the very first iteration to decorrelate neighbouring rays.
			const float firstStepScale = (i == 0) ? lerp(0.5f, 1.0f, jitter) : 1.0f;
			fragPos += rayDir * stepLen * firstStepScale;
			totalDistance += stepLen * firstStepScale;

			float4 fragView = mul(float4(fragPos, 1.0f), g_viewMatrix);
			float4 fragClip = mul(fragView, g_projectionMatrix);
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
			const bool didHitSky = (actualDepth == g_frustumDepths[3]);

			// Remember this in-screen sample for the loop-exhaustion fallback only.
			lastInScreenTex = fragTex;
			lastInScreenDistance = totalDistance;

			if (didHitSky)
			{
				const float3 skyColour = g_beautyTexture.Sample(g_pointSampler, fragTex).rgb;
				result.didHit = true;
				result.didHitSky = true;
				result.colour = skyColour;
				result.hitDistance = totalDistance;
				return result;
			}

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
					const bool midHitSky = (midActual == g_frustumDepths[3]);

					if (midHitSky || midDepth >= midActual)
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
				const float3 hitColour = g_beautyTexture.Sample(g_pointSampler, refinedTex).rgb;

				result.didHit = true;
				result.didHitSky = false;
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
			result.didHitSky = false;
			result.didFallback = true;
			result.colour = g_beautyTexture.Sample(g_pointSampler, lastInScreenTex).rgb;
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

		float3 rayDir;
		if (wantSpecular)
		{
			// Mirror direction perturbed by surface roughness. Smoothness=1 -> deterministic
			// mirror reflection (zero variance, denoiser-friendly). Lower smoothness widens
			// the cone via a cosine-weighted hemisphere offset around the mirror direction.
			const float3 specularDir = normalize(reflect(eyeDir, worldNormal));
			const float roughness = saturate(1.0f - smoothness);
			const float3 randomOffset = RandomDirectionInDirectionOfNormal(worldNormal, rngState);
			rayDir = normalize(specularDir + randomOffset * roughness * roughness);
		}
		else
		{
			// Cosine-weighted hemisphere around the surface normal for diffuse rays.
			rayDir = normalize(worldNormal + RandomDirectionInDirectionOfNormal(worldNormal, rngState));
		}

		const float jitter = RandomValue(rngState);

		const HitResult hit = RaymarchReflection(worldPos, rayDir, worldNormal, instanceID, jitter);

		if (hit.didHit && !hit.didFallback)
		{
			// True screen-space hit (sky or geometry) - report exact world-space distance.
			didReflect = true;
			hitDistance = max(hit.hitDistance, 0.0f);
			return float4(hit.colour, 1.0f);
		}

		// Either the ray fell back to the last in-screen sample, or it never had any.
		// Cone-trace voxel GI for a directional radiance estimate at the source point. We then
		// blend with the in-screen fallback (if present) so noise stays low and NRD has spatially
		// coherent data to work with.
		float traceDistance = 0.0f;
		const float3 giRadiance = ConeTraceVoxelGI(worldPos + worldNormal * 0.25f, rayDir, traceDistance);

		didReflect = true;

		if (hit.didFallback)
		{
			// Blend the last in-screen beauty sample with the voxel GI estimate. The beauty sample
			// gives smooth, locally coherent values; voxel GI fills in directional information.
			const float3 blended = lerp(giRadiance, hit.colour, 0.65f);
			hitDistance = max(hit.hitDistance, 1.0f);
			return float4(blended, 1.0f);
		}

		// Pure GI fallback - use the trace distance so NRD treats this as a far hit.
		hitDistance = max(traceDistance, 8.0f);
		return float4(giRadiance, 1.0f);
	}

	SSROut ShaderMain(UIPixelInput input)
	{
		SSROut ssr = (SSROut)0;

		const float2 screenPos = float2(input.position.x / (float)g_screenWidth, input.position.y / (float)g_screenHeight);

		const float4 pixelSpecular = GBUFFER_SPECULAR.Sample(g_pointSampler, screenPos);
		const float4 pixelDiffuse  = GBUFFER_DIFFUSE.Sample(g_pointSampler, screenPos);
		const float4 pixelNormal   = GBUFFER_NORMAL.Sample(g_pointSampler, screenPos);
		const float4 pixelPosWS    = GBUFFER_POSITION.Sample(g_pointSampler, screenPos);

		const float smoothness = pixelSpecular.b;
		const float metalness = pixelSpecular.r;
		const float specularProbability = pixelSpecular.a;
		const float diffuseWeight = saturate((1.0f - metalness) * (1.0f - smoothness));
		const float specularWeight = saturate(smoothness);
		const float3 diffuseSurfaceColour = saturate(pixelDiffuse.rgb);

		// Skip SSR for sky pixels and pure-matte surfaces. Voxel GI already handles indirect
		// lighting for matte surfaces; firing diffuse SSR rays on every matte pixel produces
		// variance that NRD can't fully smooth and would just stamp texture detail back over GI.
		// Glossy/specular surfaces (smoothness > 0) get the full diffuse+specular treatment via
		// GetReflection's per-component split below.
		if (pixelNormal.w == g_frustumDepths[3] || smoothness <= 0.0f)
			return ssr;

		const uint instanceID = (uint)pixelDiffuse.w;
		const uint2 numPixels = uint2(g_screenWidth, g_screenHeight);
		const uint2 pixelCoord = uint2(screenPos * numPixels);
		const uint pixelIndex = pixelCoord.y * numPixels.x + pixelCoord.x;
		const float3 eyeVector = normalize(pixelPosWS.xyz - g_eyePos.xyz);

		float2 noiseSamplePos = screenPos * 64.0f + frac(g_time) * 100.0f;
		const float3 noise = g_noiseTexture.Sample(g_pointSampler, noiseSamplePos).rgb;
		uint baseRngState = pixelIndex + 719393u + (uint)(noise.r * 3654.0f) + (uint)(noise.g * 1232.0f) + (uint)(noise.b * 1540.0f);
		const float depth = pixelNormal.w;

		float3 diffuseAccum = 0.0f.xxx;
		float3 specularAccum = 0.0f.xxx;
		float diffuseHitDistAccum = 0.0f;
		float specularHitDistAccum = 0.0f;
		float diffuseSamples = 0.0f;
		float specularSamples = 0.0f;

		const uint DiffuseRays = 1u;
		const uint SpecularRays = 1u;

		if (diffuseWeight > 0.0f)
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

				diffuseAccum += reflected.rgb;
				diffuseHitDistAccum += hitDistance;
				diffuseSamples += 1.0f;
			}
		}

		if (specularWeight > 0.0f)
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

				specularAccum += reflected.rgb;
				specularHitDistAccum += hitDistance;
				specularSamples += 1.0f;
			}
		}

		const float3 diffuseRadiance = diffuseSamples > 0.0f ? (diffuseAccum / diffuseSamples) : 0.0f.xxx;
		const float3 specularRadiance = specularSamples > 0.0f ? (specularAccum / specularSamples) : 0.0f.xxx;
		const float averageDiffuseHitDistance = diffuseSamples > 0.0f ? (diffuseHitDistAccum / diffuseSamples) : 0.0f;
		const float averageSpecularHitDistance = specularSamples > 0.0f ? (specularHitDistAccum / specularSamples) : 0.0f;

		// NRD's RELAX expects radiance + hit distance per channel. We pre-modulate the diffuse term
		// with surface albedo here (matches the existing resolve which sums radiance with no further
		// albedo modulation). The 'A' channel of the radiance buffer is used as a hit-mask hint.
		ssr.diff = float4(diffuseRadiance * diffuseSurfaceColour * diffuseWeight, diffuseSamples > 0.0f ? 1.0f : 0.0f);
		ssr.diffHitInfo = float4(0.0f, 0.0f, 0.0f, averageDiffuseHitDistance);
		ssr.spec = float4(specularRadiance * specularWeight, specularSamples > 0.0f ? 1.0f : 0.0f);
		ssr.specHitInfo = float4(0.0f, 0.0f, 0.0f, averageSpecularHitDistance);

		return ssr;
	}
}
