"ComputeShaderIncludes"
{
	Global
	Atmosphere
	AtmospherePhysical
	AtmosphereCommon
	EnvMapCommon
	RtCommon
	DDGICommon
}
"ComputeShader"
{
	// DDGI probe ray trace + shade. One thread group = one probe, one thread
	// = one ray. Probes updated this dispatch: slot = group * period + phase
	// (round-robin over the cascade; DDGI.cpp picks period/phase).
	//
	// Each ray returns (radiance, signed hit distance):
	//   miss      -> sky radiance (sky SH, same source as the deferred sky
	//                fill), distance = a large positive number
	//   back face -> radiance 0, distance = -t (probe classification /
	//                relocation read the sign)
	//   front     -> albedo * (sun + local lights + previous probe irradiance
	//                at the hit) + emissive; distance = t
	// Sun and local lights are SHADOW-RAY tested against the same BVH, so no
	// light reaches a surface through a wall.

	struct DdgiLight
	{
		float4 positionRange;   // xyz world position, w range (m)
		float4 directionCone;   // xyz spot direction, w cosOuter (spot)
		float4 colourIntensity; // rgb colour * intensity (engine units)
		float4 params;          // x type (0 point/tube, 1 spot, 2 rect: cosine emitter about directionCone.xyz), y cosInner, z two-sided, w asuint fixture key (0 none)
	};

	Texture2D g_ddgiSkySH : register(t65);
	StructuredBuffer<DdgiLight> g_ddgiLights : register(t66);
	RWStructuredBuffer<float4> g_ddgiRays : register(u0);
	SamplerState g_ddgiPointSampler : register(s2);

	float3 DdgiSkyRadiance(float3 dir)
	{
		// ShIrradiance is already "outgoing radiance of a white Lambertian
		// surface facing dir" (EnvMapCommon) - a smooth stand-in for sky
		// radiance along dir, in the same units the deferred sky fill uses.
		return DdgiSkyTint(max(ShIrradiance(g_ddgiSkySH, g_ddgiPointSampler, dir), 0.0f.xxx)) * g_ddgiParams1.x;
	}

	float3 DdgiSunIrradiance(float3 pos, float3 n)
	{
		const float3 toSun = -normalize(g_lightDirection.xyz);
		const float ndotl = dot(n, toSun);
		if (ndotl <= 0.0f || g_globalLight.x <= 0.0f)
			return 0.0f.xxx;
		// Same sun colour the deferred pass uses (Deferred.shader): physical
		// colour, luma-matched to the legacy colour, scaled by sun strength.
		const float3 legacy = getSunColour();
		float3 physical = ComputePhysicalSunColour(pos, toSun);
		const float legacyLuma = dot(legacy, float3(0.2126f, 0.7152f, 0.0722f));
		const float physicalLuma = max(dot(physical, float3(0.2126f, 0.7152f, 0.0722f)), 1e-4f);
		physical *= legacyLuma / physicalLuma;
		if (RtOccluded(pos + n * 0.02f, toSun, 1e4f))
			return 0.0f.xxx;
		return physical * ndotl * g_globalLight.x * g_ddgiParams1.w;
	}

	float3 DdgiLocalIrradiance(float3 pos, float3 n)
	{
		float3 sum = 0.0f.xxx;
		const uint count = (uint)g_ddgiParams1.z;
		[loop]
		for (uint i = 0; i < count; ++i)
		{
			const DdgiLight light = g_ddgiLights[i];
			float3 toLight = light.positionRange.xyz - pos;
			const float d2 = dot(toLight, toLight);
			const float range = light.positionRange.w;
			if (d2 >= range * range || d2 <= 1e-8f)
				continue;
			const float d = sqrt(d2);
			toLight /= d;
			const float ndotl = dot(n, toLight);
			if (ndotl <= 0.0f)
				continue;
			// PointLight.shader: windowed inverse square.
			float window = saturate(1.0f - pow(d / range, 4.0f));
			window *= window;
			float atten = window / max(d2, 1e-4f);
			if (light.params.x > 1.5f)
			{
				// Rect area light approximated at its centre: cosine emitter.
				const float facing = dot(-toLight, light.directionCone.xyz);
				atten *= (light.params.z > 0.5f) ? abs(facing) : saturate(facing);
			}
			else if (light.params.x > 0.5f)
			{
				const float coneDot = dot(-toLight, light.directionCone.xyz);
				atten *= smoothstep(light.directionCone.w, light.params.y, coneDot);
			}
			if (atten <= 1e-6f)
				continue;
			// params.w = the light's fixture key: its own lamp shade / bulb
			// mesh must not shadow it (DDGI.cpp FixtureKey).
			if (RtOccludedSkip(pos + n * 0.02f, toLight, d - 0.05f, asuint(light.params.w)))
				continue;
			sum += light.colourIntensity.rgb * ndotl * atten;
		}
		return sum * g_ddgiParams3.x;
	}

	[numthreads(128, 1, 1)]
	void ShaderMain(uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID)
	{
		const uint cascade = g_ddgiUpdate.x;
		const uint period = max(g_ddgiUpdate.y, 1u);
		const uint slot = gid.x * period + g_ddgiUpdate.z;
		const uint rayIndex = gtid.x;
		const uint outIndex = gid.x * kDdgiRaysPerProbe + rayIndex;
		if (slot >= (uint)g_ddgiCascade[cascade].dims.w)
		{
			g_ddgiRays[outIndex] = float4(0.0f, 0.0f, 0.0f, 1e27f);
			return;
		}

		const int3 g = DdgiSlotToGrid(cascade, slot);
		const DdgiProbeState st = DdgiLoadState(cascade, slot);
		const bool fresh = (st.coord.w == 0) || any(st.coord.xyz != g);
		const float3 offset = fresh ? 0.0f.xxx : st.offsetState.xyz;
		const float3 origin = DdgiGridToWorld(cascade, g) + offset;
		const float3 dir = DdgiRayDirection(rayIndex);
		const float maxDist = g_ddgiCascade[cascade].spacing.y;

		const RtHit hit = RtTrace(origin, dir, maxDist, false);
		if (hit.instance == 0xffffffffu)
		{
			// Ground-plane stand-in: the voxel terrain renders from GPU-only
			// surfaces and is not in the BVH, so a downward miss would read the
			// sky from below the ground. Hit a plane at the local terrain height
			// instead (meshes - roads, pavements, buildings - still win where
			// they exist, being real BVH hits).
			if (g_ddgiGround.y > 0.5f && dir.y < -1e-3f && origin.y > g_ddgiGround.x)
			{
				const float tg = (g_ddgiGround.x - origin.y) / dir.y;
				if (tg < maxDist)
				{
					const float3 gp = origin + dir * tg;
					const float3 gn = float3(0.0f, 1.0f, 0.0f);
					float3 gIrr = DdgiSunIrradiance(gp, gn) + DdgiLocalIrradiance(gp, gn);
					gIrr += DdgiSampleIrradiance(gp, gn, -dir) * g_ddgiParams0.w;
					g_ddgiRays[outIndex] = float4(max(g_ddgiGroundAlbedo.rgb * gIrr, 0.0f.xxx), tg);
					return;
				}
			}
			g_ddgiRays[outIndex] = float4(DdgiSkyRadiance(dir), 1e27f);
			return;
		}

		const RtSurface s = RtGetSurface(hit, origin, dir);
		if (!s.frontFace)
		{
			// Back face: the probe sees the inside of geometry.
			g_ddgiRays[outIndex] = float4(0.0f, 0.0f, 0.0f, -hit.t);
			return;
		}

		const float3 n = s.normal;
		float3 irradiance = DdgiSunIrradiance(s.position, n) + DdgiLocalIrradiance(s.position, n);
		// Infinite bounce: previous probe irradiance at the hit. Gain < 1 by
		// construction (albedo < 1, multiplier clamped <= 1 on the CPU).
		irradiance += DdgiSampleIrradiance(s.position, n, -dir) * g_ddgiParams0.w;
		const float3 radiance = s.albedo * irradiance + s.emissive * g_ddgiParams1.y;
		g_ddgiRays[outIndex] = float4(max(radiance, 0.0f.xxx), hit.t);
	}
}
