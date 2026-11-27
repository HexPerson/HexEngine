"GlobalIncludes"
{
	Global
}
"Global"
{
#ifndef DDGICOMMON_SHADER
#define DDGICOMMON_SHADER

	// Shared DDGI probe-volume definitions (HexEngine.Core/Scene/DDGI.cpp).
	//
	// Three camera-centred cascades of probes on a WORLD-LOCKED grid. A probe
	// is identified by its integer world grid coordinate g; its storage slot
	// is g mod dims (toroidal), so a camera move only re-initialises probes
	// entering the volume - nothing is shifted or re-centred (lesson of the
	// voxel GI: content that moves with the camera reads as lag).
	//
	// Per probe:
	//   irradiance: 6x6 octahedral texels + 1 border = 8x8, RGBA16F packed
	//               into uint2 (StructuredBuffer - SM5 cannot typed-load RW
	//               RGBA16F textures).
	//   depth:      14x14 + border = 16x16 mean/mean^2 (RG16F packed uint).
	//   state:      relocation offset, active flag, the grid coordinate it
	//               was last initialised for (a mismatch = new probe).
	//
	// Units: irradiance is the cosine-weighted average of incoming radiance
	// in ENGINE units (diffuse = albedo * N.L * lightColour, no 1/PI - see
	// PBRutils diffuse()), so the apply pass outputs albedo * irradiance.

	static const uint kDdgiCascades = 3;
	static const uint kDdgiRaysPerProbe = 128;
	static const uint kDdgiIrrInterior = 6;
	static const uint kDdgiIrrTile = 8;
	static const uint kDdgiDepthInterior = 14;
	static const uint kDdgiDepthTile = 16;

	struct DdgiCascade
	{
		int4 gridOrigin;    // xyz: world grid coord of the volume's min corner
		int4 dims;          // xyz: probes per axis, w: probe count
		float4 spacing;     // x: spacing (m), y: max ray distance, z: 1/spacing
	};

	cbuffer DdgiConstants : register(b8)
	{
		DdgiCascade g_ddgiCascade[3];
		float4 g_ddgiRayRotation0;
		float4 g_ddgiRayRotation1;
		float4 g_ddgiRayRotation2;
		uint4 g_ddgiUpdate;      // x: cascade being updated, y: period, z: phase, w: probes this dispatch
		float4 g_ddgiParams0;    // x: hysteresis, y: normal bias, z: view bias, w: bounce multiplier
		float4 g_ddgiParams1;    // x: sky intensity, y: emissive scale, z: local light count, w: sun intensity scale
		float4 g_ddgiParams2;    // x: depth sharpness, y: apply intensity, z: debug mode, w: cascade blend width (probes)
		float4 g_ddgiParams3;    // x: local light intensity scale, y: relocation enabled, z: classification enabled, w: frame
		float4 g_ddgiGround;     // x: ground plane height, y: enabled (stand-in for GPU-only voxel terrain), z: AO strength on the probe term, w: change-detector mode
		float4 g_ddgiGroundAlbedo; // rgb: ground plane albedo
		float4 g_ddgiSSGI;       // x: SSGI intensity (0 = off), y: radius (m), z: per-tap luminance cap, w: taps
		float4 g_ddgiParams4;    // x: apply sample jitter in probe spacings (0 = off), y: froxel fog left indoors, z: froxel DDGI range (m), w: sky light saturation
	};

	// Sky light saturation (r_ddgiSkySaturation): lerp toward luminance.
	float3 DdgiSkyTint(float3 sky)
	{
		const float l = dot(sky, float3(0.2126f, 0.7152f, 0.0722f));
		return max(lerp(l.xxx, sky, g_ddgiParams4.w), 0.0f.xxx);
	}

	struct DdgiProbeState
	{
		float4 offsetState;  // xyz: relocation offset (m), w: 0 active, 1 inactive
		int4 coord;          // xyz: grid coord initialised for, w: 1 once initialised
	};

	StructuredBuffer<uint2> g_ddgiIrradiance0 : register(t56);
	StructuredBuffer<uint2> g_ddgiIrradiance1 : register(t57);
	StructuredBuffer<uint2> g_ddgiIrradiance2 : register(t58);
	StructuredBuffer<uint> g_ddgiDepth0 : register(t59);
	StructuredBuffer<uint> g_ddgiDepth1 : register(t60);
	StructuredBuffer<uint> g_ddgiDepth2 : register(t61);
	StructuredBuffer<DdgiProbeState> g_ddgiState0 : register(t62);
	StructuredBuffer<DdgiProbeState> g_ddgiState1 : register(t63);
	StructuredBuffer<DdgiProbeState> g_ddgiState2 : register(t64);

	// Probe ray i of n: spherical Fibonacci, rotated by this frame's random
	// rotation (set by DDGI.cpp) so successive updates sample new directions.
	float3 DdgiSphericalFibonacci(uint i, uint n)
	{
		const float kGolden = 1.61803398875f;
		const float phi = 6.28318530718f * frac((float)i / kGolden);
		const float cosTheta = 1.0f - (2.0f * (float)i + 1.0f) / (float)n;
		const float sinTheta = sqrt(saturate(1.0f - cosTheta * cosTheta));
		return float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
	}

	float3 DdgiRayDirection(uint i)
	{
		const float3 v = DdgiSphericalFibonacci(i, kDdgiRaysPerProbe);
		return normalize(float3(dot(g_ddgiRayRotation0.xyz, v), dot(g_ddgiRayRotation1.xyz, v), dot(g_ddgiRayRotation2.xyz, v)));
	}

	float2 DdgiSignNotZero(float2 v)
	{
		return float2(v.x >= 0.0f ? 1.0f : -1.0f, v.y >= 0.0f ? 1.0f : -1.0f);
	}

	// Unit direction -> [-1,1]^2 octahedral.
	float2 DdgiOctEncode(float3 n)
	{
		const float l1 = abs(n.x) + abs(n.y) + abs(n.z);
		float2 e = n.xy / max(l1, 1e-8f);
		if (n.z < 0.0f)
			e = (1.0f - abs(e.yx)) * DdgiSignNotZero(e);
		return e;
	}

	float3 DdgiOctDecode(float2 e)
	{
		float3 v = float3(e, 1.0f - abs(e.x) - abs(e.y));
		if (v.z < 0.0f)
			v.xy = (1.0f - abs(v.yx)) * DdgiSignNotZero(v.xy);
		return normalize(v);
	}

	int3 DdgiPositiveMod(int3 a, int3 m)
	{
		return ((a % m) + m) % m;
	}

	uint DdgiSlotIndex(uint cascade, int3 g)
	{
		const int3 dims = g_ddgiCascade[cascade].dims.xyz;
		const int3 s = DdgiPositiveMod(g, dims);
		return (uint)((s.z * dims.y + s.y) * dims.x + s.x);
	}

	// Inverse of DdgiSlotIndex for the current volume placement.
	int3 DdgiSlotToGrid(uint cascade, uint slot)
	{
		const int3 dims = g_ddgiCascade[cascade].dims.xyz;
		const int3 s = int3((int)(slot % (uint)dims.x), (int)((slot / (uint)dims.x) % (uint)dims.y), (int)(slot / (uint)(dims.x * dims.y)));
		const int3 o = g_ddgiCascade[cascade].gridOrigin.xyz;
		// The g in [o, o + dims) whose positive mod equals s.
		return o + DdgiPositiveMod(s - o, dims);
	}

	float3 DdgiGridToWorld(uint cascade, int3 g)
	{
		return float3(g) * g_ddgiCascade[cascade].spacing.x;
	}

	DdgiProbeState DdgiLoadState(uint cascade, uint slot)
	{
		if (cascade == 0) return g_ddgiState0[slot];
		if (cascade == 1) return g_ddgiState1[slot];
		return g_ddgiState2[slot];
	}

	uint2 DdgiLoadIrr(uint cascade, uint index)
	{
		if (cascade == 0) return g_ddgiIrradiance0[index];
		if (cascade == 1) return g_ddgiIrradiance1[index];
		return g_ddgiIrradiance2[index];
	}

	uint DdgiLoadDepth(uint cascade, uint index)
	{
		if (cascade == 0) return g_ddgiDepth0[index];
		if (cascade == 1) return g_ddgiDepth1[index];
		return g_ddgiDepth2[index];
	}

	// Irradiance texel: rgb = cosine-weighted average radiance, a = SKY
	// VISIBILITY - the cosine-weighted fraction of the probe's rays around
	// that direction that escaped to the sky (DDGIBlendIrradiance). Used to
	// occlude the environment (sky) specular indoors, which has no
	// occlusion of its own (DdgiOccludeEnvSpecular).
	uint2 DdgiPackIrr4(float4 c)
	{
		return uint2(f32tof16(c.r) | (f32tof16(c.g) << 16), f32tof16(c.b) | (f32tof16(c.a) << 16));
	}

	float4 DdgiUnpackIrr4(uint2 p)
	{
		return float4(f16tof32(p.x & 0xffffu), f16tof32(p.x >> 16), f16tof32(p.y & 0xffffu), f16tof32(p.y >> 16));
	}

	float3 DdgiUnpackIrr(uint2 p)
	{
		return DdgiUnpackIrr4(p).rgb;
	}

	uint DdgiPackDepth(float2 m)
	{
		return f32tof16(m.x) | (f32tof16(m.y) << 16);
	}

	float2 DdgiUnpackDepth(uint p)
	{
		return float2(f16tof32(p & 0xffffu), f16tof32(p >> 16));
	}

	// Bilinear fetch of an octahedral tile (interior n, tile n+2 with border).
	float4 DdgiSampleIrr4(uint cascade, uint slot, float3 dir)
	{
		const float2 e = DdgiOctEncode(dir);
		const float2 texel = (e * 0.5f + 0.5f) * (float)kDdgiIrrInterior + 1.0f - 0.5f;
		const float2 base = floor(texel);
		const float2 f = texel - base;
		const int2 b = clamp(int2(base), int2(0, 0), int2(kDdgiIrrTile - 2, kDdgiIrrTile - 2));
		const uint tileBase = slot * kDdgiIrrTile * kDdgiIrrTile;
		const float4 c00 = DdgiUnpackIrr4(DdgiLoadIrr(cascade, tileBase + b.y * kDdgiIrrTile + b.x));
		const float4 c10 = DdgiUnpackIrr4(DdgiLoadIrr(cascade, tileBase + b.y * kDdgiIrrTile + b.x + 1));
		const float4 c01 = DdgiUnpackIrr4(DdgiLoadIrr(cascade, tileBase + (b.y + 1) * kDdgiIrrTile + b.x));
		const float4 c11 = DdgiUnpackIrr4(DdgiLoadIrr(cascade, tileBase + (b.y + 1) * kDdgiIrrTile + b.x + 1));
		return lerp(lerp(c00, c10, f.x), lerp(c01, c11, f.x), f.y);
	}

	float2 DdgiSampleDepth(uint cascade, uint slot, float3 dir)
	{
		const float2 e = DdgiOctEncode(dir);
		const float2 texel = (e * 0.5f + 0.5f) * (float)kDdgiDepthInterior + 1.0f - 0.5f;
		const float2 base = floor(texel);
		const float2 f = texel - base;
		const int2 b = clamp(int2(base), int2(0, 0), int2(kDdgiDepthTile - 2, kDdgiDepthTile - 2));
		const uint tileBase = slot * kDdgiDepthTile * kDdgiDepthTile;
		const float2 c00 = DdgiUnpackDepth(DdgiLoadDepth(cascade, tileBase + b.y * kDdgiDepthTile + b.x));
		const float2 c10 = DdgiUnpackDepth(DdgiLoadDepth(cascade, tileBase + b.y * kDdgiDepthTile + b.x + 1));
		const float2 c01 = DdgiUnpackDepth(DdgiLoadDepth(cascade, tileBase + (b.y + 1) * kDdgiDepthTile + b.x));
		const float2 c11 = DdgiUnpackDepth(DdgiLoadDepth(cascade, tileBase + (b.y + 1) * kDdgiDepthTile + b.x + 1));
		return lerp(lerp(c00, c10, f.x), lerp(c01, c11, f.x), f.y);
	}

	// Irradiance at a surface from one cascade (standard DDGI weighting:
	// trilinear x wrap-shading backface x Chebyshev visibility). Returns
	// rgb + total weight in w (0 = no valid probe); skyVis = the same
	// weighted blend of the probes' sky visibility. normal / toEye are the
	// SURFACE normal and view vector (bias, wrap, Chebyshev); lookupDir is the
	// direction read from each probe's octahedral tile - the normal for
	// irradiance, a reflection / ray direction for radiance-along-a-ray.
	// Passing a ray direction as the "normal" was wrong: the bias then
	// pushed the lookup point along the ray, through a nearby wall, into
	// outside probes that see open sky (bright seams at every room edge).
	float4 DdgiSampleCascade(uint cascade, float3 worldPos, float3 normal, float3 toEye, float3 lookupDir, out float skyVis)
	{
		const float spacing = g_ddgiCascade[cascade].spacing.x;
		const float3 bias = (normal * g_ddgiParams0.y + toEye * g_ddgiParams0.z) * spacing;
		const float3 biasedPos = worldPos + bias;
		const float3 gridPos = biasedPos * g_ddgiCascade[cascade].spacing.z;
		const int3 baseG = int3(floor(gridPos));
		const float3 alpha = saturate(gridPos - float3(baseG));

		float4 sum = 0.0f.xxxx;
		float sumW = 0.0f;
		[unroll]
		for (uint i = 0; i < 8; ++i)
		{
			const int3 off = int3(i & 1u, (i >> 1) & 1u, (i >> 2) & 1u);
			const int3 g = baseG + off;
			const int3 local = g - g_ddgiCascade[cascade].gridOrigin.xyz;
			if (any(local < 0) || any(local >= g_ddgiCascade[cascade].dims.xyz))
				continue;
			const uint slot = DdgiSlotIndex(cascade, g);
			const DdgiProbeState st = DdgiLoadState(cascade, slot);
			if (st.coord.w == 0 || any(st.coord.xyz != g) || st.offsetState.w > 0.5f)
				continue;

			const float3 probePos = DdgiGridToWorld(cascade, g) + st.offsetState.xyz;
			const float3 trilinear = lerp(1.0f - alpha, alpha, float3(off));
			float w = trilinear.x * trilinear.y * trilinear.z;

			// Wrap shading: probes behind the surface fade but never hard-cut.
			const float3 dirToProbe = normalize(probePos - worldPos);
			const float wrap = (dot(dirToProbe, normal) + 1.0f) * 0.5f;
			w *= wrap * wrap + 0.2f;

			// Chebyshev visibility from the probe's depth moments.
			const float3 probeToPoint = biasedPos - probePos;
			const float dist = length(probeToPoint);
			const float2 m = DdgiSampleDepth(cascade, slot, probeToPoint / max(dist, 1e-4f));
			float chebyshev = 1.0f;
			if (dist > m.x)
			{
				const float variance = abs(m.y - m.x * m.x);
				const float d = dist - m.x;
				chebyshev = variance / (variance + d * d);
				chebyshev = chebyshev * chebyshev * chebyshev;
			}
			w *= max(0.05f, chebyshev);

			// Crush tiny weights (DDGI): avoids a faint wash from mostly-occluded probes.
			const float crush = 0.2f;
			if (w < crush)
				w *= w * w / (crush * crush);

			sum += DdgiSampleIrr4(cascade, slot, lookupDir) * w;
			sumW += w;
		}
		const float4 avg = sumW > 1e-5f ? sum / sumW : float4(0.0f, 0.0f, 0.0f, 1.0f);
		skyVis = avg.a;
		return float4(avg.rgb, sumW);
	}

	// Distance (in probes) of a world position from the inner edge of a
	// cascade; < 0 means outside the region where all 8 corners exist.
	float DdgiCascadeInset(uint cascade, float3 worldPos)
	{
		const float3 gridPos = worldPos * g_ddgiCascade[cascade].spacing.z - float3(g_ddgiCascade[cascade].gridOrigin.xyz);
		const float3 dims = float3(g_ddgiCascade[cascade].dims.xyz);
		const float3 inset = min(gridPos, dims - 1.0f - gridPos);
		return min(min(inset.x, inset.y), inset.z);
	}

	// Full irradiance lookup: finest cascade that covers the point, blended
	// into the next one across its outer band. rgb = irradiance, a = sky
	// visibility (1 where no probe covers the point: nothing to occlude with).
	float4 DdgiSampleDirectional4(float3 worldPos, float3 normal, float3 toEye, float3 lookupDir)
	{
		const float band = max(g_ddgiParams2.w, 0.5f);
		float4 result = 0.0f.xxxx;
		float remaining = 1.0f;
		[loop]
		for (uint c = 0; c < kDdgiCascades; ++c)
		{
			const float inset = DdgiCascadeInset(c, worldPos);
			if (inset < 0.0f)
				continue;
			float vis;
			const float4 s = DdgiSampleCascade(c, worldPos, normal, toEye, lookupDir, vis);
			if (s.w <= 1e-5f)
				continue;
			const float take = (c + 1 < kDdgiCascades) ? saturate(inset / band) : 1.0f;
			result += float4(s.rgb, vis) * (remaining * take);
			remaining *= (1.0f - take);
			if (remaining <= 1e-3f)
				break;
		}
		if (remaining > 1e-3f && remaining < 1.0f)
			result /= (1.0f - remaining);
		if (remaining >= 1.0f)
			result.a = 1.0f;
		return result;
	}

	// Same lookup, but the OUTERMOST cascade also fades out across its outer
	// band instead of ending in a hard edge, and the uncovered fraction is
	// returned instead of renormalised away: coverage = 1 inside the probe
	// volume, falling to 0 at its edge. DDGIApply blends to an open-sky
	// fallback with it, so the end of the ~144 m volume no longer cuts a
	// line across the landscape that follows the camera.
	float4 DdgiSampleDirectionalCov(float3 worldPos, float3 normal, float3 toEye, float3 lookupDir, out float coverage)
	{
		const float band = max(g_ddgiParams2.w, 0.5f);
		float4 result = 0.0f.xxxx;
		float remaining = 1.0f;
		[loop]
		for (uint c = 0; c < kDdgiCascades; ++c)
		{
			const float inset = DdgiCascadeInset(c, worldPos);
			if (inset < 0.0f)
				continue;
			float vis;
			const float4 s = DdgiSampleCascade(c, worldPos, normal, toEye, lookupDir, vis);
			if (s.w <= 1e-5f)
				continue;
			const float take = saturate(inset / band);
			result += float4(s.rgb, vis) * (remaining * take);
			remaining *= (1.0f - take);
			if (remaining <= 1e-3f)
				break;
		}
		coverage = saturate(1.0f - remaining);
		if (coverage > 1e-3f)
			result /= coverage;
		else
			result = float4(0.0f, 0.0f, 0.0f, 1.0f);
		return result;
	}

	float4 DdgiSampleIrradiance4(float3 worldPos, float3 normal, float3 toEye)
	{
		return DdgiSampleDirectional4(worldPos, normal, toEye, normal);
	}

	float3 DdgiSampleIrradiance(float3 worldPos, float3 normal, float3 toEye)
	{
		return DdgiSampleDirectional4(worldPos, normal, toEye, normal).rgb;
	}

	// Environment (sky) specular occlusion for a surface. The split-sum env
	// term has no occlusion of its own, so with DDGI replacing the voxel AO
	// every matte interior surface reflected the full sky - the blue wash in
	// window rooms. Keep the env reflection by the probes' sky visibility
	// around R and fill the occluded remainder with the probes' radiance
	// around R (the same stand-in SSR uses for a missed ray). The reflected
	// FRACTION is unchanged, so callers' energy split stays valid.
	float3 DdgiOccludeEnvSpecular(float3 envSpecular, float3 specularReflectance, float3 worldPos, float3 N, float3 V, float3 R)
	{
		if (g_ddgiCascade[0].dims.w <= 0)
			return envSpecular;
		const float4 p = DdgiSampleDirectional4(worldPos, N, V, R);
		const float vis = saturate(p.a);
		return envSpecular * vis + specularReflectance * max(p.rgb, 0.0f.xxx) * (1.0f - vis);
	}

#endif
}
