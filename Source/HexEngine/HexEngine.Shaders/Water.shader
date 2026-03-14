"Requirements"
{
	GBuffer
	Beauty
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
	ShadowUtils
	Utils
	Atmosphere
}
"GlobalIncludes"
{
	Global
}
"Global"
{
	

	float GerstnerWaveHeight(float4 wave, float3 p)
	{
		float steepness = wave.z / WaveSizeMultiplier;
		float wavelength = wave.w / WaveSizeMultiplier;
		float k = 2 * 3.14159f / wavelength;
		float c = sqrt(9.8 / k);
		float2 d = normalize(wave.xy);
		float f = k * (dot(d, p.xz) - c * g_time * 4.2f);

		float a = steepness / k;		

		return a * sin(f);
	}
}
"VertexShader"
{
	//static const float _Wavelength = 1.0f;
	//static const float _Amplitude = 0.001f;

	

#define ENABLE_WAVES 1

	
float3 GerstnerWave(
		float4 wave, float3 p, inout float3 tangent, inout float3 binormal
	) {
		float steepness = wave.z / WaveSizeMultiplier;
		float wavelength = wave.w / WaveSizeMultiplier;
		float k = 2 * 3.14159f / wavelength;
		float c = sqrt(9.8 / k);
		float2 d = normalize(wave.xy);
		float f = k * (dot(d, p.xz) - c * g_time * 4.2f);

		//f = RoundDown(f);

		float a = steepness / k;

		//p.x += d.x * (a * cos(f));
		//p.y = a * sin(f);
		//p.z += d.y * (a * cos(f));

		tangent += float3(
			-d.x * d.x * (steepness * sin(f)),
			d.x * (steepness * cos(f)),
			-d.x * d.y * (steepness * sin(f))
			);
		binormal += float3(
			-d.x * d.y * (steepness * sin(f)),
			d.y * (steepness * cos(f)),
			-d.y * d.y * (steepness * sin(f))
			);
		// lowpoly:
		/*return float3(
			0.0f,
			a * sin(f),
			0.0f
			);*/

		return float3(
			d.x * (a * cos(f)),
			a * sin(f),
			d.y * (a * cos(f))
			);
	}
	

	

	MeshPixelInput ShaderMain(MeshVertexInput input, MeshInstanceData instance, uint instanceID : SV_INSTANCEID)
	{
		MeshPixelInput output = (MeshPixelInput)0;

		input.position.w = 1.0f;

		//float time = g_time;// *10.0f;
		//const float waveScale = 0.6f;

		//float k = 2 * (3.14159) / _Wavelength;
		//float f = k * ((input.position.x + instance.worldPos.x) + time);

		//float dx = sin(input.position.x + instance.worldPos.x + time) * waveScale;
		//float dz = cos(input.position.z + instance.worldPos.z + time) * waveScale;

		//input.position.y += dx;
		//input.position.y += dz;

		float3 worldPos = instance.world[3].xyz;

		

		float3 gridPoint = input.position.xyz + worldPos;

		float3 tangent = input.tangent;// float3(1, 0, 0); ;// float3(1, 0, 0);
		float3 binormal = input.binormal;// float3(0, 0, 1); ;// float3(0, 0, 1);
		float3 normal = input.normal;
		float3 p = gridPoint;

#if ENABLE_WAVES == 1
		p += GerstnerWave(_WaveA, gridPoint, tangent, binormal);
		p += GerstnerWave(_WaveB, gridPoint, tangent, binormal);
		p += GerstnerWave(_WaveC, gridPoint, tangent, binormal);
		p += GerstnerWave(_WaveD, gridPoint, tangent, binormal);

		

		tangent = normalize(tangent);
		binormal = normalize(binormal);

		normal = normalize(cross(binormal, tangent));
#endif
		

		input.position = float4(p.xyz - worldPos, 1.0f);

		//input.position.y = RoundDown(input.position.y);// fmod(input.position.y, 100.0f);

		input.normal = normal;
		input.binormal = binormal;
		input.tangent = tangent;

		//input.position.y = _Amplitude * sin(f);
		//input.position.x += _Amplitude * cos(f);

		output.position = mul(input.position, instance.world);
		output.positionWS = output.position;

		output.position = mul(output.position, g_viewProjectionMatrix);

		

		input.texcoord.xy -= g_time * 0.03f;
		output.texcoord = input.texcoord * 1.4;

		/*float3 tangent = normalize(float3(
			1 - k * _Amplitude * sin(f),
			k * _Amplitude * cos(f),
			0));*/

		/*float3 tangent = normalize(float3(
			0,
			k * _Amplitude * cos(f),
			1 - k * _Amplitude * sin(f)*/

		//float3 normal = float3(-tangent.y, tangent.x, 0);

		//input.tangent = tangent;
		//input.binormal = normalize(cross(tangent, normal));

		//input.normal = normal;// normalize(cross(input.tangent, input.binormal));

		matrix normalMatrix = mul(instance.worldInverseTranspose, g_worldMatrix);

		output.normal = mul(input.normal, (float3x3)normalMatrix);
		output.normal = normalize(output.normal);

		output.tangent = mul(input.tangent, (float3x3)normalMatrix);
		output.tangent = normalize(output.tangent);

		output.binormal = mul(input.binormal, (float3x3)normalMatrix);
		output.binormal = normalize(output.binormal);

		// input.tangent = normalize(float3(0.0f, dx, 1.0f));
		//input.binormal = normalize(float3(1.0f, dz, 0.0f));

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
	
	GBUFFER_RESOURCE(0, 1, 2, 3, 4);
	//SHADOWMAPS_RESOURCE(4);

	Texture2D beautyTexture : register(t5);

	Texture2D shaderTexture : register(t6);
	Texture2D normalMap : register(t7);
	Texture2D specularMap : register(t8);
	Texture2D noiseMap : register(t9);
	Texture2D heightMap : register(t10);
	Texture2D waterMask : register(t11);

	SamplerState g_TexSamplerAniso : register(s0);
	SamplerComparisonState g_cmpSampler : register(s1);
	SamplerState g_TexSamplerPoint : register(s2);

	bool IsInMaskedRegion(float2 screenPos)
	{
		float4 mask = waterMask.Sample(g_TexSamplerPoint, screenPos);

		// are we outside the masked water? just return the pixels
		if (mask.r == 0.0f)
			return false;

		return true;
	}

	float4 GetReflection(float3 eyeDir, float3 worldPos, float3 worldNormal, float4 originalColour, float currentDepth)
	{
		float3 rayStart = worldPos + worldNormal * 0.25f;
		float3 rayDir = normalize(reflect(eyeDir, worldNormal));

		const int stepCount = 24;
		const int refinementStepCount = 5;
		const float minStepLen = 2.0f;
		const float maxStepLen = 8.0f;
		const float baseThickness = 2.0f;

		float3 fragPos = rayStart;
		float3 previousFragPos = fragPos;
		float totalDistanceTravelled = 0.0f;
		float previousDistanceTravelled = 0.0f;
		float2 texCoord = 0.0f;
		float actualDepth = currentDepth;

		[loop]
		for (int i = 0; i < stepCount; ++i)
		{
			const float marchFraction = (float)i / (float)(stepCount - 1);
			const float stepLen = lerp(minStepLen, maxStepLen, marchFraction * marchFraction);
			const float thickness = baseThickness + totalDistanceTravelled * 0.02f;

			previousFragPos = fragPos;
			previousDistanceTravelled = totalDistanceTravelled;
			fragPos += rayDir * stepLen;
			totalDistanceTravelled += stepLen;

			float4 fragScr = float4(fragPos.xyz, 1.0f);
			float4 fragView = mul(fragScr, g_viewMatrix);
			float4 fragClip = mul(fragView, g_projectionMatrix);
			fragClip.xyz /= fragClip.w;

			float fragDepth = -fragView.z;
			fragClip.xy = fragClip.xy * 0.5 + 0.5;
			float2 fragTex = float2(fragClip.x, 1.0f - fragClip.y);

			if (fragTex.x < 0.0f || fragTex.x > 1.0f || fragTex.y < 0.0f || fragTex.y > 1.0f)
				return originalColour;

			actualDepth = GBUFFER_NORMAL.Sample(g_TexSamplerPoint, fragTex).w;
			float fragHeight = GBUFFER_POSITION.Sample(g_TexSamplerPoint, fragTex).y;
			bool isOnCorrectPlane = fragHeight >= rayStart.y;

			if (g_eyePos.y <= 0.0f)
				isOnCorrectPlane = fragHeight < rayStart.y;

			if ((fragDepth >= actualDepth - thickness) && isOnCorrectPlane && actualDepth > currentDepth)
			{
				float3 refineStart = previousFragPos;
				float3 refineEnd = fragPos;
				float refineStartDistance = previousDistanceTravelled;
				float refineEndDistance = totalDistanceTravelled;

				[loop]
				for (int j = 0; j < refinementStepCount; ++j)
				{
					float3 candidatePos = lerp(refineStart, refineEnd, 0.5f);
					float candidateDistance = lerp(refineStartDistance, refineEndDistance, 0.5f);
					float candidateThickness = baseThickness + candidateDistance * 0.02f;

					float4 candidateScr = float4(candidatePos.xyz, 1.0f);
					float4 candidateView = mul(candidateScr, g_viewMatrix);
					float4 candidateClip = mul(candidateView, g_projectionMatrix);
					candidateClip.xyz /= candidateClip.w;

					float candidateDepth = -candidateView.z;
					candidateClip.xy = candidateClip.xy * 0.5 + 0.5;
					float2 candidateTex = float2(candidateClip.x, 1.0f - candidateClip.y);

					if (candidateTex.x < 0.0f || candidateTex.x > 1.0f || candidateTex.y < 0.0f || candidateTex.y > 1.0f)
					{
						refineEnd = candidatePos;
						refineEndDistance = candidateDistance;
						continue;
					}

					float candidateActualDepth = GBUFFER_NORMAL.Sample(g_TexSamplerPoint, candidateTex).w;
					float candidateHeight = GBUFFER_POSITION.Sample(g_TexSamplerPoint, candidateTex).y;
					bool candidatePlane = candidateHeight >= rayStart.y;
					if (g_eyePos.y <= 0.0f)
						candidatePlane = candidateHeight < rayStart.y;

					if ((candidateDepth >= candidateActualDepth - candidateThickness) && candidatePlane && candidateActualDepth > currentDepth)
					{
						refineEnd = candidatePos;
						refineEndDistance = candidateDistance;
						fragTex = candidateTex;
						actualDepth = candidateActualDepth;
					}
					else
					{
						refineStart = candidatePos;
						refineStartDistance = candidateDistance;
					}
				}

				return beautyTexture.Sample(g_TexSamplerPoint, fragTex);
			}

			texCoord = fragTex;
		}

		if (actualDepth > currentDepth)
			return beautyTexture.Sample(g_TexSamplerPoint, texCoord);

		return originalColour;
	}

	float4 GetWorldColour(float3 eyeDir, inout float2 screenPos, float3 worldNormal, float4 originalWorldDiffuse, inout bool isInMaskedRegion, float3 pixelPos, float pixelDepth)
	{
		//return float4(0,1,0,1);
		//return originalWorldDiffuse;

		isInMaskedRegion = true;

		/*if (IsInMaskedRegion(screenPos) == false)
		{
			isInMaskedRegion = false;
			return originalWorldDiffuse;
		}*/

		float eta = 0.75f;// 0.5f;// -eyeDir.y - worldNormal.y;

		//if (g_eyePos.y <= 0.0f)
		//	eta = 1.33f;

		float2 origScreenPos = screenPos;

		float3 refractedNormal = refract(eyeDir, -(worldNormal), eta);

#if 0
		float3 rayStart = pixelPos;
		const float stepLen = 0.1f;

		rayStart += refractedNormal * stepLen;

		float4 fragScr = float4(rayStart.xyz, 1.0f);

		float4 fragView = mul(fragScr, g_viewMatrix);
		float4 fragClip = mul(fragView, g_projectionMatrix);

		fragClip.xyz /= fragClip.w;

		fragClip.xy = fragClip.xy * 0.5 + 0.5; // is this needed?

		float2 fragTex = float2(fragClip.x, 1.0f - fragClip.y);

		if (fragTex.x < 0.0f || fragTex.x > 1.0f || fragTex.y < 0.0f || fragTex.y > 1.0f)
		{
			screenPos = origScreenPos;
			return originalWorldDiffuse;
		}

		screenPos = float2(fragTex.x, fragTex.y);

#else
		// fire a ray into the world to get the ending position

		// we must be inside it, calculate jitter based on the normal
		float4 jitterNormal = float4(refractedNormal/*-worldNormal.xyz*/, 0.0f);
		jitterNormal = mul(jitterNormal, g_viewProjectionMatrix);
		//jitterNormal = mul(jitterNormal, g_projectionMatrix);

		const float jitterAmmount = 0.018f;

		jitterNormal = jitterNormal * jitterAmmount;

		//jitterNormal.xy = float2(jitterNormal.x / (float)g_screenWidth, jitterNormal.y / (float)g_screenHeight);

		screenPos = /* saturate */(screenPos + jitterNormal.xy);
#endif

		if (screenPos.x < 0.0f || screenPos.x > 1.0f || screenPos.y < 0.0f || screenPos.y > 1.0f)
		{
			screenPos = origScreenPos;
			return originalWorldDiffuse;
		}

		/*if (IsInMaskedRegion(screenPos) == false)
		{
			isInMaskedRegion = false;
			return originalWorldDiffuse;
		}*/

		float fragDepth = GBUFFER_NORMAL.Sample(g_TexSamplerPoint, screenPos).w;

		if(fragDepth < pixelDepth)
		{
			screenPos = origScreenPos;
			return originalWorldDiffuse;
		}

		// resample the world at the jittered position
		float4 jitterDiffuse = beautyTexture.Sample(g_TexSamplerPoint, screenPos);
		jitterDiffuse.a = 1.0f;

		return jitterDiffuse;
	}

	float4 CalculateCaustics(float3 lightDir, float3 worldNormal, float3 worldPos, float pixelDepth, float4 originalColour)
	{
		float3 rayDir = refract(lightDir, worldNormal, 0.00001f);
		float rayLength = 64.0f;

		float3 rayEndPos = worldPos + rayDir * rayLength;

		float4 dpethOfPixel = mul(float4(rayEndPos, 1.0f), g_viewMatrix);
		//dpethOfPixel = mul(dpethOfPixel, g_projectionMatrix);

		//float2 projectTexCoord;

		//projectTexCoord.x = dpethOfPixel.x / dpethOfPixel.w / 2.0f + 0.5f;
		//projectTexCoord.y = -dpethOfPixel.y / dpethOfPixel.w / 2.0f + 0.5f;

		//if ((saturate(projectTexCoord.x) == projectTexCoord.x) && (saturate(projectTexCoord.y) == projectTexCoord.y))
		float newPixelDepth = -dpethOfPixel.z;// / dpethOfPixel.w;

		// lookup depth
		//float4 depth = GBUFFER_DEPTH.Sample(TextureSampler, projectTexCoord.xy);

		if (newPixelDepth >= pixelDepth)
			return float4(1, 0, 0, 1.0f);//return float4(originalColour.rgb * 0.05f, 1.0f);

		return float4(0, 0, 0, 0.0f);
	}

	float3 ANM(float3 worldNormal, float3 tangent, float3 binormal, Texture2D normalMap, SamplerState samp, float2 texcoord, bool flipY = false, float bumMapMultiplier = 1.0f)
	{
		// Sample the pixel in the bump map.
		float3 bumpMap = normalMap.Sample(samp, texcoord).xyz;

		// flip the Y channel
		//if(flipY)
			//bumpMap.y = 1.0f - bumpMap.y;

		// Expand the range of the normal value from (0, +1) to (-1, +1).
		//bumpMap = (bumpMap * 2.0f) - 1.0f;

		//bumpMap = bumpMap * float3(bumMapMultiplier, bumMapMultiplier, 1.0f);

		// Calculate the normal from the data in the bump map.
		float3 bumpNormal = (bumpMap.x * tangent * 1.0f) + (bumpMap.y * binormal * 1.0f) + (bumpMap.z * worldNormal);
		//float3 bumpNormal = (bumpMap.x * tangent) + (bumpMap.y * binormal) + (bumpMap.z * worldNormal);

		// bumpNormal = normal + bumpMap.x * tangent + bumpMap.y * binormal;

		// Normalize the resulting bump normal.
		worldNormal = (bumpNormal);

		return worldNormal;
	}

	float4 ShaderMain(MeshPixelInput input) : SV_Target
	{
		//return float4(1,0,0,1);
		float4 albedo = shaderTexture.Sample(g_TexSamplerAniso, input.texcoord);// * input.colour;
		//return albedo;
		//float4 color = albedo * ambientColor;

		float3 reflection;
		float4 specular = float4(0,0,0,1);

		float3 eyeVector = normalize(g_eyePos.xyz - input.positionWS.xyz);// normalize(g_eyePos.xyz - input.positionWS.xyz);
		const float cameraDistance = length(input.positionWS.xyz - g_eyePos.xyz);
		const float nearQualityDistance = g_oceanConfig.reflectionNearDistance;
		const float midQualityDistance = lerp(g_oceanConfig.reflectionNearDistance, g_oceanConfig.reflectionFarDistance, 0.6f);
		const float farQualityDistance = max(g_oceanConfig.reflectionFarDistance, midQualityDistance + 1.0f);
		const float refractionQualityWeight = 1.0f - saturate((cameraDistance - nearQualityDistance) / max(midQualityDistance - nearQualityDistance, 1.0f));
		const float ssrQualityWeight = 1.0f - saturate((cameraDistance - nearQualityDistance) / max(farQualityDistance - nearQualityDistance, 1.0f));
		const float distantNormalFade = ssrQualityWeight;
		//float3 lightVector = normalize(input.positionWS.xyz - g_lightPosition.xyz);
		float3 worldNormal = normalize(input.normal.xyz);
		float3 refractionNormal = -worldNormal;

		if (g_eyePos.y <= 0.0f)
		{
			//refractionNormal *= -1.0f;
			//worldNormal *= -1.0f;

			//worldNormal.y *= -1.0f;
			//refractionNormal.y *= -1.0f;
		}

		//return float4(input.normal,1);

		float3 originalWorldNormal = worldNormal;
		
		float3 lightDir = -normalize(g_lightDirection.xyz);

		float4 worldViewPosition = mul(input.positionWS, g_viewMatrix);
		float pixelDepth = -worldViewPosition.z;

		float2 screenPos = float2(input.position.x / (float)g_screenWidth /** 2*/, input.position.y / (float)g_screenHeight /** 2*/);		

		

		// get the original diffuse colour
		float4 worldDiffuse = GBUFFER_DIFFUSE.Sample(g_TexSamplerPoint, screenPos);
		worldDiffuse.a = 1.0f;

		// make a copy, we might need this again
		float4 originalWorldDiffuse = worldDiffuse;

		// BUMP MAPPING
		if (distantNormalFade > 0.001f)
		{
			float3 bumpNormal = normalize(ANM(worldNormal, input.tangent, input.binormal, normalMap, g_TexSamplerAniso, input.texcoord, false, 1.0f));
			bumpNormal = normalize(lerp(input.normal.xyz, bumpNormal, distantNormalFade));

			refractionNormal = bumpNormal;
			worldNormal = normalize(bumpNormal);
		}

		

		//return float4(worldNormal.xyz, 1.0f);

		float4 normalAndDepth = GBUFFER_NORMAL.Sample(g_TexSamplerPoint, screenPos);
		float worldDepth = normalAndDepth.w;

		bool isInMaskedRegion = false;

		if (refractionQualityWeight > 0.001f && (worldDepth >= pixelDepth || worldDepth == -1.0f))
		{			
			float4 refractedWorldDiffuse = GetWorldColour(-eyeVector, screenPos, /* worldNormal */refractionNormal, worldDiffuse, isInMaskedRegion, input.positionWS.xyz, pixelDepth);
			worldDiffuse = lerp(originalWorldDiffuse, refractedWorldDiffuse, refractionQualityWeight);

			// sample the other buffers using the corrected jitter positions
			//
			normalAndDepth = GBUFFER_NORMAL.Sample(g_TexSamplerPoint, screenPos);

			worldDepth = normalAndDepth.w;

			//if (normalAndDepth.w != -1 /*|| g_eyePos.y <= 0.0f*/)
			//{
			//	if (normalAndDepth.w < pixelDepth)
			//		worldDiffuse = originalWorldDiffuse;
			//	else
			//		worldDepth = normalAndDepth.w;
			//}

			//return worldDiffuse;
		}

		// Correct for gamma
		//worldDiffuse = float4(pow(worldDiffuse.rgb, g_gamma), worldDiffuse.a);
		
		//float4 positions = GBUFFER_POSITION.Sample(TextureSampler, screenPos);
		
		//if(isInMaskedRegion)
		//	worldDiffuse += CalculateCaustics(lightVector, worldNormal, input.positionWS.xyz, pixelDepth, worldDiffuse);
		//if (isInMaskedRegion == false)
		//	clip(-1);

		float lightIntensity = dot(worldNormal, lightDir) * g_globalLight[0];
		
		if (lightIntensity > 0.0f)
		{
			float4 lightColour = float4(1, 1, 1, 1);

			// Determine the final diffuse color based on the diffuse color and the amount of light intensity.					
			//color = /*albedo*/float4(lightColour.rgb * lightIntensity, albedo.a);

			// Saturate the final light color.
			//color = saturate(color);

			// Calculate the reflection vector based on the light intensity, normal vector, and light direction.
			reflection = normalize(2 * lightIntensity * worldNormal - lightDir);

			float shinyPower = 120;//g_material.shininess;

			if (shinyPower < 10.0f)
				shinyPower = 10.0f;

			float rDotL = dot(reflection, eyeVector);

			// Determine the amount of specular light based on the reflection vector, viewing direction, and specular power.
			specular = float4(getSunColour(), 1.0f) * pow(saturate(rDotL), shinyPower);// * g_material.shininessStrength;

			//return specular;
			if ((g_objectFlags & OBJECT_FLAGS_HAS_ROUGHNESS) != 0)
			{
				//float4 specularIntensity = specularMap.Sample(g_TexSamplerAniso, input.texcoord);

				//specular = specular * specularIntensity.r;// *depthValue2;
			}

			//specular = min(specular, albedo.a);
		}

		//return float4(lightIntensity, lightIntensity, lightIntensity, 1.0f);

		// calculate the view-space pixel depth

		float waterDepth = pixelDepth;
		//float worldDepth = normalAndDepth.a;
		//float terrainDepth = positions.w;
		float depthDifference = (worldDepth - waterDepth);
		float relativeDepth = worldDepth == -1.0f ? 1.0f : saturate(depthDifference / g_frustumDepths[3]);

		


		//const float4 shallowColour = float4(63.0f / 255.0f, 155.0f / 255.0f, 205.0f / 255.0f, 1.0f);// *albedo;
		//const float4 deepColour = float4(20.0f / 255.0f, 51.0f / 255.0f, 75.0f / 255.0f, 1.0f);// *albedo;

		const float fresnelPow = g_oceanConfig.fresnelPow;// 3.201f;
		const float shoreFadeStrength = g_oceanConfig.shoreFadeStrength;// 12.0f;

		//return float4(relativeDepth, relativeDepth, relativeDepth, 1.0f);

		float depthMultiplier = saturate(relativeDepth * g_frustumDepths[3]);

		float4 worldAlbedoInfluence = float4(worldDiffuse.rgb * depthMultiplier, 1.0f/*albedo.a*/);

		float4 fadeColour = lerp(g_oceanConfig.shallowColour, g_oceanConfig.deepColour, saturate(1 - exp(-relativeDepth * g_oceanConfig.fadeFactor)));
		float fresnel = 1 - pow(saturate(dot(eyeVector, originalWorldNormal)), fresnelPow);// min(0.7, pow(saturate(dot(-eyeVector, worldNormal)), fresnelPow));
		float shoreFade = 1.0f - exp(-relativeDepth * shoreFadeStrength);

		float fadeFactor = saturate(fresnel * shoreFade);

		float4 ambient = float4(g_atmosphere.ambientLight.rgb * fadeColour.rgb, 1.0f);
		float4 diffuseColour = float4(fadeColour.rgb * lightIntensity, 1.0f);

		float depthValue = 1.0f;// CalculateShadows(input, g_cmpSampler, SHADOWMAPS, g_shadowBlendRange);

		float4 finalColour = diffuseColour /*+ ambient*/;// saturate(ambient + diffuseColour + specular);
		
		//finalColour.a = fadeFactor;

		//return float4(finalColour.xyz, 1.0f);

		float finalFadeFactor = fadeFactor;

		if (g_eyePos.y <= 0.0f)
			finalFadeFactor *= 0.35f;

		float4 retCol = lerp(worldDiffuse, finalColour, finalFadeFactor);

		
		
		//retCol = float4(retCol.rgb * depthValue, 1.0f);

		if (true/*  && isInMaskedRegion == true && g_eyePos.y > 0.0f */)
		{
			float3 reflectionNormal = worldNormal;

			if (g_eyePos.y <= 0.0f)
				reflectionNormal *= -1.0f;

			const float reflectionStrength = g_oceanConfig.reflectionStrength;

			float4 cheapReflectionCol = beautyTexture.Sample(g_TexSamplerPoint, screenPos);
			cheapReflectionCol.xyz = lerp(cheapReflectionCol.xyz, fadeColour.xyz, 0.25f);

			float4 reflectionCol = cheapReflectionCol;
			if (ssrQualityWeight > 0.001f)
			{
				float4 ssrReflectionCol = GetReflection(-eyeVector, input.positionWS.xyz, reflectionNormal, retCol, pixelDepth);
				reflectionCol = lerp(cheapReflectionCol, ssrReflectionCol, ssrQualityWeight);
			}

			retCol.xyz = saturate(lerp(retCol.xyz, reflectionCol.xyz + specular, reflectionStrength * fadeFactor));
		}

		retCol = saturate(retCol /* + specular */);

		// foam
		/*if (depthDifference < 1.0f && isInMaskedRegion)
		{
			retCol = retCol + (float4(0.3f, 0.3f, 0.3f, 0.0f) * (1.0f - depthDifference));
		}*/
		
		//return retCol;
		albedo = worldAlbedoInfluence;

		//return albedo * lightIntensity;

		//return float4(1,0,0,1);
		retCol.a = 1.0f;

		//return float4(1,0,0,1);

		//return retCol /* + worldAlbedoInfluence */ + ( albedo * lightIntensity) + specular;

		return retCol * lightIntensity + specular;

		//float4 finalColour = albedo * color;
		//color = finalColour;// color* albedo;

		

		//return color;
	}
}