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
}
"VertexShader"
{
	UIPixelInput ShaderMain(UIVertexInput input)
	{
		UIPixelInput output;

		output.position = input.position;
		output.texcoord = input.texcoord;
		output.colour = input.colour;

		return output;
	}
}
"PixelShader"
{
	//Texture2D historyTexture : register(t0);
	//Texture2D velocityTexture : register(t1);
	//Texture2D hitInfo : register(t2);
	Texture2D shaderTexture : register(t0);
	

	SamplerState PointSampler : register(s3);
	SamplerState LinearSampler : register(s4);

#define val0 (1.0)
#define val1 (0.125)
#define effect_width (0.15)

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		float2 screenPos = float2(input.position.x / (float)g_screenWidth , input.position.y / (float)g_screenHeight ); //input.texcoord

		float4 pixels = shaderTexture.Sample(PointSampler, input.texcoord);
		return float4(pixels.rgb, 1.0f);

#if 0
		float3 colour = pixels.rgb;
		float2 velocity = velocityTexture.Sample(PointSampler, input.texcoord).xy;

		//velocity /= float2(g_screenWidth, g_screenHeight);
		//velocity.xy = (velocity.xy + 1) / 2.0f;

		//if(pixels.a == 0.0f)
		//	return float4(pixels.rgb, 1.0f);
		
		float4 hits = hitInfo.Sample(PointSampler, input.texcoord);

		if(hits.w == -1.0f)
			return float4(colour, 1);

		// we did not hit :(
		//if(hits.w == 0.0f)
		{
			float3 adjacentAccumulated = 0;
			float numHits = 0;

			if(hits.w > 0)
			{
				adjacentAccumulated = pixels.rgb;
				numHits = numHits + 1;
			}

			float hits = 0;

			/*

			x 0 x
			0 x 0
			x 0 x
			*/

			const int2 offsets[] = {
				//{-1, -1},
				//{ 1, -1},
				//{ 1,  1},
				//{-1,  1},

				{-1,  0},
				{ 0, -1},
				{ 1,  0},
				{ 0,  1},
			};

			int2 screenCords = int2(screenPos.x * g_screenWidth, screenPos.y * g_screenHeight);

			

			const float searchSize = 1.0f;

			[loop]
			for (int i = 0; i < 4; ++i)
			{
				float2 tsOffset = float2((float)offsets[i][0] / g_screenWidth, (float)offsets[i][1] / g_screenHeight);

				tsOffset *= searchSize;

				//tsOffset -= velocity;


				float4 adjacentHit = hitInfo.Sample(PointSampler, input.texcoord + tsOffset);//hitInfo.Load(int3(screenCords + offsets[i], 0));
				
				// did we find an adjacent hit?
				if(adjacentHit.w == 1.0f)
				{
					adjacentAccumulated += adjacentHit.rgb;//shaderTexture.Load(int3(screenCords + offsets[i], 0)).rgb;
					//adjacentAccumulated += shaderTexture.Sample(LinearSampler, screenPos + tsOffset).rgb;
					numHits = numHits + 1;
					//break;
				}
				else
				{
					//adjacentAccumulated += shaderTexture.Sample(LinearSampler, input.texcoord + tsOffset).rgb;//colour.rgb;//shaderTexture.Load(int3(screenCords + offsets[i], 0)).rgb;
					//numHits = numHits + 1;
				}
			}

			if(numHits > 0)
			{
				adjacentAccumulated = adjacentAccumulated / numHits;
				colour = adjacentAccumulated;
			}
		}

		return float4(colour, 1);
		float2 prevousPixelPos = input.texcoord - velocity;

		float3 history = historyTexture.Sample(LinearSampler, prevousPixelPos).rgb;

		float3 NearColor0 = shaderTexture.Sample(PointSampler, input.texcoord, int2(1, 0)).xyz;
		float3 NearColor1 = shaderTexture.Sample(PointSampler, input.texcoord, int2(0, 1)).xyz;
		float3 NearColor2 = shaderTexture.Sample(PointSampler, input.texcoord, int2(-1, 0)).xyz;
		float3 NearColor3 = shaderTexture.Sample(PointSampler, input.texcoord, int2(0, -1)).xyz;

		float3 BoxMin = min(colour, min(NearColor0, min(NearColor1, min(NearColor2, NearColor3))));
		float3 BoxMax = max(colour, max(NearColor0, max(NearColor1, max(NearColor2, NearColor3))));

		//history = clamp(history, BoxMin, BoxMax);

		float modulationFactor = 0.8f;//0.15f;

		float3 resolvedColour = lerp(colour, history, modulationFactor);

		return float4(resolvedColour, 1.0f);
#endif
	}
}
