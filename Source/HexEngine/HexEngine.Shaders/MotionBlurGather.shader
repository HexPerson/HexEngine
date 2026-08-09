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
		output.positionSS = output.position;
		return output;
	}
}
"PixelShader"
{
	// P4.5 motion blur, pass 3 of 3: the McGuire 2012 scatter-as-gather
	// reconstruction. Samples along the tile neighbourhood's dominant
	// velocity, weighting each tap by foreground/background classification:
	//   - a FOREGROUND sample (nearer than us) blurs by ITS OWN velocity
	//     (its streak smears over us),
	//   - a BACKGROUND sample (farther) contributes through OUR blur cone
	//     (we smear, revealing it),
	//   - the cylinder term handles the both-moving-together case.
	// Depth for classification comes from the gbuffer normal RT's .w channel
	// (view-space metres - the same source every other pass uses, since the
	// depth buffer itself stays bound as a DSV).
	//
	// g_mbParams:  x = velocity scale, y = max blur px, z = sample count,
	//              w = tile size
	// g_mbParams2: xy = full-res dimensions, zw = tile-grid dimensions

	Texture2D g_scene : register(t0);
	Texture2D g_velocity : register(t1);
	Texture2D g_neighborMax : register(t2);
	Texture2D g_normalDepth : register(t3);
	SamplerState g_pointSampler : register(s2);

	cbuffer MotionBlurConstants : register(b6)
	{
		float4 g_mbParams;
		float4 g_mbParams2;
	};

	// Pixel's own velocity, converted to the shared working space (scaled,
	// clamped texture-UV offset) - must match MotionBlurTileMax exactly.
	float2 PixelVelocity(int2 p)
	{
		float2 v = g_velocity.Load(int3(p, 0)).xy;
		v = float2(v.x, -v.y) * g_mbParams.x;
		const float2 vPx = v * g_mbParams2.xy;
		const float lenPx = length(vPx);
		if (lenPx > g_mbParams.y)
			v *= g_mbParams.y / lenPx;
		return v;
	}

	// Soft depth comparison (view metres): 1 when a is in front of b by the
	// full softness band, 0 when behind.
	float SoftDepthCompare(float a, float b)
	{
		return saturate(1.0f - (a - b) / 1.0f);
	}

	float Cone(float distPx, float radiusPx)
	{
		return saturate(1.0f - abs(distPx) / max(radiusPx, 0.25f));
	}

	float Cylinder(float distPx, float radiusPx)
	{
		return 1.0f - smoothstep(0.95f * radiusPx, 1.05f * radiusPx, abs(distPx));
	}

	float Hash(float2 p)
	{
		return frac(sin(dot(p, float2(12.9898f, 78.233f))) * 43758.5453f);
	}

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		const float2 uv = input.texcoord;
		const int2 pix = (int2)(uv * g_mbParams2.xy);

		const float4 centreColour = g_scene.SampleLevel(g_pointSampler, uv, 0);

		// Dominant velocity of the 3x3 tile neighbourhood.
		const float2 vMax = g_neighborMax.SampleLevel(g_pointSampler, uv, 0).xy;
		const float lenMaxPx = length(vMax * g_mbParams2.xy);
		if (lenMaxPx < 0.75f)
			return centreColour;

		const float2 vC = PixelVelocity(pix);
		const float lenCPx = max(length(vC * g_mbParams2.xy), 0.5f);
		const float zC = g_normalDepth.SampleLevel(g_pointSampler, uv, 0).w;

		const int sampleCount = max((int)g_mbParams.z, 5);

		// Per-pixel jitter on the tap positions hides the sample banding
		// (frame-stable: the blur itself must not shimmer under TAA).
		const float jitter = Hash((float2)pix) - 0.5f;

		// Self weight: the centre sample stands in for the part of the
		// integration the taps don't cover around t=0.
		float totalWeight = (float)sampleCount / (20.0f * lenCPx);
		float3 sum = centreColour.rgb * totalWeight;

		[loop]
		for (int i = 0; i < sampleCount; ++i)
		{
			const float t = lerp(-1.0f, 1.0f, ((float)i + jitter + 1.0f) / ((float)sampleCount + 1.0f));
			const float2 sampleUv = clamp(uv + vMax * t, 0.5f / g_mbParams2.xy, 1.0f - 0.5f / g_mbParams2.xy);
			const int2 samplePix = (int2)(sampleUv * g_mbParams2.xy);

			const float distPx = t * lenMaxPx;
			const float2 vS = PixelVelocity(samplePix);
			const float lenSPx = max(length(vS * g_mbParams2.xy), 0.5f);
			const float zS = g_normalDepth.SampleLevel(g_pointSampler, sampleUv, 0).w;

			const float front = SoftDepthCompare(zC, zS); // sample in front of us
			const float back  = SoftDepthCompare(zS, zC); // sample behind us

			const float weight =
				front * Cone(distPx, lenSPx) +
				back  * Cone(distPx, lenCPx) +
				Cylinder(distPx, lenSPx) * Cylinder(distPx, lenCPx) * 2.0f;

			sum += g_scene.SampleLevel(g_pointSampler, sampleUv, 0).rgb * weight;
			totalWeight += weight;
		}

		return float4(sum / max(totalWeight, 1e-4f), centreColour.a);
	}
}
