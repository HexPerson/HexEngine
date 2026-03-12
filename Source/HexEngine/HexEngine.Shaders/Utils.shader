"Global"
{
#ifndef UTILS_SHADER
#define UTILS_SHADER

#define FLT_MIN          1.175494351e-38F  

	float3 ApplyNormalMap(float3 worldNormal, float3 tangent, float3 binormal, Texture2D normalMap, SamplerState samp, float2 texcoord, bool flipY = false, float bumMapMultiplier = 1.0f)
	{
		// Sample the pixel in the bump map.
		float3 bumpMap = normalMap.Sample(samp, texcoord).xyz;

		// flip the Y channel
		if(flipY)
			bumpMap.y = 1.0f - bumpMap.y;

		// Expand the range of the normal value from (0, +1) to (-1, +1).
		bumpMap = (bumpMap * 2.0f) - 1.0f;

		bumpMap = bumpMap * float3(bumMapMultiplier, bumMapMultiplier, 1.0f);

		// Calculate the normal from the data in the bump map.
		//float3 bumpNormal = (bumpMap.x * (tangent)) + (bumpMap.y * (binormal)) + (bumpMap.z * worldNormal);
		float3 bumpNormal = (bumpMap.x * tangent) + (bumpMap.y * binormal) + (bumpMap.z * worldNormal);

		// bumpNormal = normal + bumpMap.x * tangent + bumpMap.y * binormal;

		// Normalize the resulting bump normal.
		worldNormal = (bumpNormal);

		return worldNormal;
	}

	float3 ScreenToWorldPosition(float depth, float maxDepth, float2 scr, float2 screenSize, matrix viewProjectionMatrixInverse)
	{
		if (depth == -1)
			depth = maxDepth;

		float4 end_point;

		end_point.x = ((2.0f * scr.x) / screenSize.x) - 1.0f;
		end_point.y = (((2.0f * scr.y) / screenSize.y) - 1.0f);// *-1.0f;
		end_point.z = (depth / maxDepth);
		end_point.w = 1.0f;

		end_point = mul(end_point, viewProjectionMatrixInverse);

		return end_point.xyz / end_point.w;
	}

	float3 ScreenToWorldPosition2(float depth, float maxDepth, float3 dirToPixelWS, float3 cameraPos)
	{
		if (depth == -1)
			depth = maxDepth;

		float3 end_point;

		end_point = cameraPos + dirToPixelWS * depth;

		return end_point;
	}

	// Calculates UV offset for parallax bump mapping
	inline float2 ParallaxOffset(float h, float height, float3 viewDir)
	{
		h = h * height - height / 2.0;
		float3 v = normalize(viewDir);
		v.z += 0.42;
		return h * (v.xy / v.z);
	}

	float2 CalcVelocity(float4 newPos, float4 oldPos, float2 viewSize)
	{
		oldPos.xyz /= oldPos.w;
		oldPos.xy = (oldPos.xy * 0.5) + 0.5;
		//oldPos.xy = (oldPos.xy + 1) / 2.0f;
		//oldPos.y = 1 - oldPos.y;

		newPos.xyz /= newPos.w;
		newPos.xy = (newPos.xy * 0.5) + 0.5;
		//newPos.xy = (newPos.xy + 1) / 2.0f;
		//newPos.y = 1 - newPos.y;

        float2 vel = (oldPos.xy - newPos.xy);


		return vel;
	}

#endif
}
