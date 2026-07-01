"InputLayout"
{
	PosNormTanBinTex_INSTANCED
}
"VertexShaderIncludes"
{
	MeshCommon
}
"PixelShaderIncludes"
{
	MeshCommon
}
"VertexShader"
{
	MeshPixelInput ShaderMain(MeshVertexInput input, MeshInstanceData instance)
	{
		MeshPixelInput output = (MeshPixelInput)0;

		input.position.w = 1.0f;

		output.position = mul(input.position, instance.world);
		output.positionWS = output.position;
		output.position = mul(output.position, g_viewProjectionMatrix);
		// positionWS.w is unused by the pixel shader's lighting (it reads
		// .xyz) - carry the per-instance texture slot index in it. The sim
		// shader stamps the index into worldPrev[3].w from emitter flags.
		output.positionWS.w = instance.worldPrev[3].w;

		output.texcoord = input.texcoord * instance.uvScale + instance.worldPrev[3].xy;

		output.normal = mul(input.normal, (float3x3)instance.world);
		output.normal = normalize(output.normal);

		output.tangent = mul(input.tangent, (float3x3)instance.world);
		output.tangent = normalize(output.tangent);

		output.binormal = mul(input.binormal, (float3x3)instance.world);
		output.binormal = normalize(output.binormal);

		output.viewDirection.xyz = g_eyePos.xyz - output.positionWS.xyz;
		output.viewDirection.xyz = normalize(output.viewDirection.xyz);
		output.viewDirection.w = instance.worldPrev[3].z;
		output.colour = instance.colour;

		return output;
	}
}
"PixelShader"
{
	// Up to 4 per-emitter particle textures (ParticleWorldSystem's
	// kMaxParticleTextures). Slots 1..3 reuse the bind positions of maps
	// this shader never sampled (normal/roughness/metallic). The instance's
	// texture index arrives in positionWS.w.
	Texture2D g_albedoMap  : register(t0);
	Texture2D g_albedoMap1 : register(t1);
	Texture2D g_albedoMap2 : register(t2);
	Texture2D g_albedoMap3 : register(t3);
	// Froxel-fog INTEGRATION volume at t8 - the temporally-accumulated
	// (EMA-smoothed) product, NOT the raw scatter volume. One sample, two jobs:
	//   .a   = transmittance to the particle's depth (self-fogging, so a flake
	//          attenuates at its OWN depth instead of the opaque surface behind)
	//   .rgb = accumulated inscatter; .rgb/(1-.a) = MEAN fog radiance, used as
	//          the particle light probe.
	// An earlier build probed the raw SCATTER volume, which is re-jittered every
	// frame for the integrate pass's temporal supersampling: a static mist puff
	// cycled through ~16 sub-froxel samples and visibly PULSED, with hard per-
	// froxel shadow edges. The integration volume is stable and ray-accumulated.
	Texture3D g_volumetricFogLUT : register(t8);
	SamplerState g_textureSampler : register(s0);
	SamplerState g_volumeFogSampler : register(s4);

	// Must match VolumetricScattering::kVolumeWidth/Height/Depth. Smoothstep-
	// corrected trilinear softens froxel-grid stair-stepping in the probe.
	static const float3 PARTICLE_FROXEL_DIMS = float3(128.0f, 72.0f, 64.0f);

	float4 SampleFroxelSmooth(Texture3D vol, SamplerState samp, float3 uvw)
	{
		const float3 c = uvw * PARTICLE_FROXEL_DIMS - 0.5f;
		const float3 fb = floor(c);
		const float3 f = c - fb;
		const float3 fs = f * f * (3.0f - 2.0f * f);
		return vol.SampleLevel(samp, (fb + 0.5f + fs) / PARTICLE_FROXEL_DIMS, 0);
	}

	cbuffer ParticleLightBuffer : register(b7)
	{
		float4 g_particleLightCountsAndParams; // x=pointCount y=spotCount z=softFadeScale
		float4 g_particleTransparencyAssist; // x=enabled y=depthBias z=ditherStrength
		float4 g_pointLightPosRadius[16];
		float4 g_pointLightColorStrength[16];
		float4 g_spotLightPosRadius[16];
		float4 g_spotLightDirCone[16];
		float4 g_spotLightColorStrength[16];
		// Padding to keep this cbuffer the same total size (1568 bytes) as
		// DefaultPixel.shader's ForwardLightsBuffer, which also binds to b7. Both buffers
		// share the slot and either may be left bound when the other shader runs; if the
		// sizes don't match, D3D11 logs "Constant Buffer too small" the first time a draw
		// inherits the smaller binding. The particle shader doesn't currently use the
		// inner-cone field but the slot has to exist for the layout to match.
		float4 g_spotLightInnerCone[16];
	};

	float Hash12(float2 p)
	{
		const float h = dot(p, float2(127.1f, 311.7f));
		return frac(sin(h) * 43758.5453123f);
	}

	float4 ShaderMain(MeshPixelInput input) : SV_Target
	{
		// Select this particle's texture by the per-instance slot index
		// (positionWS.w, stamped by the sim shader from emitter flags).
		const uint texIdx = (uint)(input.positionWS.w + 0.5f);
		float4 tex;
		if (texIdx == 1u)
			tex = g_albedoMap1.Sample(g_textureSampler, input.texcoord);
		else if (texIdx == 2u)
			tex = g_albedoMap2.Sample(g_textureSampler, input.texcoord);
		else if (texIdx == 3u)
			tex = g_albedoMap3.Sample(g_textureSampler, input.texcoord);
		else
			tex = g_albedoMap.Sample(g_textureSampler, input.texcoord);
		float alpha = tex.a * input.colour.a;
		if (alpha <= 0.001f)
			clip(-1.0f);

		if (g_particleTransparencyAssist.x > 0.5f)
		{
			const float depthBias = saturate(g_particleTransparencyAssist.y);
			const float ditherStrength = saturate(g_particleTransparencyAssist.z);

			// Approximate camera-depth bias to reduce unstable self-overlap in unsorted transparency.
			const float depthNdc = saturate(input.position.z / max(input.position.w, 1e-5f));
			const float depthWeight = lerp(1.0f, (1.0f - depthNdc), depthBias);
			alpha *= depthWeight;

			if (ditherStrength > 0.001f)
			{
				const float noise = Hash12(floor(input.position.xy));
				const float cutoff = saturate((1.0f - alpha) * ditherStrength);
				if (noise < cutoff)
					clip(-1.0f);
			}

			alpha = saturate(alpha);
			if (alpha <= 0.001f)
				clip(-1.0f);
		}

		// Froxel UVW for this particle: screen UV + exp-mapped depth slice
		// (near 0.1m, far 128m - mirrors VolumetricScatterApply). Shared by
		// the fog-transmittance tap below and the light-probe tap in the
		// lit path.
		const float2 froxelUv = input.position.xy / float2((float)g_screenWidth, (float)g_screenHeight);
		const float froxelDist = length(input.positionWS.xyz - g_eyePos.xyz);
		const float3 froxelUvw = float3(
			froxelUv,
			saturate(log(max(froxelDist, 0.1f) / 0.1f) / log(128.0f / 0.1f)));

		// Per-particle volumetric fog transmittance at the PARTICLE's depth.
		// Inscatter is intentionally NOT added here: the background behind
		// the particle already carries the full inscatter from the apply
		// pass and shows through (1 - alpha); adding it again per particle
		// would double-brighten dense flurries.
		float fogTrans = 1.0f;
		if (g_particleLightCountsAndParams.w > 0.5f)
		{
			fogTrans = SampleFroxelSmooth(g_volumetricFogLUT, g_volumeFogSampler, froxelUvw).a;
		}

		float3 baseColor = tex.rgb * input.colour.rgb;
		if (input.viewDirection.w < 0.5f)
		{
			return float4(baseColor * fogTrans, alpha);
		}

		// FROXEL LIGHT PROBE path: when the scatter volume is bound
		// (transparencyAssist.w carries its brightness scale), light the
		// particle from the froxel grid instead of the manual loops below.
		// The probe already includes the shadow-tested sun, shadow-tested
		// point/spot lights, emissive surface glow and fog ambient - so a
		// snowflake darkens in a building's shadow, sparkles through a
		// streetlamp cone, and picks up neon tint, none of which the
		// unshadowed analytic loops below can do. Particles are isotropic
		// scatterers (snow, dust, mist) so skipping NdotL is appropriate.
		const float probeScale = g_particleTransparencyAssist.w;
		if (probeScale > 0.0f)
		{
			// Mean inscattered radiance of the fog here = accumulated inscatter /
			// accumulated opacity. Both EMA-smoothed -> temporally STABLE (no pulse);
			// ray-accumulated -> free of hard per-froxel shadow edges. Also makes the
			// particle MATCH the fog it sits in by construction.
			const float4 integ = SampleFroxelSmooth(g_volumetricFogLUT, g_volumeFogSampler, froxelUvw);
			const float opacity = max(1.0f - integ.a, 0.04f);
			const float3 meanRadiance = integ.rgb / opacity;
			const float3 probeAmbient = max(g_atmosphere.ambientLight.rgb * g_atmosphere.ambientLight.a, float3(0.02f, 0.02f, 0.02f));
			const float3 probeLight = probeAmbient + meanRadiance * probeScale;
			return float4(baseColor * probeLight * fogTrans, alpha);
		}

		float3 n = normalize(input.normal);
		float3 p = input.positionWS.xyz;
		float3 l = -normalize(g_lightDirection.xyz);
		float sunNdotL = saturate(dot(n, l));
		float sunStrength = max(0.0f, g_globalLight.x);
		float3 ambient = max(g_atmosphere.ambientLight.rgb * g_atmosphere.ambientLight.a, float3(0.08f, 0.08f, 0.08f));
		float3 lightAccum = ambient + sunNdotL * sunStrength.xxx;

		const uint pointCount = min((uint)g_particleLightCountsAndParams.x, 16u);
		[loop]
		for (uint i = 0; i < pointCount; ++i)
		{
			float3 toLight = g_pointLightPosRadius[i].xyz - p;
			float distSq = dot(toLight, toLight);
			float radius = max(0.05f, g_pointLightPosRadius[i].w);
			float radiusSq = radius * radius;
			if (distSq > radiusSq)
				continue;

			float dist = sqrt(max(1e-6f, distSq));
			float3 lDir = toLight / dist;
			float ndotl = saturate(dot(n, lDir));
			float atten = saturate(1.0f - distSq / radiusSq);
			atten *= atten;
			lightAccum += g_pointLightColorStrength[i].rgb * (g_pointLightColorStrength[i].w * ndotl * atten);
		}

		const uint spotCount = min((uint)g_particleLightCountsAndParams.y, 16u);
		[loop]
		for (uint i = 0; i < spotCount; ++i)
		{
			float3 toLight = g_spotLightPosRadius[i].xyz - p;
			float distSq = dot(toLight, toLight);
			float radius = max(0.05f, g_spotLightPosRadius[i].w);
			float radiusSq = radius * radius;
			if (distSq > radiusSq)
				continue;

			float dist = sqrt(max(1e-6f, distSq));
			float3 lDir = toLight / dist;
			float3 spotForward = normalize(g_spotLightDirCone[i].xyz);
			float coneCos = g_spotLightDirCone[i].w;
			float cosAngle = dot(-lDir, spotForward);
			float coneAtten = smoothstep(coneCos, saturate(coneCos + 0.08f), cosAngle);
			if (coneAtten <= 0.0f)
				continue;

			float ndotl = saturate(dot(n, lDir));
			float distAtten = saturate(1.0f - distSq / radiusSq);
			distAtten *= distAtten;
			lightAccum += g_spotLightColorStrength[i].rgb * (g_spotLightColorStrength[i].w * ndotl * distAtten * coneAtten);
		}

		float3 litColor = baseColor * lightAccum;

		return float4(litColor * fogTrans, alpha);
	}
}
