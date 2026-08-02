"Global"
{
#ifndef SNOW_COMMON_SHADER
#define SNOW_COMMON_SHADER

	// Shared by the snow-tessellation VS/HS/DS (DefaultSnowTess.shader).
	// Kept free of pixel-shader intrinsics so it is safe to include in a
	// domain shader (PBRutils drags in PS-only lighting code and cannot).
	//
	// The height field MUST match PBRutils::SnowHeightField byte-for-byte -
	// the domain shader displaces geometry by it while the pixel shader
	// shades relief/POM from the PBRutils copy; any divergence would make
	// the lit micro-relief slide off the displaced macro surface.

	// Per-control-point payload: VS -> HS -> DS. World space so the DS can
	// interpolate + displace without re-fetching instance transforms.
	struct SnowCP
	{
		float3 worldPos   : WORLDPOS;
		float3 worldPrev  : WORLDPREV;
		float3 normal     : NORMAL;
		float3 tangent    : TANGENT;
		float3 binormal   : BINORMAL;
		float2 texcoord   : TEXCOORD0;
		float4 colour     : COLOR0;
		uint   instanceID : INSTANCEID;
	};

	// Hull patch-constant output (tri domain).
	struct SnowHSConst
	{
		float edges[3] : SV_TessFactor;
		float inside   : SV_InsideTessFactor;
	};

	// Metres of snow at full thickness - the vertical displacement ceiling.
	#define SNOW_MAX_HEIGHT 0.15f

	float SnowHeight_Hash13(float3 p)
	{
		p = frac(p * 0.1031f);
		p += dot(p, p.yzx + 33.33f);
		return frac((p.x + p.y) * p.z);
	}

	float SnowHeight_Noise3(float3 p)
	{
		const float3 pi = floor(p);
		const float3 pf = frac(p);
		const float3 w  = pf * pf * (3.0f - 2.0f * pf);

		const float n000 = SnowHeight_Hash13(pi + float3(0,0,0));
		const float n100 = SnowHeight_Hash13(pi + float3(1,0,0));
		const float n010 = SnowHeight_Hash13(pi + float3(0,1,0));
		const float n110 = SnowHeight_Hash13(pi + float3(1,1,0));
		const float n001 = SnowHeight_Hash13(pi + float3(0,0,1));
		const float n101 = SnowHeight_Hash13(pi + float3(1,0,1));
		const float n011 = SnowHeight_Hash13(pi + float3(0,1,1));
		const float n111 = SnowHeight_Hash13(pi + float3(1,1,1));

		const float nx00 = lerp(n000, n100, w.x);
		const float nx10 = lerp(n010, n110, w.x);
		const float nx01 = lerp(n001, n101, w.x);
		const float nx11 = lerp(n011, n111, w.x);
		const float nxy0 = lerp(nx00, nx10, w.y);
		const float nxy1 = lerp(nx01, nx11, w.y);
		return lerp(nxy0, nxy1, w.z);
	}

	// Matches PBRutils::SnowHeightField exactly.
	float SnowHeight_Field(float2 xz)
	{
		const float h1 = SnowHeight_Noise3(float3(xz.x, 0.0f, xz.y) / 0.45f);
		const float h2 = SnowHeight_Noise3(float3(xz.x, 3.7f, xz.y) / 0.13f);
		return h1 * 0.7f + h2 * 0.3f;
	}

	// Vertical snow displacement (metres) at a world position + geometric
	// normal. Same slope/coverage/melt gating the pixel shader's snow mask
	// uses, so the raised geometry and the shaded snow agree on where snow
	// is. Steep faces (normal.y low) and zero coverage produce no lift.
	float SnowDisplacement(float3 worldPos, float3 normalWS, float snowCoverage, float snowMelt)
	{
		const float slope = smoothstep(0.35f, 0.85f, normalWS.y);
		const float h = SnowHeight_Field(worldPos.xz);
		const float thickness = saturate(slope * (0.2f + h * 1.4f) * snowCoverage)
			* (1.0f - saturate(snowMelt));
		return thickness * SNOW_MAX_HEIGHT;
	}

#endif
}
