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

	SamplerState g_textureSampler : register(s0);
	SamplerComparisonState g_cmpSampler : register(s1);
	SamplerState g_pointSampler : register(s2);

	//bool BroadPhaseDetection(float3 rayStart, float3 rayDir, float maxLen, float currentDepth, out float targetDepth, out float2 pixelEnd)
	//{
	//	float3 endPos = rayStart + rayDir * maxLen;
	//	pixelEnd = pixelStart;

	//	float4 fragView = mul(float4(endPos, 1.0f), g_viewMatrix);
	//	float4 fragClip = mul(fragView, g_projectionMatrix);

	//	fragClip.xyz /= fragClip.w;

	//	float fragDepth = -fragView.z;// / fragView.w;

	//	fragClip.xy = fragClip.xy * 0.5 + 0.5; // is this needed?

	//	

	//	float2 fragTex = float2(fragClip.x, fragClip.y);

	//	if (fragTex.x < 0.0f || fragTex.x > 1.0f || fragTex.y < 0.0f || fragTex.y > 1.0f)
	//		return false;

	//	float actualDepth = GBUFFER_NORMAL.Sample(
	//		g_pointSampler,
	//		fragTex).w;

	//	if ((fragDepth >= actualDepth/* && actualDepth > currentDepth*/) /*|| fragDepth == g_frustumDepths[3]*/)
	//	{
	//		targetDepth = actualDepth;
	//		pixelEnd = float2(fragClip.x * g_screenWidth, fragClip.y * g_screenHeight);
	//		return true;
	//	}

	//	return false;
	//}

	static const float PI = 3.1415;

	uint NextRandom(inout uint state)
	{
		state = state * 747796405 + 2891336453;
		uint result = ((state >> ((state >> 28) + 4)) ^ state) * 277803737;
		result = (result >> 22) ^ result;
		return result;
	}

	float RandomValue(inout uint state)
	{
		return NextRandom(state) / 4294967295.0; // 2^32 - 1
	}

	float RandomValueNormalDistribution(inout uint state)
	{
		// Thanks to https://stackoverflow.com/a/6178290
		float theta = 2 * 3.1415926 * RandomValue(state);
		float rho = sqrt(-2 * log(RandomValue(state)));
		return rho * cos(theta);
	}

	float3 RandomDirection(inout uint state)
	{
		// Thanks to https://math.stackexchange.com/a/1585996
		float x = RandomValueNormalDistribution(state);
		float y = RandomValueNormalDistribution(state);
		float z = RandomValueNormalDistribution(state);
		return normalize(float3(x, y, z));
	}

	float3 RandomDirectionInDirectionOfNormal(float3 normal, inout uint state)
	{
		// Thanks to https://math.stackexchange.com/a/1585996
		float x = RandomValueNormalDistribution(state);
		float y = RandomValueNormalDistribution(state);
		float z = RandomValueNormalDistribution(state);

		float3 generatedNormal = normalize(float3(x, y, z));

		if(dot(generatedNormal, normal) < 0.0f)
			generatedNormal = generatedNormal * -1.0f;

		return generatedNormal;
	}

	float2 RandomPointInCircle(inout uint rngState)
	{
		float angle = RandomValue(rngState) * 2 * PI;
		float2 pointOnCircle = float2(cos(angle), sin(angle));
		return pointOnCircle * sqrt(RandomValue(rngState));
	}

	float4 GetReflection(
		float3 eyeDir,
		float3 worldPos,
		float3 worldNormal,
		float4 originalColour,
		float currentDepth,
		inout bool didReflect,
		out float hitDistance,
		float2 screenPosUV,
		float3 noise,
		inout uint rngState,
		float smoothness,
		float specularProbability,
		uint instanceID)
	{
		didReflect = false;
		hitDistance = 0.0f;

		// fire a ray
		float3 rayStart = worldPos;// +RandomPointInCircle(rngState).xy;

		float initialWorldPosFromEye = length(worldPos - g_eyePos.xyz);

		//rayStart += eyeDir + noise * 2.0f;

		float3 diffuseDir = normalize(worldNormal + RandomDirectionInDirectionOfNormal(worldNormal, rngState));
		float3 specularDir = normalize(reflect(eyeDir, worldNormal));

		bool isSpecularBounce = specularProbability >= RandomValue(rngState);

		float3 rayDir = normalize(lerp(diffuseDir, specularDir, smoothness * isSpecularBounce));

		//didReflect = true;
		//return float4(rayDir, 1.0f);


		const int stepCount = 26;
		const int refinementStepCount = 5;
		const float minStepLen = 2.0f;
		const float maxStepLen = 8.0f;
		const float baseThickness = 2.0f;

		float3 fragPos = rayStart + worldNormal * 0.25f;

		/*const float2 RandomSamples[4] = {
			float2(-1.0f, -1.0f),
			float2(1.0f, 1.0f),
			float2(-1.0f, 1.0f),
			float2(1.0f, -1.0f),
		};*/

		/*float targetDepth = 0.0f;
		float2 pixelEnd;
		if (BroadPhaseDetection(rayStart, rayDir, (float)stepCount * stepLen, currentDepth, targetDepth, pixelEnd) == false)
			return originalColour;*/

		float3 rayColour = 1;//originalColour.rgb;

		const int MaxBounces = 1;
		int NumBounces = 0;

		float totalDistanceTravelled = 0.0f;

		[loop]
		for (int i = 0; i < stepCount; ++i)
		{
			const float marchFraction = (float)i / (float)(stepCount - 1);
			const float stepLen = lerp(minStepLen, maxStepLen, marchFraction * marchFraction);
			const float thickness = baseThickness + totalDistanceTravelled * 0.02f + (1.0f - smoothness) * 1.5f;

			float3 previousFragPos = fragPos;
			float previousDistanceTravelled = totalDistanceTravelled;

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
			{
				if (NumBounces == 0)
					return float4(originalColour.rgb, 1.0f);

				return float4(rayColour, 1.0f);
			}

			float4 actualDepthAndNormal = GBUFFER_NORMAL.Sample(g_pointSampler, fragTex);
			float actualDepth = actualDepthAndNormal.w;
			bool didHitSky = actualDepth == g_frustumDepths[3];

			if (fragDepth >= actualDepth - thickness || didHitSky)
			{
				if (!didHitSky)
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
						float candidateThickness = baseThickness + candidateDistance * 0.02f + (1.0f - smoothness) * 1.5f;

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

						float4 candidateDepthAndNormal = GBUFFER_NORMAL.Sample(g_pointSampler, candidateTex);
						bool candidateHitSky = candidateDepthAndNormal.w == g_frustumDepths[3];
						uint candidateInstanceId = (uint)GBUFFER_DIFFUSE.Sample(g_pointSampler, candidateTex).w;
						bool candidateBlocked = candidateHitSky || ((candidateDepth >= candidateDepthAndNormal.w - candidateThickness) && candidateInstanceId != instanceID);

						if (candidateBlocked)
						{
							refineEnd = candidatePos;
							refineEndDistance = candidateDistance;
							fragPos = candidatePos;
							totalDistanceTravelled = candidateDistance;
							fragScr = candidateScr;
							fragView = candidateView;
							fragClip = candidateClip;
							fragDepth = candidateDepth;
							fragTex = candidateTex;
							actualDepthAndNormal = candidateDepthAndNormal;
							didHitSky = candidateHitSky;
						}
						else
						{
							refineStart = candidatePos;
							refineStartDistance = candidateDistance;
						}
					}
				}
				float4 colourAndEmissive = g_beautyTexture.Sample(g_pointSampler, fragTex);
				float4 pixelPosWS = GBUFFER_POSITION.Sample(g_pointSampler, fragTex);
				float4 pixelSpecular = GBUFFER_SPECULAR.Sample(g_pointSampler, fragTex);

				uint newInstanceId = (uint)GBUFFER_DIFFUSE.Sample(g_pointSampler, fragTex).w;

				if(newInstanceId == instanceID && !didHitSky)
				{
					//i--;
					continue;
				}

				didReflect = true;

				float3 lastRayColour = rayColour;
				rayColour *= colourAndEmissive.rgb;

				// if we hit an emissive object just return
				if(pixelPosWS.w > 0.0f)
				{
                    hitDistance = max(length(pixelPosWS.xyz - worldPos), 0.0f);
					return float4(rayColour.rgb, 1.0f);
				}


				//eyeDir = normalize(pixelPosWS.xyz - g_eyePos.xyz);

				//diffuseDir = normalize(actualNormal + RandomDirectionInDirectionOfNormal(actualNormal, rngState));
				//specularDir = normalize(reflect(rayDir, actualNormal));

				//worldNormal = actualNormal;

				//smoothness = pixelSpecular.b;
				//specularProbability = pixelSpecular.a;

				//isSpecularBounce = specularProbability >= RandomValue(rngState);

				//rayDir = normalize(lerp(diffuseDir, specularDir, smoothness * isSpecularBounce));

				//float p = max(rayColour.r, max(rayColour.g, rayColour.b));
				//if (RandomValue(rngState) >= p) {
				//	break;
				//}
				//rayColour *= 1.0f / p;				

                hitDistance = didHitSky ? max(totalDistanceTravelled, 0.0f) : max(length(pixelPosWS.xyz - worldPos), 0.0f);
				NumBounces++;

				if(didHitSky)
				{
					if(totalDistanceTravelled >= initialWorldPosFromEye)
						break;

					if(NumBounces > 1)
						rayColour = lastRayColour;						
				}
				else
				{
					if (NumBounces >= MaxBounces /*|| didHitSky*/)
						break;
				}

				break;
			}

			// nothing hit, so check surrounding pixels
			//[loop]
			//for (int j = 0; j < 4; ++j)
			//{
			//	float2 texOffset = float2(1.0f / g_screenWidth, 1.0f / g_screenHeight);

			//	float2 texCoord = float2(fragTex.x, fragTex.y) + float2(texOffset.x * RandomSamples[j].x, texOffset.y * RandomSamples[j].y);

			//	// sample the depth of the world
			//	float actualDepth = GBUFFER_NORMAL.Sample(
			//		g_pointSampler,
			//		texCoord).w;

			//	if(abs(actualDepth - fragDepth) <= 2.0f)
			//	{					
			//		didReflect = true;
			//		return GBUFFER_DIFFUSE.Sample(g_pointSampler, float2(texCoord.x, 1.0f - texCoord.y)).rgba;
			//	}
			//}
		}

		if(NumBounces == 0)
			return float4(0.0f, 0.0f, 0.0f, 0.0f);

		hitDistance = totalDistanceTravelled;
		return float4(rayColour, 1.0f);
	}

	/*struct SSR_OUT
	{
		float4 reflection : SV_Target0;
		float4 mask;
	};*/

	SSROut ShaderMain(UIPixelInput input)
	{
		SSROut ssr = (SSROut)0;

		float2 texcoord = input.texcoord;
		//float4 originalColour = g_originalTexture.Sample(g_pointSampler, texcoord);

		float2 screenPos = float2(input.position.x / (float)g_screenWidth, input.position.y / (float)g_screenHeight);

		

		// Sample the gbuffer
		//
		
		float4 pixelSpecular = GBUFFER_SPECULAR.Sample(g_pointSampler, screenPos);
		float4 pixelColour = g_beautyTexture.Sample(g_pointSampler, screenPos);
		float4 pixelDiffuse = GBUFFER_DIFFUSE.Sample(g_pointSampler, screenPos);
		float4 pixelNormal = GBUFFER_NORMAL.Sample(g_pointSampler, screenPos);
		float4 pixelPosWS = GBUFFER_POSITION.Sample(g_pointSampler, screenPos);

		float smoothness = pixelSpecular.b;
		float metalness = pixelSpecular.r;
		float specularProbability = pixelSpecular.a;
		float diffuseWeight = saturate((1.0f - metalness) * (1.0f - smoothness));
		float specularWeight = saturate(smoothness);
		float3 diffuseSurfaceColour = saturate(pixelDiffuse.rgb);

		if (smoothness <= 0.0f/* diffuseWeight <= 0.0f && specularWeight <= 0.0f */)
		{
			ssr.diff = float4(0, 0, 0, 0);
			ssr.diffHitInfo = float4(0, 0, 0, 0);
			ssr.spec = float4(0, 0, 0, 0);
			ssr.specHitInfo = float4(0, 0, 0, 0);
			return ssr;
		}

		uint instanceID = (uint)GBUFFER_DIFFUSE.Sample(g_pointSampler, screenPos).w;
		uint2 numPixels = uint2(g_screenWidth, g_screenHeight);
		uint2 pixelCoord = screenPos * numPixels;
		uint pixelIndex = pixelCoord.y * numPixels.x + pixelCoord.x;
		float3 eyeVector = normalize(pixelPosWS.xyz - g_eyePos.xyz);
		float2 noiseSamplePos = screenPos * 64;
		noiseSamplePos += frac(g_time) * 100.0f;
		float3 noise = g_noiseTexture.Sample(g_pointSampler, noiseSamplePos).rgb;
		uint baseRngState = pixelIndex + 719393 + noise.r * 3654 + noise.g * 1232 + 1540 * noise.b;
		float depth = pixelNormal.w;

		float3 finalDiffuseColour = 0.0f;
		float3 finalSpecularColour = 0.0f;
		float diffuseHitDistanceAccum = 0.0f;
		float specularHitDistanceAccum = 0.0f;
		float diffuseHitCount = 0.0f;
		float specularHitCount = 0.0f;
		bool hadDiffuseReflection = false;
		bool hadSpecularReflection = false;

		const uint DiffuseRays = 1;
		const uint SpecularRays = 2;

		if (diffuseWeight > 0.0f)
		{
			uint diffuseRngState = baseRngState ^ 0x68bc21ebu;
			[loop]
			for (uint i = 0; i < DiffuseRays; ++i)
			{
				bool didReflect = false;
				float hitDistance = 0.0f;
				float4 reflectedColour = GetReflection(
					eyeVector,
					pixelPosWS.xyz,
					pixelNormal.xyz,
					pixelColour,
					depth,
					didReflect,
					hitDistance,
					screenPos,
					noise,
					diffuseRngState,
					smoothness,
					0.0f,
					instanceID
				);

				if (didReflect)
				{
					finalDiffuseColour += reflectedColour.rgb;
					diffuseHitDistanceAccum += hitDistance;
					diffuseHitCount += 1.0f;
					hadDiffuseReflection = true;
				}
			}
		}

		if (specularWeight > 0.0f)
		{
			uint specularRngState = baseRngState ^ 0x2c1b3c6du;
			[loop]
			for (uint i = 0; i < SpecularRays; ++i)
			{
				bool didReflect = false;
				float hitDistance = 0.0f;
				float4 reflectedColour = GetReflection(
					eyeVector,
					pixelPosWS.xyz,
					pixelNormal.xyz,
					pixelColour,
					depth,
					didReflect,
					hitDistance,
					screenPos,
					noise,
					specularRngState,
					smoothness,
					max(specularProbability, 1.0f),
					instanceID
				);

				if (didReflect)
				{
					finalSpecularColour += reflectedColour.rgb;
					specularHitDistanceAccum += hitDistance;
					specularHitCount += 1.0f;
					hadSpecularReflection = true;
				}
			}
		}

		float3 diffuseColour = diffuseHitCount > 0.0f ? finalDiffuseColour / diffuseHitCount : 0.0f;
		float3 specularColour = specularHitCount > 0.0f ? finalSpecularColour / specularHitCount : 0.0f;
		float averageDiffuseHitDistance = diffuseHitCount > 0.0f ? diffuseHitDistanceAccum / diffuseHitCount : 0.0f;
		float averageSpecularHitDistance = specularHitCount > 0.0f ? specularHitDistanceAccum / specularHitCount : 0.0f;

		ssr.diff = float4(diffuseColour * diffuseSurfaceColour * diffuseWeight, hadDiffuseReflection ? 1.0f : 0.0f);
		ssr.diffHitInfo = float4(0, 0, 0, averageDiffuseHitDistance);
		ssr.spec = float4(specularColour * specularWeight, hadSpecularReflection ? 1.0f : 0.0f);
		ssr.specHitInfo = float4(0, 0, 0, averageSpecularHitDistance);

		return ssr;
	}
}




