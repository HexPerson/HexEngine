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
	PBRutils
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
	// Clustered light apply (Phase 2, slice 2).
	//
	// One fullscreen pass shading every UNSHADOWED point and spot light from
	// the cluster lists, additively into the light accumulation buffer -
	// replacing hundreds of per-light sphere draws with one. Shadowed lights
	// stay on the per-light path (they need per-light shadow maps bound), so
	// lights flagged shadowed in the buffer are skipped here and unshadowed
	// ones are skipped there.
	//
	// Every model choice mirrors PointLight.shader / SpotLight.shader exactly,
	// so a light moving between the paths must not change appearance:
	//   attenuation  = saturate(1-(d/r)^4)^2 / max(d^2, 0.01^2)   (UE4/Frostbite window)
	//   spot cone    = smoothstep(cosOuter, max(cosInner, cosOuter+1e-4), coneDot)
	//   surface      = CalculatePBRPointLighting(..., depthValue = 1)
	// The one legitimate difference: no per-pixel volumetric march here - the
	// froxel volume owns fog, and the plan retires the inline march anyway.

	GBUFFER_RESOURCE(0, 1, 2, 3, 4);

	struct GpuLight
	{
		float4 posRadius;
		float4 colorStrength;
		float4 dirCone;
		float4 params; // x cos(inner), y type (0 point, 1 spot), z shadowed
	};

	// Raw-bound by SceneRenderer at explicit slots (the engine API has no PS
	// structured-buffer bind).
	StructuredBuffer<GpuLight> g_clLights : register(t21);
	StructuredBuffer<uint>     g_clCounts : register(t22);
	StructuredBuffer<uint>     g_clLists  : register(t23);

	SamplerState g_textureSampler : register(s0);
	SamplerComparisonState g_cmpSampler : register(s1);
	SamplerState g_pointSampler : register(s2);

	static const uint  kClustersX = 16;
	static const uint  kClustersY = 9;
	static const uint  kClustersZ = 32;
	static const uint  kMaxLightsPerCluster = 64;
	static const float kNearPlaneM = 0.25f;
	static const float kFarDepthM  = 128.0f;

	cbuffer ClusterConstants : register(b5)
	{
		matrix g_clusterViewMatrix;
		float4 g_clusterScreenParams;
	};

	uint DepthToSlice(float depth)
	{
		const float w = log(max(depth, kNearPlaneM) / kNearPlaneM) / log(kFarDepthM / kNearPlaneM);
		return min((uint)(saturate(w) * (float)kClustersZ), kClustersZ - 1);
	}

	float4 ShaderMain(UIPixelInput input) : SV_TARGET
	{
		const float2 screenPos = float2(
			input.position.x / (float)g_screenWidth,
			input.position.y / (float)g_screenHeight);

		// Flood test (r_clusterApplyDebug): unconditional magenta proves the
		// draw itself - render target, blend, viewport, shader bind - before
		// any cluster or light logic gets a chance to zero the output.
		if (g_clusterScreenParams.w > 0.5f)
			return float4(10.0f, 0.0f, 10.0f, 0.0f);

		const float4 pixelNormal = GBUFFER_NORMAL.Sample(g_pointSampler, screenPos);
		const float4 pixelPosWS  = GBUFFER_POSITION.Sample(g_pointSampler, screenPos);

		// Sky: both spellings, matching the deferred pass and SSR.
		if (pixelPosWS.a > 0.0f || pixelNormal.w == g_frustumDepths[3])
			return float4(0.0f.xxx, 0.0f);

		const float4 pixelColour = GBUFFER_DIFFUSE.Sample(g_pointSampler, screenPos);
		const float3 normalWS = normalize(pixelNormal.xyz);

		const uint cx = min((uint)(screenPos.x * (float)kClustersX), kClustersX - 1);
		const uint cy = min((uint)(screenPos.y * (float)kClustersY), kClustersY - 1);
		const uint cz = DepthToSlice(pixelNormal.w);
		const uint clusterIdx = (cz * kClustersY + cy) * kClustersX + cx;

		const uint count = min(g_clCounts[clusterIdx], kMaxLightsPerCluster);

		float3 accum = 0.0f.xxx;

		[loop]
		for (uint i = 0; i < count; ++i)
		{
			const GpuLight light = g_clLights[g_clLists[clusterIdx * kMaxLightsPerCluster + i]];

			// Shadowed lights are the per-light path's job.
			if (light.params.z > 0.5f)
				continue;

			float3 lightToPixelVec = pixelPosWS.xyz - light.posRadius.xyz;
			const float d = length(lightToPixelVec);
			const float lightRange = light.posRadius.w;
			if (d >= lightRange || d <= 0.0f)
				continue;
			lightToPixelVec /= d;

			const float minDistSqr = 0.01f * 0.01f;
			float distanceFalloff = saturate(1.0f - pow(d / lightRange, 4.0f));
			distanceFalloff *= distanceFalloff;
			float attenuation = distanceFalloff / max(d * d, minDistSqr);

			// Spot cone, same smoothstep as SpotLight.shader.
			if (light.params.y > 0.5f)
			{
				const float coneDot = dot(lightToPixelVec, light.dirCone.xyz);
				attenuation *= smoothstep(
					light.dirCone.w,
					max(light.params.x, light.dirCone.w + 1e-4f),
					coneDot);
			}

			if (attenuation <= 0.0f)
				continue;

			const float4 pbr = CalculatePBRPointLighting(
				GBUFFER_SPECULAR,
				g_pointSampler,
				screenPos,
				normalWS,
				pixelPosWS.xyz,
				lightToPixelVec,
				light.colorStrength.rgb * light.colorStrength.w,
				pixelColour.rgb,
				1.0f, // unshadowed by definition on this path
				attenuation);

			accum += pbr.rgb;
		}

		return float4(accum, 0.0f);
	}
}
