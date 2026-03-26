"ComputeShaderIncludes"
{
	Global
}
"ComputeShader"
{
	Texture3D<float4> g_voxelRadianceSrc : register(t0);
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
	};

	[numthreads(8, 8, 8)]
	void ShaderMain(uint3 tid : SV_DispatchThreadID)
	{
		const uint clipIdx = min((uint)g_giParams0.w, 3u);
		const uint voxelRes = max(1u, (uint)g_clipVoxelInfo[clipIdx].z);
		if (any(tid >= voxelRes))
			return;

		const int3 p = int3(tid);
		const int3 maxP = int3((int)voxelRes - 1, (int)voxelRes - 1, (int)voxelRes - 1);
		const float4 center = g_voxelRadianceSrc[p];
		const float3 sunDirWs = normalize(g_giParams3.xyz + float3(1e-6f, 1e-6f, 1e-6f));
		const float dirStrength = saturate(g_giParams3.w);

		float3 accum = center.rgb;
		float accumW = 1.0f;
		float maxOcc = center.a;

		static const int3 kOffsets[6] =
		{
			int3(1, 0, 0), int3(-1, 0, 0),
			int3(0, 1, 0), int3(0, -1, 0),
			int3(0, 0, 1), int3(0, 0, -1)
		};

		[unroll]
		for (uint i = 0; i < 6; ++i)
		{
			const int3 np = clamp(p + kOffsets[i], int3(0, 0, 0), maxP);
			const float4 n = g_voxelRadianceSrc[np];
			const float3 sampleDir = normalize((float3)kOffsets[i]);
			const float directionalWeight = 1.0f + saturate(dot(sampleDir, -sunDirWs)) * (0.60f * dirStrength);
			const float w = 0.70f * directionalWeight;
			accum += n.rgb * w;
			accumW += w;
			maxOcc = max(maxOcc, n.a);
		}

		const float3 blurred = accum / max(accumW, 1e-4f);

		// Extra directional gather from the up-sun side to preserve sun-driven GI gradients.
		float3 directionalSample = 0.0f.xxx;
		if (dirStrength > 0.001f)
		{
			const int3 sunStep = int3(round(-sunDirWs));
			if (any(sunStep != int3(0, 0, 0)))
			{
				const int3 sp = clamp(p + sunStep * 2, int3(0, 0, 0), maxP);
				directionalSample = g_voxelRadianceSrc[sp].rgb;
			}
		}

		const float propagation = lerp(0.40f, 0.55f, dirStrength);
		float3 mixed = lerp(blurred, max(blurred, directionalSample), 0.35f * dirStrength);
		float3 outRgb = lerp(center.rgb, max(center.rgb, mixed), propagation);
		outRgb = min(outRgb, 32.0f.xxx);

		const float outOcc = saturate(max(center.a * 0.985f, maxOcc * 0.95f));
		g_voxelRadianceOut[p] = float4(outRgb, outOcc);
	}
}
