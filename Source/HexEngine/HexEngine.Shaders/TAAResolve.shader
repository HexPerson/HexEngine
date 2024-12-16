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
	Texture2D historyTexture : register(t0);
	Texture2D velocityTexture : register(t1);
	Texture2D depthTexture : register(t2);
	Texture2D shaderTexture : register(t3);

	SamplerState PointSampler : register(s3);
	SamplerState LinearSampler : register(s4);

#define FRAME_VELOCITY_IN_TEXELS_DIFF 0.01f
#define FRAME_DEPTH_MAX_DIFF 4.0f

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		float3 colour = shaderTexture.Sample(PointSampler, input.texcoord).rgb;		
		float2 velocity = velocityTexture.Sample(PointSampler, input.texcoord).xy;

		//velocity /= float2(g_screenWidth, g_screenHeight);
		//velocity.xy = (velocity.xy + 1) / 2.0f;
		//velocity.y = 1 - velocity.y;

		//return float4(velocity.x, velocity.y, 0.0f, 1.0f);

		float depth = depthTexture.Sample(PointSampler, input.texcoord).w;

		float velocityConfidence = saturate(1.f - length(velocity.xy) / FRAME_VELOCITY_IN_TEXELS_DIFF);

		//velocity *= velocityConfidence;

		/*if (velocity.x == 999 && velocity.y == 999)
		{h
			return float4(colour.rgb, 1.0f);
		}*/

		float2 prevousPixelPos = input.texcoord - velocity;

		float3 history = historyTexture.Sample(LinearSampler, prevousPixelPos);
		float oldDepth = depthTexture.Sample(LinearSampler, prevousPixelPos).w;

		if (oldDepth - depth > FRAME_DEPTH_MAX_DIFF || (depth == g_frustumDepths[3] && oldDepth == g_frustumDepths[3]))
			return float4(colour, 1.0f);

		// Box filter
		// Apply clamping on the history color.
		float3 NearColor0 = shaderTexture.Sample(PointSampler, input.texcoord, int2(1, 0)).xyz;
		float3 NearColor1 = shaderTexture.Sample(PointSampler, input.texcoord, int2(0, 1)).xyz;
		float3 NearColor2 = shaderTexture.Sample(PointSampler, input.texcoord, int2(-1, 0)).xyz;
		float3 NearColor3 = shaderTexture.Sample(PointSampler, input.texcoord, int2(0, -1)).xyz;

		float3 BoxMin = min(colour, min(NearColor0, min(NearColor1, min(NearColor2, NearColor3))));
		float3 BoxMax = max(colour, max(NearColor0, max(NearColor1, max(NearColor2, NearColor3))));

		history = clamp(history, BoxMin, BoxMax);

		float modulationFactor = 0.9f;// *velocityConfidence;

		float3 resolvedColour = lerp(colour, history, modulationFactor);

		return float4(resolvedColour, 1.0f);
	}
}
