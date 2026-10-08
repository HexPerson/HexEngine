"ComputeShaderIncludes"
{
	Global
}
"ComputeShader"
{
	// Resamples a textured rect light's image into one slice of the shared
	// area-light texture array (ClusteredLighting, kAreaTextureSize square,
	// RGBA16F, linear). The array's mip chain is generated afterwards and the
	// lighting shaders pick a mip from the lobe/footprint size (PBRutils'
	// area-light section), so one fixed-size slice per image serves both the
	// mirror-sharp reflection and the blurred diffuse wash.

	Texture2D<float4>         g_areaBlitSource : register(t0);
	RWTexture2DArray<float4>  g_areaBlitDest   : register(u0);
	SamplerState              g_areaBlitSampler : register(s0);	// linear clamp

	cbuffer AreaLightBlitConstants : register(b6)
	{
		uint4  g_areaBlitParams;	// x slice, y dest size, z decode sRGB, w taps per axis
		float4 g_areaBlitLod;		// x source mip to read
	};

	float3 SrgbToLinear(float3 c)
	{
		return c <= 0.04045f ? c / 12.92f : pow((c + 0.055f) / 1.055f, 2.4f);
	}

	[numthreads(8, 8, 1)]
	void ShaderMain(uint3 tid : SV_DispatchThreadID)
	{
		const uint size = g_areaBlitParams.y;
		if (tid.x >= size || tid.y >= size)
			return;

		// Box-filter the destination texel's footprint with NxN bilinear taps:
		// big source images (often loaded without mips) would otherwise alias.
		const uint taps = max(g_areaBlitParams.w, 1u);
		const float texel = 1.0f / (float)size;
		float3 sum = 0.0f.xxx;
		[loop]
		for (uint y = 0; y < taps; ++y)
		{
			[loop]
			for (uint x = 0; x < taps; ++x)
			{
				const float2 uv = (float2(tid.xy) + (float2(x, y) + 0.5f) / (float)taps) * texel;
				float3 c = g_areaBlitSource.SampleLevel(g_areaBlitSampler, uv, g_areaBlitLod.x).rgb;
				if (g_areaBlitParams.z != 0u)
					c = SrgbToLinear(saturate(c));
				sum += c;
			}
		}

		g_areaBlitDest[uint3(tid.xy, g_areaBlitParams.x)] = float4(max(sum / (float)(taps * taps), 0.0f.xxx), 1.0f);
	}
}
