"Requirements"
{
	//ShadowMaps
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
	Atmosphere
	ShadowUtils
	Utils
	LightingUtils
}
"VertexShader"
{
	MeshPixelInput ShaderMain(MeshVertexInput input, MeshInstanceData instance)
	{
		MeshPixelInput output;

		input.position.w = 1.0f;

		output.position = mul(input.position, instance.world);
		output.position = mul(output.position, g_viewMatrix);
		output.position = mul(output.position, g_projectionMatrix);

		output.positionWS = mul(input.position, instance.world);

		output.texcoord = input.texcoord;

		output.normal = mul(input.normal, (float3x3)instance.world);
		output.normal = normalize(output.normal);

		output.tangent = mul(input.tangent, (float3x3)instance.world);
		output.tangent = normalize(output.tangent);

		output.binormal = mul(input.binormal, (float3x3)instance.world);
		output.binormal = normalize(output.binormal);

		// Determine the viewing direction based on the position of the camera and the position of the vertex in the world.
		output.viewDirection.xyz = g_eyePos.xyz - output.positionWS.xyz;

		// Normalize the viewing direction vector.
		output.viewDirection.xyz = normalize(output.viewDirection.xyz);

		output.colour = instance.colour;

		return output;
	}
}
"PixelShader"
{
	//SHADOWMAPS_RESOURCE(0); // 

	Texture2D g_splatMap : register(t0);
	Texture3D g_noiseMap : register(t1);


	//Texture2D g_depthMaps[4] : register(t4);

	SamplerState g_textureSampler : register(s0);
	SamplerComparisonState g_cmpSampler : register(s1);
	SamplerState g_textureSamplerMirror : register(s3);

	static const int numStepsLight = 8;
	static const float lightAbsorptionTowardSun = 1.0f;
	static const float darknessThreshold = 0.2f;
	static const float lightAbsorbptionThroughCloud = 1.0f;

	float2 RayBoxDist(float3 boundsMin, float3 boundsMax, float3 rayOrigin, float3 rayDir)
	{
		float3 t0 = (boundsMin - rayOrigin) / rayDir;
		float3 t1 = (boundsMax - rayOrigin) / rayDir;
		float3 tmin = min(t0, t1);
		float3 tmax = max(t0, t1);

		float dstA = max(max(tmin.x, tmin.y), tmin.z);
		float dstB = min(tmax.x, min(tmax.y, tmax.z));

		float dstToBox = max(0, dstA);
		float dstInsideBox = max(0, dstB - dstToBox);

		return float2(dstToBox, dstInsideBox);

	}

	float GetDensity(float3 position)
	{
		float3 texCoord = (position.xyz + 100.0f) / 200.0f;

		texCoord *= 1.0f;

		float4 cloudNoise = g_noiseMap.SampleLevel(g_textureSamplerMirror, texCoord/*float3(input.texcoord.xy, ySamplePos)*/, 0.0f);

		return cloudNoise.r;
	}

	float LightMarch(float3 position, float3 boundsMin, float3 boundsMax)
	{
		float3 dirToLight = normalize(g_lightPosition.xyz - position);

		float dstInsideBox = RayBoxDist(boundsMin, boundsMax, position, 1 / dirToLight).y;

		float stepSize = dstInsideBox / numStepsLight;
		float totalDensity = 0;

		for (int step = 0; step < numStepsLight; step++) {
			position += dirToLight * stepSize;
			totalDensity += max(0, GetDensity(position) * stepSize);
		}

		float transmittance = exp(-totalDensity * lightAbsorptionTowardSun);
		return darknessThreshold + transmittance * (1 - darknessThreshold);
	}


	float4 ShaderMain(MeshPixelInput input) : SV_TARGET
	{
		//return float4(1,0,0,1);

		//float ySamplePos = (input.positionWS.y + 100.0f) / 200.0f;

		float3 rayDir = normalize(input.positionWS.xyz - g_eyePos.xyz);		

		const float3 boundsMin = float3(-200.0f, -200.0f, -200.0f);
		const float3 boundsMax = float3(200.0f, 200.0f, 200.0f);

		float2 boxDistance = RayBoxDist(boundsMin, boundsMax, g_eyePos.xyz, rayDir);

		bool didHitCloudVolume = boxDistance.y != 0;

		if (didHitCloudVolume)
		{
			float3 marchStart = g_eyePos.xyz + rayDir * boxDistance.x;
			float totalLength = boxDistance.y;

			//float totalDensity = 0.0f;
			float transmittance = 1.0f;
			float3 lightEnergy = 0.0f;

			float dstToBox = boxDistance.x;
			float dstInsideBox = boxDistance.y;

			const float stepSize = 10.0f;
			float dstTravelled = 0.0f;
			float dstLimit = min(dstToBox, dstInsideBox);

			//float3 dirToLight = normalize(g_lightPosition.xyz - input.positionWS.xyz);

			//float cosAngle = dot(rayDir, dirToLight);
			//float phaseVal = phase(cosAngle);

			while(dstTravelled < dstLimit)
			{
				float density = /*totalLength / 200.0f **/ GetDensity(marchStart);

				if (density > 0.0f)
				{
					float lightTransmittance = LightMarch(marchStart, boundsMin, boundsMax);
					lightEnergy += density * stepSize * transmittance * lightTransmittance;// *phaseVal;
					transmittance *= exp(-density * stepSize * lightAbsorbptionThroughCloud);

					if (transmittance < 0.01f)
						break;
				}

				//totalDensity += density;

				dstTravelled += stepSize;
				marchStart += rayDir * dstTravelled;
			}

			//totalDensity /= 10.0f;

			lightEnergy = saturate(lightEnergy);

			return float4(lightEnergy.rgb, lightEnergy.r);
		}

		return float4(0, 0, 0, 0);	
	}
}