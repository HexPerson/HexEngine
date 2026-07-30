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
	// Half-res SSR -> full-res NRD bridge (r_ssrHalfRes).
	//
	// The march runs at half resolution (that is where the perf win lives:
	// 4x fewer rays), but NRD's temporal accumulation at half resolution
	// makes reflections swim under camera motion - the march-vs-denoiser
	// bisect (r_ssrDenoise 0 at half res: swim gone) pinned the artifact on
	// NRD, and its full-res configuration is known-good. So the four march
	// outputs get upsampled HERE, before the denoiser, and NRD runs at full
	// resolution against the full-res gbuffer guides exactly as it always
	// did.
	//
	// Depth-aware 4-tap: same weighting scheme as SSRResolve's fallback
	// upsample - bilinear x depth-similarity, each half-res texel's depth
	// taken from its representative full-res gbuffer texel (the top-left of
	// its 2x2, matching SSR.shader's UV snap). Weights are computed once and
	// shared by all four signals so radiance and hit distance stay
	// consistent per pixel.

	Texture2D g_srcDiffuse     : register(t0);
	Texture2D g_srcDiffuseHit  : register(t1);
	Texture2D g_srcSpecular    : register(t2);
	Texture2D g_srcSpecularHit : register(t3);
	Texture2D g_srcNormalDepth : register(t4); // full-res gbuffer normal, .w = view depth

	SamplerState g_pointSampler : register(s2);

	struct UpsampleOut
	{
		float4 diffuse     : SV_Target0;
		float4 diffuseHit  : SV_Target1;
		float4 specular    : SV_Target2;
		float4 specularHit : SV_Target3;
	};

	UpsampleOut ShaderMain(UIPixelInput input)
	{
		uint srcW, srcH;
		g_srcSpecular.GetDimensions(srcW, srcH);
		const float2 srcSize = float2((float)srcW, (float)srcH);
		const float2 fullSize = float2((float)g_screenWidth, (float)g_screenHeight);

		const float2 screenPos = input.texcoord;
		const float pixelDepth = g_srcNormalDepth.SampleLevel(g_pointSampler, screenPos, 0).w;

		const float2 pos = screenPos * srcSize - 0.5f;
		const float2 base = floor(pos);
		const float2 f = pos - base;

		float4 diffuse = 0.0f.xxxx;
		float4 diffuseHit = 0.0f.xxxx;
		float4 specular = 0.0f.xxxx;
		float4 specularHit = 0.0f.xxxx;
		float wSum = 0.0f;

		float4 fbDiffuse = 0.0f.xxxx;
		float4 fbDiffuseHit = 0.0f.xxxx;
		float4 fbSpecular = 0.0f.xxxx;
		float4 fbSpecularHit = 0.0f.xxxx;

		[unroll]
		for (int i = 0; i < 4; ++i)
		{
			const float2 off = float2((float)(i & 1), (float)(i >> 1));
			const float2 texel = clamp(base + off, 0.0f.xx, srcSize - 1.0f.xx);
			const float2 uv = (texel + 0.5f) / srcSize;

			const float2 repUv = (texel * 2.0f + 0.5f) / fullSize;
			const float texelDepth = g_srcNormalDepth.SampleLevel(g_pointSampler, repUv, 0).w;

			const float bilin =
				(off.x > 0.5f ? f.x : 1.0f - f.x) *
				(off.y > 0.5f ? f.y : 1.0f - f.y);
			const float depthW = exp(-abs(texelDepth - pixelDepth) / max(pixelDepth * 0.1f, 0.05f));
			const float w = bilin * depthW;

			const float4 sd = g_srcDiffuse.SampleLevel(g_pointSampler, uv, 0);
			const float4 sdh = g_srcDiffuseHit.SampleLevel(g_pointSampler, uv, 0);
			const float4 ss = g_srcSpecular.SampleLevel(g_pointSampler, uv, 0);
			const float4 ssh = g_srcSpecularHit.SampleLevel(g_pointSampler, uv, 0);

			diffuse     += sd * w;
			diffuseHit  += sdh * w;
			specular    += ss * w;
			specularHit += ssh * w;
			wSum += w;

			fbDiffuse     += sd * bilin;
			fbDiffuseHit  += sdh * bilin;
			fbSpecular    += ss * bilin;
			fbSpecularHit += ssh * bilin;
		}

		UpsampleOut o;
		if (wSum > 1e-4f)
		{
			const float inv = 1.0f / wSum;
			o.diffuse = diffuse * inv;
			o.diffuseHit = diffuseHit * inv;
			o.specular = specular * inv;
			o.specularHit = specularHit * inv;
		}
		else
		{
			// Disoccluded sliver thinner than a half-res texel: plain bilinear
			// beats black.
			o.diffuse = fbDiffuse;
			o.diffuseHit = fbDiffuseHit;
			o.specular = fbSpecular;
			o.specularHit = fbSpecularHit;
		}
		return o;
	}
}
