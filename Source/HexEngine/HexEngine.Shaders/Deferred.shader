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
	GBUFFER_RESOURCE(0, 1, 2, 3, 4);
	Texture2D g_beautyTex : register(t5);
	SHADOWMAPS_RESOURCE(6);
	Texture3D g_cloudShapeNoise : register(t12);
	Texture3D g_cloudDetailNoise : register(t13);
	

	SamplerState g_textureSampler : register(s0);
	SamplerComparisonState g_cmpSampler : register(s1);
	SamplerState g_pointSampler : register(s2);
	SamplerState g_mirrorSampler : register(s3);

	cbuffer CloudConstants : register(b4)
	{
		float4 g_cloudBoundsMin;
		float4 g_cloudBoundsMax;
		float4 g_cloudParams0; // x=density, y=coverage, z=erosion, w=maxDistance
		float4 g_cloudParams1; // x=absorption, y=powder, z=anisotropy, w=stepScale
		float4 g_cloudParams2; // x=shapeScale, y=detailScale, z=windSpeed, w=animationSpeed
		float4 g_cloudParams3; // x=viewAbsorption, y=ambientStrength, z=shadowFloor, w=phaseBoost
		float4 g_cloudParams4; // x=silverLiningStrength, y=silverLiningExponent, z=multiScatterStrength, w=heightTintStrength
		float4 g_cloudParams5; // x=tintWarmth, y=skyTintInfluence, z=directionalDiffuse, w=ambientOcclusion
		float4 g_cloudWindDirection; // xyz=wind direction, w=quality preset
		float4 g_cloudMarch; // x=view steps, y=light steps, z=ground shadow steps, w=ground shadow strength
	};

	float2 RayBoxDist(float3 boundsMin, float3 boundsMax, float3 rayOrigin, float3 rayDir)
	{
		float3 safeDir = rayDir;
		safeDir.x = abs(safeDir.x) < 1e-5f ? (safeDir.x < 0.0f ? -1e-5f : 1e-5f) : safeDir.x;
		safeDir.y = abs(safeDir.y) < 1e-5f ? (safeDir.y < 0.0f ? -1e-5f : 1e-5f) : safeDir.y;
		safeDir.z = abs(safeDir.z) < 1e-5f ? (safeDir.z < 0.0f ? -1e-5f : 1e-5f) : safeDir.z;
		const float3 invDir = 1.0f / safeDir;
		const float3 t0 = (boundsMin - rayOrigin) * invDir;
		const float3 t1 = (boundsMax - rayOrigin) * invDir;
		const float3 tmin = min(t0, t1);
		const float3 tmax = max(t0, t1);

		const float dstA = max(max(tmin.x, tmin.y), tmin.z);
		const float dstB = min(tmax.x, min(tmax.y, tmax.z));

		const float dstToBox = max(0.0f, dstA);
		const float dstInsideBox = max(0.0f, dstB - dstToBox);

		return float2(dstToBox, dstInsideBox);
	}

	float SampleCloudDensity(float3 worldPos, float3 boundsMin, float3 boundsMax, float3 windOffset)
	{
		const float3 boundsSize = max(boundsMax - boundsMin, 1e-3f.xxx);
		const float3 localUVW = (worldPos - boundsMin) / boundsSize;

		if (any(localUVW < 0.0f.xxx) || any(localUVW > 1.0f.xxx))
			return 0.0f;

		const float shape = g_cloudShapeNoise.SampleLevel(g_mirrorSampler, worldPos * g_cloudParams2.x + windOffset, 0.0f).r;
		const float detail = g_cloudDetailNoise.SampleLevel(g_mirrorSampler, worldPos * g_cloudParams2.y + windOffset * 1.7f, 0.0f).r;
		const float weather = g_cloudShapeNoise.SampleLevel(g_mirrorSampler, worldPos * (g_cloudParams2.x * 0.32f) + windOffset * 0.45f, 0.0f).r;

		const float height = saturate(localUVW.y);
		const float heightMask = smoothstep(0.03f, 0.22f, height) * (1.0f - smoothstep(0.68f, 0.98f, height));
		const float verticalCore = smoothstep(0.05f, 0.55f, height) * (1.0f - smoothstep(0.62f, 0.96f, height));

		const float coverage = saturate(g_cloudParams0.y);
		const float weatherShift = (weather - 0.5f) * 0.35f;
		const float coverageThreshold = saturate(1.0f - coverage + weatherShift);
		float cloud = saturate((shape - coverageThreshold) / max(0.001f, coverage));
		const float erosionByHeight = lerp(1.22f, 0.78f, smoothstep(0.18f, 0.90f, height));
		cloud = saturate(cloud - (1.0f - detail) * g_cloudParams0.z * erosionByHeight);
		const float billow = saturate(1.0f + (detail - 0.5f) * 0.28f + (weather - 0.5f) * 0.36f);
		const float densityShape = lerp(cloud * cloud, cloud, 0.55f);

		return min(densityShape * heightMask * verticalCore * billow * g_cloudParams0.x, 2.0f);
	}

	float CalculateCloudShadow(float3 worldPos, float3 sunDir)
	{
		const float shadowStrength = saturate(g_cloudMarch.w);
		if (shadowStrength <= 0.0001f)
			return 1.0f;

		const float3 boundsMin = g_cloudBoundsMin.xyz;
		const float3 boundsMax = g_cloudBoundsMax.xyz;
		if (worldPos.y > boundsMax.y)
			return 1.0f;

		const int shadowSteps = max(1, (int)g_cloudMarch.z);
		const float2 hit = RayBoxDist(boundsMin, boundsMax, worldPos, sunDir);
		if (hit.y <= 0.0f)
			return 1.0f;

		const float3 windDir = normalize(g_cloudWindDirection.xyz + float3(1e-5f, 1e-5f, 1e-5f));
		const float3 windOffset = windDir * g_cloudParams2.z * g_cloudParams2.w * (g_time * 0.01f);

		const float invCloudHeight = rcp(max(100.0f, boundsMax.y - boundsMin.y));
		const float stepLen = max(1.0f, hit.y / (float)shadowSteps);

		float opticalDepth = 0.0f;
		float travelled = 0.0f;
		[loop]
		for (int i = 0; i < shadowSteps; ++i)
		{
			if (travelled >= hit.y)
				break;

			const float3 samplePos = worldPos + sunDir * (hit.x + travelled);
			const float density = SampleCloudDensity(samplePos, boundsMin, boundsMax, windOffset);
			opticalDepth += density * stepLen * invCloudHeight;
			travelled += stepLen;
		}

		const float cloudTransmittance = max(g_cloudParams3.z, exp(-opticalDepth * g_cloudParams1.x));
		return lerp(1.0f, cloudTransmittance, shadowStrength);
	}

	void CalculateDiffuseAndSpecularLighting(
		float shadowValue,
		float3 pixelNormal,
		float3 pixelSpecular,
		float3 pixelColour,
		float3 lightDir,
		float3 eyeDir,
		float shinyPower,
		float shininessStrength,		
		float lightMultiplier,
		inout float3 diffuse, 
		inout float3 specular)
	{
		float lightIntensity = saturate(dot(pixelNormal, lightDir));

		if (lightIntensity > 0.0f /*&& shadowValue > 0.0f*/)
		{
			diffuse += pixelColour * lightIntensity * shadowValue;

			diffuse = /*saturate*/(diffuse) * lightMultiplier;

			// Calculate the reflection vector based on the light intensity, normal vector, and light direction.
			float3 reflection = normalize(2 * lightIntensity * pixelNormal - lightDir);

			// Determine the amount of specular light based on the reflection vector, viewing direction, and7 specular power.
			specular = pow(saturate(dot(reflection, eyeDir)), shinyPower);// * shininessStrength;

			specular = specular * pixelSpecular * shadowValue;

			specular = /*saturate*/(specular) * lightMultiplier;
		}
	}

	float4 ShaderMain(UIPixelInput input) : SV_TARGET
	{
		float2 texcoord = input.texcoord;

		float2 screenPos = float2(input.position.x / (float)g_screenWidth, input.position.y / (float)g_screenHeight);

		// Sample the gbuffer
		//
		float4 pixelColour = g_beautyTex.Sample(g_pointSampler, screenPos);
		float4 pixelNormal = GBUFFER_NORMAL.Sample(g_pointSampler, screenPos);
		float4 pixelPosWS = GBUFFER_POSITION.Sample(g_pointSampler, screenPos);
		//float4 pixelMaterial = GBUFFER_SPECULAR.Sample(g_pointSampler, screenPos);
		
		//return float4(pixelColour.aaa, 1.0f);

		// sky
		if(pixelColour.a == -1 || pixelPosWS.a > 0.0f)
		{
			//return float4(1, 0, 0, 1.0f);
			return float4(pixelColour.rgb, 1.0f);
		}
		

		float3 lightDir = -normalize(g_lightDirection.xyz);
		float3 eyeVector = normalize(g_eyePos.xyz - pixelPosWS.xyz);

		ShadowInput shadow;
		shadow.pixelDepth = pixelNormal.w;
		shadow.positionWS = pixelPosWS;
		shadow.positionSS = input.position.xy;
		shadow.samples = g_shadowConfig.samples;

		float d = dot(normalize(pixelNormal.xyz), normalize(g_shadowCasterLightDir.xyz));
		float bias = g_shadowConfig.biasMultiplier* (1.0 - d);// max(0.000002 * (1.0 - d), 0.0000002); // seems good
			//float bias = 0.00011 * (1.0 - d);// max(0.000002 * (1.0 - d), 0.0000002);

		float depthValue = CalculateShadows(shadow, g_cmpSampler, g_pointSampler, SHADOWMAPS, bias);
		depthValue *= CalculateCloudShadow(pixelPosWS.xyz, lightDir);

		float4 pbr = CalculatePBR(
			GBUFFER_SPECULAR, 
			g_pointSampler,
			screenPos, 
			pixelNormal.xyz, 
			pixelPosWS.xyz, 
			lightDir, 
			getSunColour(), 
			pixelColour.rgb,
			depthValue,
			g_globalLight[0]);

		return pbr;

	#if 0
		float shinyPower = pixelSpecular.g;
		float shininessStrength = pixelSpecular.r;
		float emission = pixelPosWS.w;

		if(emission == -1.0f)
		{
			return float4(pixelColour.rgb, 1.0f);
		}
		else if (emission > 0.0f)
		{
			pixelColour.rgb = pixelColour.rgb * emission;
		}
		//else
		{
			ShadowInput shadow;
			shadow.pixelDepth = pixelNormal.w;
			shadow.positionWS = pixelPosWS;
			shadow.positionSS = input.position.xy;
			shadow.samples = g_shadowConfig.samples;

			float d = dot(normalize(pixelNormal.xyz), normalize(g_shadowCasterLightDir.xyz));
			float bias = g_shadowConfig.biasMultiplier* (1.0 - d);// max(0.000002 * (1.0 - d), 0.0000002); // seems good
			//float bias = 0.00011 * (1.0 - d);// max(0.000002 * (1.0 - d), 0.0000002);

			float depthValue = CalculateShadows(shadow, g_cmpSampler, g_pointSampler, SHADOWMAPS, bias);

			float3 ambient = pixelColour.rgb * g_atmosphere.ambientLight.rgb;
			float3 diffuse = float3(0, 0, 0);// pixelColour.rgb* depthValue;// float3(0, 0, 0);
			float3 specular = float3(0, 0, 0);

			CalculateDiffuseAndSpecularLighting(
				depthValue,
				pixelNormal.xyz,
				pixelSpecular.rrr,
				pixelColour.rgb * getSunColour(),
				lightDir,
				eyeVector, 
				shinyPower,
				shininessStrength,		
				g_globalLight[0],
				diffuse,
				specular);

			float3 finalColour = ambient + diffuse + specular;

			float4 result = float4(finalColour.rgb, 1.0f);
			return /*saturate*/(result);
		
		}
		#endif
	}
}
