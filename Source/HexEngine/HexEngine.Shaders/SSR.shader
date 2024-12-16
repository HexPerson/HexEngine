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
		float2 screenPosUV,
		float3 noise,
		inout uint rngState,
		float smoothness,
		float specularProbability,
		uint instanceID)
	{
		didReflect = false;

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


		const int stepCount = 16;
		const float stepLen = 16.0f; // 32x32 is good

		//const int smallStepCount = stepCount * 3.0f;
		//const float smallStepLen = stepLen / 3.0f;

		const float surfaceSmoothness = 2.0f;

		float3 fragPos = rayStart;

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

		const int MaxBounces = 3;
		int NumBounces = 0;

		float totalDistanceTravelled = 0.0f;

		[loop]
		for (int i = 0; i < stepCount; ++i)
		{			
			fragPos += rayDir * stepLen;

			totalDistanceTravelled += stepLen;

			// convert this position to a world space
			float4 fragScr = float4(fragPos.xyz, 1.0f);

			float4 fragView = mul(fragScr, g_viewMatrix);
			float4 fragClip = mul(fragView, g_projectionMatrix);

			fragClip.xyz /= fragClip.w;

			float fragDepth = -fragView.z;// / fragView.w;

			fragClip.xy = fragClip.xy * 0.5 + 0.5; // is this needed?

			float2 fragTex = float2(fragClip.x, 1.0f - fragClip.y);

			if (fragTex.x < 0.0f || fragTex.x > 1.0f || fragTex.y < 0.0f || fragTex.y > 1.0f)
			{
				if(NumBounces == 0)
					return float4(originalColour.rgb, 1.0f);

				return float4(rayColour, 1.0f);
			}

			// sample the depth of the world
			float4 actualDepthAndNormal = GBUFFER_NORMAL.Sample(
				g_pointSampler,
				fragTex);

			float actualDepth = actualDepthAndNormal.w;
			float3 actualNormal = actualDepthAndNormal.xyz;			

			bool didHitSky = actualDepth == g_frustumDepths[3];

			if(fragDepth > actualDepth || didHitSky)
			{
				// back track to the last interval
				fragPos -= rayDir * stepLen;
				totalDistanceTravelled -= stepLen;

				[loop]
				for(int j = 0; j < 16; ++j)
				{
					fragPos += rayDir * 1.0f;
					totalDistanceTravelled += 1.0f;

					// convert this position to a world space
					fragScr = float4(fragPos.xyz, 1.0f);

					fragView = mul(fragScr, g_viewMatrix);
					fragClip = mul(fragView, g_projectionMatrix);

					fragClip.xyz /= fragClip.w;

					fragDepth = -fragView.z;// / fragView.w;

					fragClip.xy = fragClip.xy * 0.5 + 0.5; // is this needed?

					fragTex = float2(fragClip.x, 1.0f - fragClip.y);

					actualDepthAndNormal = GBUFFER_NORMAL.Sample(g_pointSampler, fragTex);

					didHitSky = actualDepthAndNormal.w == g_frustumDepths[3];

					uint newInstanceId = (uint)GBUFFER_DIFFUSE.Sample(g_pointSampler, fragTex).w;

					if(newInstanceId == instanceID && !didHitSky)
					{
						//i--;
						continue;
					}

					if(fragDepth >= actualDepthAndNormal.w || (didHitSky && totalDistanceTravelled >= initialWorldPosFromEye))
					{
						break;
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
			return float4(originalColour.rgb, 1.0f);// originalColour;

		return float4(rayColour, 1.0f);// originalColour;
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

		

		
		float smoothness = pixelSpecular.b;
		

		uint instanceID = (uint)GBUFFER_DIFFUSE.Sample(g_pointSampler, screenPos).w;//pixelColour.w;

		float3 finalColour = pixelColour.rgb;

		bool hadAnyReflection = false;

		// Calculate reflection, if there is any
		if (smoothness > 0.0f)
		{
			//smoothness = smoothness * pixelSpecular.r;

			//float2 velocity = g_velocityTexture.Sample(g_pointSampler, screenPos).xy;

			uint2 numPixels = uint2(g_screenWidth, g_screenHeight);
			uint2 pixelCoord = screenPos * numPixels;
			uint pixelIndex = pixelCoord.y * numPixels.x + pixelCoord.x;

			const int pixelsToReject = 2;

			if(pixelsToReject > 0)
			{
				uint flipFlop = 1 - (g_frame % pixelsToReject);

				uint pixelIndex2 = pixelIndex + pixelCoord.y % pixelsToReject;

				if((pixelIndex2 % pixelsToReject) != flipFlop)
				{
					ssr.hitinfo = float4(0, 0, 0, 0);
					//ssr.diff = float4(g_historyTexture.Sample(g_pointSampler, screenPos - velocity).rgb, 1.0f);		
					ssr.diff = float4(pixelColour.rgb, 1.0f);		

					return ssr;
				}
			}

			float4 pixelNormal = GBUFFER_NORMAL.Sample(g_pointSampler, screenPos);
			float4 pixelPosWS = GBUFFER_POSITION.Sample(g_pointSampler, screenPos);

			float3 eyeVector = normalize(pixelPosWS.xyz - g_eyePos.xyz);
			float specularProbability = pixelSpecular.a;
			float emission = pixelPosWS.w;

			//float numPixelsTotal = g_screenWidth * g_screenHeight;
			//numPixelsTotal /= 129600;

			float3 finalReflectedColour = 0;

			float2 noiseSamplePos = screenPos * 32;

			//noiseSamplePos += frac(g_time) * 20.0f;

			float3 noise = g_noiseTexture.Sample(g_pointSampler, noiseSamplePos).rgb;

			
			uint rngState = pixelIndex + 719393 + noise.r * 3654 + noise.g * 1232 + 1540 * noise.b;// pixelIndex + (noise.r * 3 + noise.g  /*fmod(g_time, 1337.0f)*/ * 14540 * noise.b) + 719393  /*+(g_frame % 64) * 150*/;

			//rngState += g_frame * 40;
			//rngState += g_jitterOffsets;

			const float NumRays = 2;

			float depth = pixelNormal.w;

			float numHits = 0;

			/*if (depth <= g_frustumDepths[0])
				NumRays = 5;
			else if (depth <= g_frustumDepths[1])
				NumRays = 3;
			else if (depth <= g_frustumDepths[2])
				NumRays = 2;
			else if (depth <= g_frustumDepths[3])
				NumRays = 1;*/

			

			[loop]
			for (uint i = 0; i < NumRays; ++i)
			{
				bool didReflect = false;
				float4 reflectedColour = GetReflection(
					eyeVector,
					pixelPosWS.xyz,
					pixelNormal.xyz,
					pixelColour,
					depth,
					didReflect,
					screenPos,
					noise,
					rngState,
					smoothness,
					specularProbability,
					instanceID
				);

				if (didReflect)
				{
					finalReflectedColour += reflectedColour.rgb;
					numHits += 1.0f;

					hadAnyReflection = true;
				}
			}

			if(numHits > 0.0f)
				finalColour.rgb = finalReflectedColour.rgb / (float)numHits;// NumRays;

			ssr.hitinfo = float4(lerp(pixelColour.rgb, finalColour.rgb, smoothness), hadAnyReflection ? 1.0f : 0.0f);
		}	
		else
			ssr.hitinfo = float4(0, 0, 0, -1);

		ssr.diff = float4(lerp(pixelColour.rgb, finalColour.rgb, smoothness), 1.0f);		

		return ssr;//float4(lerp(pixelColour.rgb, finalColour.rgb, smoothness), 1.0f);
	}
}