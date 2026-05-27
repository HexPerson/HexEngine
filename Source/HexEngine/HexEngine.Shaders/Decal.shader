"InputLayout"
{
	Pos
}
"VertexShaderIncludes"
{
	Global
}
"PixelShaderIncludes"
{
	Global
}
"VertexShader"
{
	// The decal renderer draws a unit cube ([-0.5, 0.5]^3) per decal, transformed
	// into world space by g_worldMatrix from the standard PerObjectBuffer. The
	// pixel shader reconstructs the underlying scene surface position by sampling
	// the GBuffer position copy (NOT the live position RT, which would deadlock
	// with the OM). Vertex attributes are POSITION only.
	struct DecalVSInput
	{
		float3 position : POSITION;
	};

	struct DecalPSInput
	{
		float4 position : SV_POSITION;
	};

	DecalPSInput ShaderMain(DecalVSInput input)
	{
		DecalPSInput output;

		float4 worldPos = mul(float4(input.position, 1.0f), g_worldMatrix);
		output.position = mul(worldPos, g_viewProjectionMatrix);

		return output;
	}
}
"PixelShader"
{
	// Re-declared from the VS section because the engine's shader build pipeline
	// compiles VS and PS as separate translation units (no shared HLSL between
	// the blocks unless lifted into a "Global" include - which would be overkill
	// for a single struct).
	struct DecalPSInput
	{
		float4 position : SV_POSITION;
	};

	// Decal pass binds:
	//   RTs:  SV_TARGET0 = GBuffer diffuse, SV_TARGET1 = GBuffer mat
	//         (we leave the normal / position / velocity / features RTs alone -
	//         specifically the normal RT has linear depth packed in .a from the
	//         opaque pass, which any modification would corrupt).
	//   SRVs: t0 = position GBuffer copy (live position RT is unbound so this
	//              snapshot is taken before the decal pass starts and rebound).
	//         t1 = decal albedo texture
	//         t2 = decal normal texture (unused in v1 - reserved for future
	//              normal-blend pass when per-RT blend states land)
	//         t3 = decal mat texture (.r = metallic, .g = roughness)
	//
	// Per-decal constants come via DecalConstants (b4). The decal's world matrix
	// is in PerObjectBuffer.g_worldMatrix as usual; the inverse is stashed in
	// DecalConstants so the PS doesn't have to invert per-pixel.
	Texture2D g_positionTex     : register(t0);
	Texture2D g_decalAlbedoTex  : register(t1);
	Texture2D g_decalNormalTex  : register(t2);
	Texture2D g_decalMatTex     : register(t3);

	SamplerState g_textureSampler : register(s0);
	SamplerState g_pointSampler   : register(s2);

	cbuffer DecalConstants : register(b4)
	{
		// Inverse of g_worldMatrix - precomputed on CPU so the PS just multiplies.
		matrix g_decalWorldInverse;

		// x = albedo weight, y = normal weight (reserved v1), z = mat weight, w = opacity
		float4 g_decalWeights;

		// x = roughness override (multiplies mat texture .g if present, else used as-is)
		// y = metallic override (same, mat .r)
		// z = normal cutoff (cosine of max angle between surface and decal +Y)
		// w = albedo-bound flag (1 if albedo texture bound, else 0)
		float4 g_decalOverrides;

		// x = normal-bound flag (reserved v1), y = mat-bound flag,
		// z = responds-to-weather flag (multiplies opacity by
		//     g_weatherSurface.puddleAmount each frame - puddle decals fade in/out
		//     with rain automatically while other decals (blood, paint, scorch
		//     marks) stay put with the flag at 0),
		// w = reserved for v2 (per-decal sort priority, fade distance)
		float4 g_decalFlags;
	};

	struct DecalOut
	{
		float4 diff : SV_TARGET0; // alpha-blended into GBUFFER_DIFFUSE
		float4 mat  : SV_TARGET1; // alpha-blended into GBUFFER_SPECULAR (mat)
	};

	DecalOut ShaderMain(DecalPSInput input)
	{
		DecalOut o = (DecalOut)0;

		// Screen-space UV from SV_POSITION (which is in pixel coords).
		const float2 screenUV = float2(input.position.x / (float)g_screenWidth,
		                               input.position.y / (float)g_screenHeight);

		// Underlying surface world position from the GBuffer position copy.
		// Position RT's .a encodes a background-flag in this engine - pixels with
		// no opaque geometry (skybox) are flagged and we leave them untouched.
		const float4 surfacePosWS = g_positionTex.Sample(g_pointSampler, screenUV);
		if (surfacePosWS.a > 0.5f)
			discard;

		// Transform sampled surface into decal-local space. Out-of-box pixels
		// (which is most of the screen for a small decal) are discarded here -
		// the cheap test that does the heavy lifting for v1.
		const float3 localPos = mul(float4(surfacePosWS.xyz, 1.0f), g_decalWorldInverse).xyz;
		if (any(abs(localPos) > 0.5f))
			discard;

		// Normal-cutoff fade. We don't bind the GBuffer normal RT as an SRV (the
		// per-RT blend state required to write decals into it without clobbering
		// the packed depth.w isn't built yet - tracked as v2). Instead the face
		// normal is reconstructed from world-position derivatives, transformed
		// into decal-local space, and compared against the projection axis
		// (local +Y). saturate(local.y) is the cosine of the surface vs decal-up
		// angle - we soft-fade between cutoff and (cutoff + small) so decals don't
		// hard-clip on slightly-angled surfaces (e.g. drainage camber).
		const float3 dPdx = ddx(surfacePosWS.xyz);
		const float3 dPdy = ddy(surfacePosWS.xyz);
		const float3 faceNormalWS    = normalize(cross(dPdy, dPdx));
		const float3 faceNormalLocal = normalize(mul(float4(faceNormalWS, 0.0f), g_decalWorldInverse).xyz);

		const float cutoff = g_decalOverrides.z;
		const float facing = saturate(faceNormalLocal.y);
		if (facing < cutoff)
			discard;
		const float facingFade = saturate((facing - cutoff) * 8.0f);

		// UV from local XZ ([-0.5, 0.5] -> [0, 1]). Local Y is the projection axis
		// so it doesn't feed UV.
		const float2 uv = saturate(localPos.xz + 0.5f);

		// Edge falloff - cubic in box space so the decal eases out at the walls
		// instead of hard-clipping. The 4.0 multiplier gives a fade band that
		// occupies the outer 25% of each box dimension; the inner 50% is full
		// opacity.
		const float3 edgeDist = saturate((0.5f - abs(localPos)) * 4.0f);
		const float  edgeFade = edgeDist.x * edgeDist.y * edgeDist.z;

		// Weather-driven puddles. When the flag is set, multiply opacity by the
		// global puddleAmount so the decal fades out when it's dry and fades back
		// in when it rains. lerp(1, puddle, flag) keeps non-weather decals at full
		// opacity (flag == 0 -> 1.0 multiplier).
		const float weatherMul = lerp(1.0f, g_weatherSurface.puddleAmount, g_decalFlags.z);
		const float baseAlpha  = g_decalWeights.w * edgeFade * facingFade * weatherMul;

		// Diffuse channel ------------------------------------------------------
		float4 decalAlbedo = float4(0.0f, 0.0f, 0.0f, 0.0f);
		if (g_decalOverrides.w > 0.5f)
		{
			decalAlbedo = g_decalAlbedoTex.Sample(g_textureSampler, uv);
		}
		// When no albedo texture is bound, write zero alpha so the diffuse RT
		// is left untouched - useful for mat-only puddle decals.
		o.diff = float4(decalAlbedo.rgb, baseAlpha * decalAlbedo.a * g_decalWeights.x * g_decalOverrides.w);

		// Material (metallic + roughness + smoothness + reserved) ---------------
		// GBuffer mat layout: r = metallic, g = roughness, b = smoothness, a reserved.
		// We always write at least the override values; if a mat texture is bound
		// it's multiplied with the overrides so artists can mask the strength.
		float matMetallic  = g_decalOverrides.y;
		float matRoughness = g_decalOverrides.x;
		float matAlpha = baseAlpha * g_decalWeights.z;
		if (g_decalFlags.y > 0.5f)
		{
			const float4 matSample = g_decalMatTex.Sample(g_textureSampler, uv);
			matMetallic  = matSample.r * g_decalOverrides.y;
			matRoughness = matSample.g * g_decalOverrides.x;
			matAlpha    *= matSample.a;
		}
		o.mat = float4(matMetallic, matRoughness, 1.0f - matRoughness, matAlpha);

		return o;
	}
}
