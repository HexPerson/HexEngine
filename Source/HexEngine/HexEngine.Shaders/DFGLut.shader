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
	// Split-sum DFG LUT (P1-B). Karis 2013, "Real Shading in Unreal Engine 4".
	//
	// Precomputes the BRDF half of the split-sum approximation over
	// (NdotV, perceptualRoughness) into a 2D table:
	//
	//   .r = scale applied to F0
	//   .g = bias  added to F0
	//   .b = single-scatter directional albedo Ess, used for multi-scatter
	//        energy compensation (see below)
	//
	// This replaces EnvBRDFApprox, an analytic curve fit to this exact table.
	// The fit is good but not exact - it drifts most at grazing angles and high
	// roughness, precisely where energy loss is already worst.
	//
	// View-independent by construction: the table depends only on NdotV and
	// roughness, so it's generated once at startup and never regenerated.
	//
	// The .b channel is the total single-scatter energy Ess = integral of the
	// GGX BRDF over the hemisphere. Single-scatter GGX loses energy as roughness
	// rises because it models only ONE bounce off the microfacets - light that
	// would have bounced again is simply dropped, so rough metals render darker
	// than they should. Ess lets the shader add that energy back.

	static const uint kSampleCount = 1024u;

	float2 Hammersley(uint i, uint count)
	{
		uint bits = i;
		bits = (bits << 16u) | (bits >> 16u);
		bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
		bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
		bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
		bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
		return float2((float)i / (float)count, (float)bits * 2.3283064365386963e-10f);
	}

	// GGX importance sample around +Z (the LUT works in a canonical tangent frame,
	// so N = (0,0,1) and no basis construction is needed).
	float3 ImportanceSampleGGX(float2 xi, float roughness)
	{
		const float a = roughness * roughness;

		const float phi = 2.0f * 3.14159265f * xi.x;
		const float cosTheta = sqrt((1.0f - xi.y) / (1.0f + (a * a - 1.0f) * xi.y));
		const float sinTheta = sqrt(1.0f - cosTheta * cosTheta);

		return float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
	}

	// Smith height-correlated visibility, Schlick-GGX with the IBL k.
	float GeometrySmithIBL(float NdotV, float NdotL, float roughness)
	{
		const float a = roughness * roughness;
		const float k = (a * a) * 0.5f; // IBL variant, not the direct-lighting k
		const float gv = NdotV / (NdotV * (1.0f - k) + k);
		const float gl = NdotL / (NdotL * (1.0f - k) + k);
		return gv * gl;
	}

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		// u = NdotV, v = perceptual roughness. Both clamped off zero: NdotV = 0 is
		// a degenerate grazing case and roughness 0 makes the GGX lobe a delta
		// function that importance sampling can't resolve.
		const float NdotV = max(input.texcoord.x, 1e-3f);
		const float perceptualRoughness = max(input.texcoord.y, 1e-3f);

		// Canonical frame: N = +Z, V in the XZ plane at the requested NdotV.
		const float3 V = float3(sqrt(1.0f - NdotV * NdotV), 0.0f, NdotV);

		float scale = 0.0f;
		float bias = 0.0f;
		float energy = 0.0f;

		[loop]
		for (uint i = 0u; i < kSampleCount; ++i)
		{
			const float2 xi = Hammersley(i, kSampleCount);
			const float3 H = ImportanceSampleGGX(xi, perceptualRoughness);
			const float3 L = normalize(2.0f * dot(V, H) * H - V);

			const float NdotL = saturate(L.z);
			if (NdotL <= 0.0f)
				continue;

			const float NdotH = saturate(H.z);
			const float VdotH = saturate(dot(V, H));

			const float G = GeometrySmithIBL(NdotV, NdotL, perceptualRoughness);
			// The pdf and BRDF denominators cancel down to this weight for a
			// GGX-importance-sampled estimator.
			const float GVis = (G * VdotH) / max(NdotH * NdotV, 1e-6f);
			const float Fc = pow(1.0f - VdotH, 5.0f);

			scale += (1.0f - Fc) * GVis;
			bias += Fc * GVis;
			// Ess: the same integral without the Fresnel split, i.e. how much
			// energy a white-Fresnel surface returns.
			energy += GVis;
		}

		const float inv = 1.0f / (float)kSampleCount;
		return float4(scale * inv, bias * inv, energy * inv, 1.0f);
	}
}
