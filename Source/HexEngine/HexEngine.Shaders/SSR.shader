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
	Texture2D g_giFallbackTexture : register(t9);

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

	float4 SampleSSRHistory(float2 screenPosUV)
	{
		const float2 velocity = g_velocityTexture.Sample(g_pointSampler, screenPosUV).xy;
		const float2 historyUv = saturate(screenPosUV + velocity);
		return g_historyTexture.Sample(g_textureSampler, historyUv);
	}

	float3 SampleGiFallback(float2 screenPosUV, float roughness)
	{
		const float3 gi = g_giFallbackTexture.Sample(g_textureSampler, screenPosUV).rgb;
		const float giWeight = saturate((roughness - 0.3f) / 0.4f);
		return gi;// * giWeight;
	}

	float GetViewZ(float3 worldPos)
	{
		return mul(float4(worldPos, 1.0f), g_viewMatrix).z;
	}

	float4 GetReflection(
		float3 eyeDir,
		float3 worldPos,
		float3 worldNormal,
		float4 originalColour,
		float currentDepth,
		inout bool didReflect,
		out float hitDistance,
		out float2 hitUv,
		out float virtualViewZ,
		float2 screenPosUV,
		float3 noise,
		inout uint rngState,
		float smoothness,
		float specularProbability,
		uint instanceID)
	{
		didReflect = false;
		hitDistance = 0.0f;
		hitUv = screenPosUV;
		virtualViewZ = 0.0f;

		// fire a ray
		float3 rayStart = worldPos;// +RandomPointInCircle(rngState).xy;
		const float eyePlaneSide = dot(g_eyePos.xyz - rayStart, worldNormal);

		float initialWorldPosFromEye = length(worldPos - g_eyePos.xyz);

		//rayStart += eyeDir + noise * 2.0f;

		float roughness = saturate(1.0f - smoothness);
		float3 diffuseDir = normalize(worldNormal + RandomDirectionInDirectionOfNormal(worldNormal, rngState));
		float3 specularDir = normalize(reflect(eyeDir, worldNormal));

		bool isSpecularBounce = specularProbability >= RandomValue(rngState);
		float3 glossyDir = normalize(lerp(specularDir, RandomDirectionInDirectionOfNormal(specularDir, rngState), roughness * roughness));
		float3 rayDir = isSpecularBounce ? glossyDir : diffuseDir;

		//didReflect = true;
		//return float4(rayDir, 1.0f);


		const int stepCount = (roughness < 0.2f) ? 48 : ((roughness < 0.45f) ? 36 : 28);
		const int refinementStepCount = (roughness < 0.2f) ? 7 : 5;
		const float minStepLen = lerp(0.18f, 0.5f, roughness);
		const float maxStepLen = lerp(1.1f, 3.2f, roughness);
		const float baseThickness = lerp(0.06f, 1.5f, roughness);
		const float thicknessGrowth = lerp(0.0015f, 0.03f, roughness);
		const float selfIntersectionDistance = lerp(0.08f, 0.8f, roughness);

		const float marchJitter = RandomValue(rngState);
		float totalDistanceTravelled = marchJitter * minStepLen;
		float3 fragPos = rayStart + worldNormal * 0.25f + rayDir * totalDistanceTravelled;

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
		float2 lastFragTex = screenPosUV;
		float lastActualDepth = currentDepth;

		const int MaxBounces = 1;
		int NumBounces = 0;

		[loop]
		for (int i = 0; i < stepCount; ++i)
		{
			const float marchFraction = ((float)i + marchJitter) / (float)stepCount;
			const float stepLen = lerp(minStepLen, maxStepLen, marchFraction * marchFraction);
			const float thickness = baseThickness + totalDistanceTravelled * thicknessGrowth;

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
				if (lastActualDepth >= g_frustumDepths[3] * 0.999f)
				{
					didReflect = true;
					hitDistance = max(length(fragPos - worldPos), 0.0f);
					hitUv = lastFragTex;
					virtualViewZ = GetViewZ(fragPos);
					return float4(g_beautyTexture.Sample(g_textureSampler, lastFragTex).rgb, 1.0f);
				}

				return float4(0.0f, 0.0f, 0.0f, 0.0f);
			}

			float4 actualDepthAndNormal = GBUFFER_NORMAL.Sample(g_pointSampler, fragTex);
			float4 actualPosWS = GBUFFER_POSITION.Sample(g_pointSampler, fragTex);
			float actualDepth = actualDepthAndNormal.w;
			lastFragTex = fragTex;
			lastActualDepth = actualDepth;

			float planeDistance = dot(actualPosWS.xyz - rayStart, worldNormal);
			bool isOnCorrectPlane = eyePlaneSide >= 0.0f ? planeDistance >= 0.0f : planeDistance <= 0.0f;
			bool isSceneDepth = actualDepth < g_frustumDepths[3] * 0.999f;
			bool depthMatched = isSceneDepth && (fragDepth >= actualDepth - thickness) && (fragDepth <= actualDepth + thickness * 2.0f) && actualDepth > currentDepth && isOnCorrectPlane;

			if (depthMatched )
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
					float candidateThickness = baseThickness + candidateDistance * thicknessGrowth;

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
					float4 candidatePosWS = GBUFFER_POSITION.Sample(g_pointSampler, candidateTex);
					uint candidateInstanceId = (uint)GBUFFER_DIFFUSE.Sample(g_pointSampler, candidateTex).w;
					float candidatePlaneDistance = dot(candidatePosWS.xyz - rayStart, worldNormal);
					bool candidatePlaneMatch = eyePlaneSide >= 0.0f ? candidatePlaneDistance >= 0.0f : candidatePlaneDistance <= 0.0f;
					bool candidateIsSceneDepth = candidateDepthAndNormal.w < g_frustumDepths[3] * 0.999f;
					bool candidateDepthMatched = candidateIsSceneDepth && (candidateDepth >= candidateDepthAndNormal.w - candidateThickness) && (candidateDepth <= candidateDepthAndNormal.w + candidateThickness * 2.0f) && candidateDepthAndNormal.w > currentDepth && candidatePlaneMatch;
					bool candidateIsImmediateSelfHit = candidateInstanceId == instanceID && candidateDistance <= selfIntersectionDistance;
					bool candidateBlocked = candidateDepthMatched && !candidateIsImmediateSelfHit;

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
						lastFragTex = candidateTex;
						lastActualDepth = candidateDepthAndNormal.w;
					}
					else
					{
						refineStart = candidatePos;
						refineStartDistance = candidateDistance;
					}
				}
				float4 colourAndEmissive = g_beautyTexture.Sample(g_textureSampler, fragTex);
				float4 pixelPosWS = GBUFFER_POSITION.Sample(g_pointSampler, fragTex);

				didReflect = true;

				rayColour *= colourAndEmissive.rgb;

				// if we hit an emissive object just return
				if(pixelPosWS.w > 0.0f)
				{
					hitDistance = max(length(pixelPosWS.xyz - worldPos), 0.0f);
					hitUv = fragTex;
					virtualViewZ = GetViewZ(pixelPosWS.xyz);
					return float4(rayColour.rgb, 1.0f);
				}		

                hitDistance = max(length(pixelPosWS.xyz - worldPos), 0.0f);
				hitUv = fragTex;
				virtualViewZ = GetViewZ(pixelPosWS.xyz);
				NumBounces++;

				if (NumBounces >= MaxBounces)
					break;

				break;
			}
		}

		if(NumBounces == 0)
		{
			if (lastActualDepth >= g_frustumDepths[3] * 0.999f)
			{
				didReflect = true;
				hitDistance = max(length(fragPos - worldPos), 0.0f);
				hitUv = lastFragTex;
				virtualViewZ = GetViewZ(fragPos);
				return float4(g_beautyTexture.Sample(g_textureSampler, lastFragTex).rgb, 0.0f);
			}

			return float4(0.0f, 0.0f, 0.0f, 0.0f);
		}

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

		float roughness = saturate(pixelSpecular.g);
		float smoothness = saturate(1.0f - roughness);
		float metalness = pixelSpecular.r;
		float specularProbability = pixelSpecular.a;
		float diffuseWeight = saturate(specularProbability * roughness);
		// Roughness shapes reflection coherence: rough surfaces lean on diffuse/rough reflection, smooth surfaces on specular.
		float specularWeight = saturate(specularProbability * smoothness);
		float3 diffuseSurfaceColour = 1.0f.xxx;

		if (diffuseWeight <= 0.0f && specularWeight <= 0.0f)
		{
			ssr.diff = float4(0, 0, 0, 0);
			ssr.diffHitInfo = float4(0, 0, 0, 0);
			ssr.spec = float4(0, 0, 0, 0);
			ssr.specHitInfo = float4(0, 0, 0, 0);
			return ssr;
		}

		uint instanceID = (uint)pixelDiffuse.w;
		uint2 numPixels = uint2(g_screenWidth, g_screenHeight);
		uint2 pixelCoord = screenPos * numPixels;
		uint pixelIndex = pixelCoord.y * numPixels.x + pixelCoord.x;
		float3 eyeVector = normalize(pixelPosWS.xyz - g_eyePos.xyz);
		float2 noiseSamplePos = screenPos * 64;
		float3 noise = g_noiseTexture.Sample(g_pointSampler, noiseSamplePos).rgb;
		uint baseRngState = pixelIndex + 719393 + noise.r * 3654 + noise.g * 1232 + 1540 * noise.b;
		float depth = pixelNormal.w;

		float3 finalDiffuseColour = 0.0f;
		float3 finalSpecularColour = 0.0f;
		float diffuseHitDistanceAccum = 0.0f;
		float specularHitDistanceAccum = 0.0f;
		float2 diffuseHitUvAccum = 0.0f;
		float2 specularHitUvAccum = 0.0f;
		float diffuseVirtualViewZAccum = 0.0f;
		float specularVirtualViewZAccum = 0.0f;
		float diffuseHitCount = 0.0f;
		float specularHitCount = 0.0f;
		float denoiseableDiffuseHitCount = 0.0f;
		float denoiseableSpecularHitCount = 0.0f;

		const uint DiffuseRays = 1;
		const uint SpecularRays = 1;//(roughness >= 0.35f) ? 2u : 1u;

		if (diffuseWeight > 0.0f)
		{
			uint diffuseRngState = baseRngState ^ 0x68bc21ebu;
			[loop]
			for (uint i = 0; i < DiffuseRays; ++i)
			{
				bool didReflect = false;
				float hitDistance = 0.0f;
				float2 hitUv = screenPos;
				float virtualViewZ = 0.0f;
				float4 reflectedColour = GetReflection(
					eyeVector,
					pixelPosWS.xyz,
					pixelNormal.xyz,
					pixelColour,
					depth,
					didReflect,
					hitDistance,
					hitUv,
					virtualViewZ,
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
					diffuseHitCount += 1.0f;

					if (reflectedColour.a > 0.0f)
					{
						diffuseHitDistanceAccum += hitDistance;
						diffuseHitUvAccum += hitUv;
						diffuseVirtualViewZAccum += virtualViewZ;
						denoiseableDiffuseHitCount += 1.0f;
					}
				}
			}
		}

		if (specularWeight > 0.02f)
		{
			uint specularRngState = baseRngState ^ 0x2c1b3c6du;
			[loop]
			for (uint i = 0; i < SpecularRays; ++i)
			{
				bool didReflect = false;
				float hitDistance = 0.0f;
				float2 hitUv = screenPos;
				float virtualViewZ = 0.0f;
				float4 reflectedColour = GetReflection(
					eyeVector,
					pixelPosWS.xyz,
					pixelNormal.xyz,
					pixelColour,
					depth,
					didReflect,
					hitDistance,
					hitUv,
					virtualViewZ,
					screenPos,
					noise,
					specularRngState,
					smoothness,
					1.0f,
					instanceID
				);

				if (didReflect)
				{
					finalSpecularColour += reflectedColour.rgb;
					specularHitCount += 1.0f;

					if (reflectedColour.a > 0.0f)
					{
						specularHitDistanceAccum += hitDistance;
						specularHitUvAccum += hitUv;
						specularVirtualViewZAccum += virtualViewZ;
						denoiseableSpecularHitCount += 1.0f;
					}
				}
			}
		}

		float3 diffuseColour = diffuseHitCount > 0.0f ? finalDiffuseColour / diffuseHitCount : 0.0f;
		float3 specularColour = specularHitCount > 0.0f ? finalSpecularColour / specularHitCount : 0.0f;
		float averageDiffuseHitDistance = denoiseableDiffuseHitCount > 0.0f ? diffuseHitDistanceAccum / denoiseableDiffuseHitCount : 0.0f;
		float averageSpecularHitDistance = denoiseableSpecularHitCount > 0.0f ? specularHitDistanceAccum / denoiseableSpecularHitCount : 0.0f;
		float2 averageDiffuseHitUv = denoiseableDiffuseHitCount > 0.0f ? diffuseHitUvAccum / denoiseableDiffuseHitCount : screenPos;
		float2 averageSpecularHitUv = denoiseableSpecularHitCount > 0.0f ? specularHitUvAccum / denoiseableSpecularHitCount : screenPos;
		float averageDiffuseVirtualViewZ = denoiseableDiffuseHitCount > 0.0f ? diffuseVirtualViewZAccum / denoiseableDiffuseHitCount : 0.0f;
		float averageSpecularVirtualViewZ = denoiseableSpecularHitCount > 0.0f ? specularVirtualViewZAccum / denoiseableSpecularHitCount : 0.0f;

		ssr.diff = float4(diffuseColour * diffuseSurfaceColour * diffuseWeight, denoiseableDiffuseHitCount > 0.0f ? 1.0f : 0.0f);
		ssr.diffHitInfo = float4(averageDiffuseHitUv, averageDiffuseVirtualViewZ, averageDiffuseHitDistance);
		ssr.spec = float4(specularColour * specularWeight, denoiseableSpecularHitCount > 0.0f ? 1.0f : 0.0f);
		ssr.specHitInfo = float4(averageSpecularHitUv, averageSpecularVirtualViewZ, averageSpecularHitDistance);

		return ssr;
	}
}




