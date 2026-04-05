"ComputeShaderIncludes"
{
	Global
}
"ComputeShader"
{
	struct GpuGiEmitterPoint
	{
		float4 positionRadius;
		float4 radianceOpacity;
	};

	StructuredBuffer<GpuGiEmitterPoint> g_emitterPoints : register(t0);
	RWTexture3D<float4> g_voxelRadianceOut : register(u0);

	cbuffer GIConstants : register(b4)
	{
		float4 g_clipCenterExtent[4];
		float4 g_clipVoxelInfo[4];
		float4 g_giParams0;
		float4 g_giParams1;
		float4 g_giParams2;
		float4 g_giParams3;
		float4 g_giParams4;
		float4 g_giParams5;
		float4 g_giParams6;
		float4 g_giParams7;
		float4 g_giParams8;
		float4 g_giParams9;
	};

	[numthreads(64, 1, 1)]
	void ShaderMain(uint3 tid : SV_DispatchThreadID)
	{
		uint pointCount = 0u;
		uint pointStride = 0u;
		g_emitterPoints.GetDimensions(pointCount, pointStride);
		if (tid.x >= pointCount)
			return;

		const GpuGiEmitterPoint emitterPoint = g_emitterPoints[tid.x];
		const uint clipIdx = min((uint)g_giParams0.w, 3u);
		const float3 clipCenter = g_clipCenterExtent[clipIdx].xyz;
		const float clipExtent = max(1e-3f, g_clipCenterExtent[clipIdx].w);
		const uint voxelRes = max((uint)g_clipVoxelInfo[clipIdx].z, 1u);
		const float voxelSize = (clipExtent * 2.0f) / (float)voxelRes;
		const float clipAttenuation = rcp(1.0f + (float)clipIdx * 0.5f);
		const float emissiveInject = max(0.0f, g_giParams8.w);

		const float3 positionWs = emitterPoint.positionRadius.xyz;
		const float radiusWs = max(emitterPoint.positionRadius.w, voxelSize * 0.75f);
		const float3 radianceBase = max(emitterPoint.radianceOpacity.rgb, 0.0f.xxx) * emissiveInject * clipAttenuation;
		if (dot(radianceBase, radianceBase) <= 1e-8f)
			return;

		const float3 clipMin = clipCenter - clipExtent.xxx;
		const float3 clipMax = clipCenter + clipExtent.xxx;
		const float3 sphereMin = max(positionWs - radiusWs.xxx, clipMin);
		const float3 sphereMax = min(positionWs + radiusWs.xxx, clipMax);
		if (any(sphereMax < sphereMin))
			return;

		const float3 uvwMin = saturate(((sphereMin - clipCenter) / (clipExtent * 2.0f)) + 0.5f);
		const float3 uvwMax = saturate(((sphereMax - clipCenter) / (clipExtent * 2.0f)) + 0.5f);
		const uint3 voxelMin = min((uint3)(uvwMin * (float)(voxelRes - 1u)), voxelRes - 1u);
		const uint3 voxelMax = min((uint3)(uvwMax * (float)(voxelRes - 1u)), voxelRes - 1u);

		for (uint z = voxelMin.z; z <= voxelMax.z; ++z)
		{
			for (uint y = voxelMin.y; y <= voxelMax.y; ++y)
			{
				for (uint x = voxelMin.x; x <= voxelMax.x; ++x)
				{
					const uint3 coord = uint3(x, y, z);
					const float3 uvw = (float3(coord) + 0.5f.xxx) / (float)voxelRes;
					const float3 voxelCenterWs = (uvw - 0.5f.xxx) * (clipExtent * 2.0f) + clipCenter;
					const float dist = distance(voxelCenterWs, positionWs);
					if (dist > radiusWs)
						continue;

					float support = saturate(1.0f - dist / max(radiusWs, 1e-5f));
					support *= support;

					const float4 previous = g_voxelRadianceOut[coord];
					const float3 radiance = min(previous.rgb + radianceBase * support, 32.0f.xxx);
					const float opacity = max(previous.a, emitterPoint.radianceOpacity.a);
					g_voxelRadianceOut[coord] = float4(radiance, opacity);
				}
			}
		}
	}
}
