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
	EnvMapCommon
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
	// Reflection-probe prefilter (IBL step 2).
	//
	// Converts a probe's six captured faces into the same prefiltered
	// octahedral roughness atlas the sky uses (see EnvMapCommon / SkyEnvMap).
	// One fullscreen draw per probe, run once per capture - not per frame.
	//
	// The face basis below mirrors ReflectionProbeComponent::kFaceDirs/kFaceUps
	// exactly (forward = dir, right = normalize(cross(up, forward)),
	// up = cross(forward, right)). Change one, change both.

	Texture2D g_facePX : register(t0);
	Texture2D g_faceNX : register(t1);
	Texture2D g_facePY : register(t2);
	Texture2D g_faceNY : register(t3);
	Texture2D g_facePZ : register(t4);
	Texture2D g_faceNZ : register(t5);
	SamplerState g_linearSampler : register(s4);

	static const uint kPrefilterSampleCount = 128u;

	static const float3 kFaceDirs[6] =
	{
		float3( 1.0f, 0.0f, 0.0f),
		float3(-1.0f, 0.0f, 0.0f),
		float3( 0.0f, 1.0f, 0.0f),
		float3( 0.0f,-1.0f, 0.0f),
		float3( 0.0f, 0.0f, 1.0f),
		float3( 0.0f, 0.0f,-1.0f),
	};
	static const float3 kFaceUps[6] =
	{
		float3(0.0f, 1.0f, 0.0f),
		float3(0.0f, 1.0f, 0.0f),
		float3(0.0f, 0.0f,-1.0f),
		float3(0.0f, 0.0f, 1.0f),
		float3(0.0f, 1.0f, 0.0f),
		float3(0.0f, 1.0f, 0.0f),
	};

	float3 SampleFace(uint face, float2 uv)
	{
		// Texture objects can't be dynamically indexed; a branch chain is fine
		// at this pass's size (one 128x640 draw per capture).
		[branch] if (face == 0u) return g_facePX.SampleLevel(g_linearSampler, uv, 0).rgb;
		[branch] if (face == 1u) return g_faceNX.SampleLevel(g_linearSampler, uv, 0).rgb;
		[branch] if (face == 2u) return g_facePY.SampleLevel(g_linearSampler, uv, 0).rgb;
		[branch] if (face == 3u) return g_faceNY.SampleLevel(g_linearSampler, uv, 0).rgb;
		[branch] if (face == 4u) return g_facePZ.SampleLevel(g_linearSampler, uv, 0).rgb;
		return g_faceNZ.SampleLevel(g_linearSampler, uv, 0).rgb;
	}

	float3 SampleCapturedCube(float3 d)
	{
		// Pick the face whose axis dominates the direction.
		const float3 ad = abs(d);
		uint face;
		if (ad.x >= ad.y && ad.x >= ad.z)      face = (d.x >= 0.0f) ? 0u : 1u;
		else if (ad.y >= ad.z)                 face = (d.y >= 0.0f) ? 2u : 3u;
		else                                   face = (d.z >= 0.0f) ? 4u : 5u;

		// Project onto the capture camera's image plane using the same basis
		// the C++ rig builds for its look-at.
		const float3 fwd = kFaceDirs[face];
		const float3 upRef = kFaceUps[face];
		const float3 right = normalize(cross(upRef, fwd));
		const float3 up = cross(fwd, right);

		const float z = dot(d, fwd);
		const float u = dot(d, right) / z;
		const float v = dot(d, up) / z;

		// 90-degree fov, aspect 1: image plane spans [-1,1]. Y flips into
		// texture space.
		const float2 uv = float2(u * 0.5f + 0.5f, 1.0f - (v * 0.5f + 0.5f));
		return SampleFace(face, saturate(uv));
	}

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

	float3 ImportanceSampleGGX(float2 xi, float roughness, float3 N)
	{
		const float a = roughness * roughness;

		const float phi = 2.0f * 3.14159265f * xi.x;
		const float cosTheta = sqrt((1.0f - xi.y) / (1.0f + (a * a - 1.0f) * xi.y));
		const float sinTheta = sqrt(1.0f - cosTheta * cosTheta);

		const float3 h = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);

		const float3 up = abs(N.y) < 0.999f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
		const float3 tangentX = normalize(cross(up, N));
		const float3 tangentY = cross(N, tangentX);

		return tangentX * h.x + tangentY * h.y + N * h.z;
	}

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		const float rowsF = ENVMAP_ROUGHNESS_ROWS;
		const float rowIdx = floor(input.texcoord.y * rowsF);
		const float2 innerUv = float2(input.texcoord.x, frac(input.texcoord.y * rowsF));

		// Same inset as SkyEnvMap / SampleEnvAtlas: gutter texels fold to the
		// octahedrally-wrapped neighbour so the +-axis seams filter cleanly.
		const float3 N = OctDecodeDir(OctBlockToUnit(innerUv));
		const float roughness = rowIdx / (rowsF - 1.0f);

		if (rowIdx < 0.5f)
			return float4(SampleCapturedCube(N), 1.0f);

		// Same per-texel Cranley-Patterson rotation as the sky prefilter - a
		// shared fixed sample set aliases bright thin features (windows, lights)
		// into rings; per-texel rotation converts that to fine noise.
		const float2 pix = input.position.xy;
		const float azimuthRotation = frac(sin(dot(pix, float2(12.9898f, 78.233f))) * 43758.5453f);

		float3 accum = 0.0f.xxx;
		float weight = 0.0f;

		[loop]
		for (uint i = 0u; i < kPrefilterSampleCount; ++i)
		{
			float2 xi = Hammersley(i, kPrefilterSampleCount);
			xi.x = frac(xi.x + azimuthRotation);

			const float3 h = ImportanceSampleGGX(xi, roughness, N);
			const float3 l = normalize(2.0f * dot(N, h) * h - N);

			const float NoL = dot(N, l);
			if (NoL > 0.0f)
			{
				accum += SampleCapturedCube(l) * NoL;
				weight += NoL;
			}
		}

		return float4(accum / max(weight, 1e-4f), 1.0f);
	}
}
