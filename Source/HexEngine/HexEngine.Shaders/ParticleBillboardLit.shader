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
	Texture2D g_albedoMap : register(t0);
	SamplerState g_textureSampler : register(s0);

	cbuffer ParticleLightBuffer : register(b7)
	{
		float4 g_particleLightCountsAndParams; // x=pointCount y=spotCount z=softFadeScale
		float4 g_particleTransparencyAssist; // x=enabled y=depthBias z=ditherStrength
		float4 g_pointLightPosRadius[16];
		float4 g_pointLightColorStrength[16];
		float4 g_spotLightPosRadius[16];
		float4 g_spotLightDirCone[16];
		float4 g_spotLightColorStrength[16];
	};

	float Hash12(float2 p)
	{
		const float h = dot(p, float2(127.1f, 311.7f));
		return frac(sin(h) * 43758.5453123f);
	}

	float4 ShaderMain(MeshPixelInput input) : SV_Target
	{
		float4 tex = g_albedoMap.Sample(g_textureSampler, input.texcoord);
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

		float3 baseColor = tex.rgb * input.colour.rgb;
		if (input.viewDirection.w < 0.5f)
		{
			return float4(baseColor, alpha);
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

		return float4(litColor, alpha);
	}
}
