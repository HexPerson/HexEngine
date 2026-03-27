"Requirements"
{
	GBuffer
}
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
	PBRutils
}
"VertexShader"
{
	MeshPixelInput ShaderMain(MeshVertexInput input, MeshInstanceData instance)
	{
		MeshPixelInput output = (MeshPixelInput)0;

		input.position.w = 1.0f;

		output.position = mul(input.position, instance.world);
		output.position = mul(output.position, g_viewProjectionMatrix);

		// Per-fragment world position on the light volume mesh.
		output.positionWS = mul(input.position, instance.world);

		// Store light center for pixel shader calculations.
		output.tangent = instance.world[3].xyz;

		output.colour = instance.colour;

		// we'll use this for radius and strength
		output.texcoord = instance.uvScale;

		return output;
	}
}
"PixelShader"
{
	GBUFFER_RESOURCE(0, 1, 2, 3, 4);

	Texture2D g_beautyTex : register(t13);

	SamplerState g_textureSampler : register(s0);
	SamplerState g_pointSampler : register(s2);

	float ComputeScattering(float lightDotView)
	{
		float result = 1.0f - g_atmosphere.volumetricScattering * g_atmosphere.volumetricScattering;
		result /= (4.0f * PI * pow(1.0f + g_atmosphere.volumetricScattering * g_atmosphere.volumetricScattering - (2.0f * g_atmosphere.volumetricScattering) * lightDotView, 1.5f));
		return result;
	}

	int GetVolumetricSampleCount()
	{
		if (g_atmosphere.volumetricQuality <= 0)
			return 8;
		if (g_atmosphere.volumetricQuality >= 2)
			return 20;

		return 14;
	}

	bool GetRaySphereInterval(float3 rayStart, float3 rayDirection, float3 sphereCenter, float sphereRadius, out float tMin, out float tMax)
	{
		float3 oc = rayStart - sphereCenter;
		float b = dot(oc, rayDirection);
		float c = dot(oc, oc) - (sphereRadius * sphereRadius);
		float h = b * b - c;
		if (h <= 0.0f)
		{
			tMin = 0.0f;
			tMax = 0.0f;
			return false;
		}

		float s = sqrt(h);
		tMin = -b - s;
		tMax = -b + s;
		return tMax > 0.0f;
	}

	float CalculateVolumetricScattering(float3 raySurfacePos, float3 lightPos, float radius, float lightStrength, float sceneDepth)
	{
		if (radius <= 0.0001f || lightStrength <= 0.0f || g_atmosphere.volumetricStrength <= 0.0f)
			return 0.0f;

		float3 rayStart = g_eyePos.xyz;
		float3 direction = normalize(raySurfacePos - rayStart);
		float distanceToLight = length(lightPos - rayStart);
		bool cameraInsideVolume = distanceToLight < radius;

		float tMin = 0.0f;
		float tMax = 0.0f;
		if (!GetRaySphereInterval(rayStart, direction, lightPos, radius, tMin, tMax))
			return 0.0f;
		float traceStart = max(0.0f, tMin);
		float traceEnd = tMax;
		float rayLength = traceEnd - traceStart;
		if (rayLength <= 0.0001f)
			return 0.0f;

		const int sampleCount = GetVolumetricSampleCount();
		const float stepDistance = rayLength / (float)sampleCount;
		float3 currentPos = rayStart + direction * (traceStart + stepDistance * 0.5f);
		float accumFog = 0.0f;

		[loop]
		for (int i = 0; i < sampleCount; ++i)
		{
			float sampleDepth = -mul(float4(currentPos, 1.0f), g_viewMatrix).z;
			if (sampleDepth <= 0.0f)
			{
				currentPos += direction * stepDistance;
				continue;
			}

			if (sceneDepth > 0.0f)
			{
				float depthBias = max(0.02f, sampleDepth * 0.002f);
				if (sampleDepth > sceneDepth + depthBias)
					break;
			}

			float3 lightToSample = lightPos - currentPos;
			float d = length(lightToSample);
			if (d > 0.0001f)
			{
				float attenuation = saturate(1.0f - saturate(d / radius));
				attenuation = attenuation * attenuation;

				float3 lightDirection = normalize(currentPos - lightPos);
				float phase = ComputeScattering(dot(direction, lightDirection));

				accumFog += phase * attenuation * stepDistance;
			}

			currentPos += direction * stepDistance;
		}

		accumFog /= max(radius, 0.0001f);

		accumFog *= lightStrength * g_atmosphere.volumetricStrength;

		// Inside the light volume we reduce gain, but keep it distance-weighted so it does not look flat/dull.
		if (cameraInsideVolume)
		{
			float centerFactor = saturate(distanceToLight / max(radius, 0.0001f));
			float insideGain = lerp(g_atmosphere.volumetricPointInsideMin, g_atmosphere.volumetricPointInsideMax, centerFactor);
			accumFog *= insideGain;
		}

		return max(0.0f, accumFog);
	}

	float4 ShaderMain(MeshPixelInput input) : SV_Target
	{
		float2 screenPos = float2(input.position.x / (float)g_screenWidth, input.position.y / (float)g_screenHeight);

		float4 pixelColour = GBUFFER_DIFFUSE.Sample(g_pointSampler, screenPos);
		float4 pixelNormal = GBUFFER_NORMAL.Sample(g_pointSampler, screenPos);
		float4 pixelPosWS = GBUFFER_POSITION.Sample(g_pointSampler, screenPos);
		float3 lightPos = input.tangent;
		float lightRange = input.texcoord.x;
		float lightIntensity = input.colour.a;
		float volumetricScattering = CalculateVolumetricScattering(
			input.positionWS.xyz,
			lightPos,
			lightRange,
			lightIntensity,
			pixelNormal.w);

		float3 volumetricContribution = max(input.colour.rgb * volumetricScattering, 0.0f);
		if (!all(isfinite(volumetricContribution)))
			volumetricContribution = 0.0f.xxx;

		bool hasGeometry = !(pixelPosWS.a > 0.0f || pixelColour.a == -1.0f || pixelNormal.w <= 0.0f);
		if (!hasGeometry)
			return float4(volumetricContribution, 1.0f);

		float3 normalWS = pixelNormal.xyz;
		const float normalLenSq = dot(normalWS, normalWS);
		if (normalLenSq <= 0.000001f)
			return float4(volumetricContribution, 1.0f);
		normalWS *= rsqrt(normalLenSq);

		float3 lightToPixelVec = lightPos - pixelPosWS.xyz;
		float d = length(lightToPixelVec);
		if (d <= 0.0001f || d > lightRange)
			return float4(volumetricContribution, 1.0f);
		lightToPixelVec /= d;

		float attenuation = saturate(1.0f - saturate(d / lightRange));
		attenuation = attenuation * attenuation;

		float4 pbr = CalculatePBRPointLighting(
			GBUFFER_SPECULAR,
			g_pointSampler,
			screenPos,
			normalWS,
			pixelPosWS.xyz,
			lightToPixelVec,
			input.colour.rgb,
			pixelColour.rgb,
			1.0f,
			attenuation
		);

		float3 lightContribution = max(pbr.rgb * lightIntensity, 0.0f);
		lightContribution += volumetricContribution;
		if (!all(isfinite(lightContribution)))
			lightContribution = 0.0f.xxx;
		return float4(lightContribution, 1.0f);
	}
}
