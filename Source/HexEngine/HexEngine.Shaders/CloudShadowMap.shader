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
	CloudCommon
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
	// Cloud shadow map: a top-down transmittance image over the cloud BASE
	// plane, rendered once per frame. Each texel is a point on that plane;
	// we march from it toward the sun through the slab (same density
	// function the clouds are drawn with) and store the transmittance.
	// Consumers project a world position along the sun ray onto the plane
	// and take one tap (CloudCommon::SampleCloudShadowMap) - exact for
	// parallel light, and ~1000x cheaper than the per-pixel re-march it
	// replaces.

	Texture3D    g_shapeNoise    : register(t0);
	Texture3D    g_detailNoise   : register(t1);
	SamplerState g_mirrorSampler : register(s3);

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		const float halfExtent = g_cloudShadowMapOrigin.w;
		const float3 sunDir = g_cloudShadowMapSun.xyz;
		if (halfExtent <= 0.0f || sunDir.y <= 0.02f)
			return float4(1.0f, 0.0f, 0.0f, 1.0f);

		const float2 uv = input.texcoord;
		const float2 local = (uv * 2.0f - 1.0f) * halfExtent;
		const float3 boundsMin = g_cloudBoundsMin.xyz;
		const float3 boundsMax = g_cloudBoundsMax.xyz;
		const float3 origin = g_cloudShadowMapOrigin.xyz
			+ g_cloudShadowMapAxisX.xyz * local.x
			+ g_cloudShadowMapAxisZ.xyz * local.y;

		const float2 hit = CloudRayBoxDist(boundsMin, boundsMax, origin, sunDir);
		if (hit.y <= 0.0f)
			return float4(1.0f, 0.0f, 0.0f, 1.0f);

		const float3 windOffset = g_cloudWindOffset.xyz;
		const int steps = max(4, (int)g_cloudMarch.z);
		// Physical per-metre extinction + span clamp, matching the cloud
		// render marches: light beyond ~900 m of cloud is fully extinct.
		const float invCloudHeight = 0.012f;
		const float span = min(hit.y, 900.0f);
		const float stepLen = max(1.0f, span / (float)steps);

		float opticalDepth = 0.0f;
		float travelled = 0.0f;
		[loop]
		for (int i = 0; i < steps; ++i)
		{
			if (travelled >= span)
				break;
			const float3 p = origin + sunDir * (hit.x + travelled + stepLen * 0.5f);
			opticalDepth += SampleCloudDensityTexCoarse(g_shapeNoise, g_detailNoise, g_mirrorSampler, p, boundsMin, boundsMax, windOffset) * stepLen * invCloudHeight;
			travelled += stepLen;
		}

		const float transmittance = max(g_cloudParams3.z, exp(-opticalDepth * g_cloudParams1.x));
		return float4(transmittance, 0.0f, 0.0f, 1.0f);
	}
}
