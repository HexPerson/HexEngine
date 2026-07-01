"ComputeShaderIncludes"
{
	Global
	AtmosphereCommon
}
"ComputeShader"
{
	// Phase D scatter pass. Per-froxel, write:
	//   .rgb = scatter radiance entering this froxel from the sun
	//          (density * phase_function * sun_visibility * sun_colour)
	//   .a   = volumetric extinction coefficient at this point
	//
	// Camera-frustum-aligned 3D volume. Exp depth distribution so near
	// froxels (~10 cm) get fine resolution for sun shafts through nearby
	// geometry, far froxels (~256 m at the back) coarse - beyond 256 m
	// the aerial-perspective volume handles atmospheric scattering instead.
	//
	// Per-froxel pipeline:
	//   1. Reconstruct world position from (uv, w)
	//   2. Compute participating-media density at that position:
	//        - Atmospheric Mie contribution (constant base + height profile)
	//        - Authored height fog (engine's existing fogHeightDensity)
	//   3. Sample sun visibility via shadow cascade lookup
	//   4. Compute scatter = density * sunColour * sunVisibility * phase
	//
	// Local light contributions land in Phase D-3 - this pass is sun-only.

	RWTexture3D<float4> g_scatterVolume : register(u0);

	// Sun shadow cascade 0 (the closest, highest-detail cascade). v1 uses
	// only this cascade - the volume's 256 m far plane lies entirely
	// within typical first-cascade range, so per-froxel sampling needs no
	// cross-cascade selection. Pixels outside the cascade UV bounds fall
	// back to "lit" (no shadow), which avoids hard edges at the cascade
	// border but means froxels beyond ~50 m get no shadow contribution.
	// Sun shadow cascades 0..3 in sequential registers. The scatter
	// shader picks the first cascade whose NDC bounds contain the
	// froxel position - same selection logic as the engine's opaque
	// PCSS path. Cascade 0 is nearest/highest-detail; 3 is furthest.
	Texture2D    g_sunCascades[4]    : register(t0);
	// Atmospheric transmittance LUT (Hillaire model) at t4 (after the
	// 4 cascade slots). Reddens god rays at sunset - multiplying scatter
	// by sun-to-froxel transmittance gives the same warm tint the sky
	// itself shows. g_jitter.w gates the lookup (nullptr-safe).
	Texture2D    g_transmittanceLUT  : register(t4);
	// Shadow maps for up to 4 shadow-casting spot lights at t5..t8.
	// Per-forward-spot mapping in g_spotShadowSlotPerForward[] picks
	// which slot (if any) holds each spot's shadow. SM5.0 sampler-array
	// indices must be literal, so each slot is its own binding +
	// switched on by literal index in SampleSpotShadowSlot().
	Texture2D    g_spotShadow0       : register(t5);
	Texture2D    g_spotShadow1       : register(t6);
	Texture2D    g_spotShadow2       : register(t7);
	Texture2D    g_spotShadow3       : register(t8);
	// Point-light shadow cubemap array at t9. Each slot holds 6 faces
	// for one shadowed point light. Hardware cubemap face selection
	// from the sample direction means no per-face switch logic - just
	// `array.Sample(sampler, float4(toFroxelDir, slot))`.
	TextureCubeArray g_pointShadowCubes : register(t9);
	// GBuffer sources for the screen-space emissive -> froxel injection.
	// The froxel volume is camera-frustum-aligned, so a froxel's xy IS a
	// screen UV - sampling the gbuffer at that UV gives the surface this
	// froxel's ray hits. position.w carries the surface's emissive
	// intensity (length(emission), written by DefaultPixel.shader);
	// diffuse.rgb carries its tint.
	Texture2D    g_gbufferDiffuse    : register(t10);
	Texture2D    g_gbufferPosition   : register(t11);
	SamplerState g_shadowPointSampler : register(s2);
	// Linear-clamp sampler for the transmittance LUT - the LUT is a
	// continuous function so point sampling shows banding.
	SamplerState g_linearSamplerAtm  : register(s4);

	cbuffer VolumetricScatterParams : register(b5)
	{
		// .xy = volume dimensions (xy), .z = depth slices, .w = far depth (metres)
		float4 g_volumeDimsAndFar;
		// .xyz = sun direction (world, surface -> sun), .w = sun intensity
		float4 g_sunDirAndIntensity;
		// .rgb = sun colour, .a = anisotropy (g for HG phase)
		float4 g_sunColourAndPhaseG;
		// .x = base extinction (uniform fog), .y = height density, .z = height pivot, .w = height falloff
		float4 g_volumeMediumParams;
		// Per-cascade world->light-clip matrices. Indexed parallel to
		// g_sunCascades[]. Standard convention - NDC.xy in [-1,1] becomes
		// UV via .xy*0.5+0.5 then y-flip; depthFromLight = NDC.z/NDC.w.
		matrix g_sunCascadeVPs[4];
		// .x = shadow bias (NDC.z units, suppresses self-shadowing fireflies)
		// .y = 1/shadowMapWidth   (texel U size for PCF offsets)
		// .z = 1/shadowMapHeight  (texel V size for PCF offsets)
		// .w = active cascade count (0..4). Shader loops up to this many
		//      cascades looking for one whose bounds contain the froxel.
		float4 g_shadowParams;
		// .xyz = per-frame sub-froxel jitter in [-0.5, 0.5] cell space.
		// Halton(2,3,5) sequence from CPU; combined with integrate's
		// EMA gives temporal supersampling for free.
		// .w   = useTransmittance flag (1.0 = sample LUT, 0.0 = fall
		// back to uniform white). Gates the atmospheric tinting when
		// the engine doesn't have an active AtmosphereLUTs subsystem.
		float4 g_jitter;
		// Spot light shadow VPs - one per shadow SLOT (max 4). Indexed
		// by slot (0..3), not by forward-light index. The mapping array
		// below converts forward-light index -> slot index.
		matrix g_spotShadowVPs[4];
		// For each of the 16 forward spot slots, .x stores the shadow
		// slot index (0..3) that holds its shadow map, or -1 if the
		// light is unshadowed. yzw unused (16-byte alignment).
		float4 g_spotShadowSlotPerForward[16];
		// .x = shadow bias (NDC.z), .y = 1/shadowMapW, .z = 1/shadowMapH,
		// .w = numShadowedSpots (0 disables all spot shadowing in the loop).
		float4 g_spotShadowParams;
		// For each forward point slot, .x = shadow slot index (0..kMaxShadowedPoints-1)
		// in the cubemap array, or -1 if the point is unshadowed.
		float4 g_pointShadowSlotPerForward[16];
		// .x = bias in METRES (linear depth comparison, not NDC).
		// .y = numShadowedPoints (loop gate). .zw = unused.
		float4 g_pointShadowParams;
		// Screen-space emissive injection.
		// .x = strength multiplier (0 = disabled, gates the gbuffer taps).
		// .y = world-space falloff range in metres. .zw = unused.
		float4 g_emissiveParams;
		// Ambient inscatter of the fog medium. .rgb = ambient colour
		// premultiplied by strength (CPU side); contribution per froxel is
		// .rgb * extinction. Approximates multiple-scattered skylight in
		// the medium - dense storm/blizzard/sandstorm fog reads as a grey
		// or coloured soup instead of just darkening the scene. .w unused.
		float4 g_fogAmbient;
	};

	// Forward lights cbuffer. Layout MUST match SceneRenderer's
	// ForwardLightConstants struct exactly. Same instance the
	// transparency pass uses (b7 in PS), bound at b7 in CS here for
	// the volumetric scatter pass to read per-froxel.
	#define kFWD_MAX_POINT 16
	#define kFWD_MAX_SPOT  16
	cbuffer ForwardLights : register(b7)
	{
		// .x = active pointCount, .y = active spotCount
		float4 g_fwdCountsAndParams;
		float4 g_fwdReserved;
		float4 g_fwdPointPosRadius   [kFWD_MAX_POINT]; // xyz=pos, w=radius
		float4 g_fwdPointColorStrength[kFWD_MAX_POINT]; // rgb=color, w=strength
		float4 g_fwdSpotPosRadius    [kFWD_MAX_SPOT];  // xyz=pos, w=radius
		float4 g_fwdSpotDirCone      [kFWD_MAX_SPOT];  // xyz=fwd, w=cos(outerHalf)
		float4 g_fwdSpotColorStrength[kFWD_MAX_SPOT];  // rgb=color, w=strength
		float4 g_fwdSpotInnerCone    [kFWD_MAX_SPOT];  // x=cos(innerHalf)
	};

	static const float NEAR_PLANE_M = 0.1f;

	// Inverse exp distribution: w in [0,1] -> depth in [NEAR_PLANE, farDepth]
	// depth = near * pow(far/near, w). Concentrates detail near camera.
	float VolumeWToDepth(float w, float farDepth)
	{
		return NEAR_PLANE_M * pow(farDepth / NEAR_PLANE_M, w);
	}

	// Henyey-Greenstein phase function for Mie / aerosol forward scattering.
	float MiePhaseHG(float mu, float g)
	{
		const float g2 = g * g;
		const float denom = pow(max(1e-3f, 1.0f + g2 - 2.0f * g * mu), 1.5f);
		return (1.0f / (4.0f * kATM_PI)) * (1.0f - g2) / denom;
	}

	// Volumetric light falloff. Matches the LEGACY per-pixel volumetric
	// path's softened inverse-square - strict 1/d^2 collapses the
	// visible cone into a tiny bright blob right at the source with
	// nothing beyond, which is exactly the issue the legacy shader had
	// to solve for sparsely-sampled rays. The soft denominator
	// (d^2 + softness^2) caps the close-range spike, redistributing
	// energy along the cone so the whole beam is visible.
	float LocalLightFalloff(float distSq, float radius)
	{
		const float r2 = max(radius * radius, 1e-6f);
		const float dRel = sqrt(distSq / r2);
		// Quartic window goes to 0 at radius - matches engine convention.
		const float window = saturate(1.0f - dRel * dRel * dRel * dRel);
		// Softness = 8% of radius. Same constant the legacy uses.
		const float softness = max(radius * 0.08f, 0.5f);
		const float softnessSqr = softness * softness;
		return (window * window) / (distSq + softnessSqr);
	}

	// Sample the point-light cubemap shadow at `slot` for the world
	// position. Returns visibility in [0,1]. Cubemap face selection is
	// done by hardware from the (lightPos -> worldPos) direction.
	//
	// Comparison is done in LINEAR depth (metres). PointLight's per-face
	// projection uses near=1.0, far=lightRadius, FOV=90 deg. The cubemap
	// stores NDC.z from that projection; we invert it back to linear via
	//   linearDepth = (near * far) / (far - ndcZ * (far - near))
	// and compare against the froxel's distance from the light along the
	// cube's chosen face axis (the max-abs component of toFroxel, which
	// equals the face's view-space z for the selected face).
	float SamplePointShadow(int slot, float3 worldPos, float3 lightPos, float lightRadius, float biasMetres)
	{
		const float3 toFroxel = worldPos - lightPos;
		const float froxelLinearDepth = max(max(abs(toFroxel.x), abs(toFroxel.y)), abs(toFroxel.z));
		if (froxelLinearDepth <= 0.0001f)
			return 1.0f;

		const float near = 1.0f;
		const float far  = max(lightRadius, near + 1e-3f);
		const float3 sampleDir = normalize(toFroxel);
		const float sampledNdcZ = g_pointShadowCubes.SampleLevel(g_shadowPointSampler, float4(sampleDir, (float)slot), 0).x;

		// DEBUG MODES (drive via SceneRenderer pointShadowBiasMetres):
		//   bias = -1   -> force vis = 0    (verifies path runs at all)
		//   bias = -2   -> force vis = 0.5
		//   bias = -3   -> return sampledNdcZ directly (sub-1.0 visible = some data)
		//   bias = -4   -> amplify (1 - sampledNdcZ) * 100  (bright where shadow map has any geometry)
		//   bias = -5   -> return froxelLinearDepth / 20 normalised (verifies direction math)
		//   bias = -6   -> sampledNdcZ * 1000 (saturated). If you see ANYTHING bright, the
		//                  cube array has nonzero data. If still pitch black, the cube array
		//                  is reading zeros - either the copy isn't happening, the source
		//                  depth maps weren't cleared/rendered, or the SRV format is wrong.
		//   bias = -7   -> face-index visualisation. Sample dominant axis -> distinct value
		//                  per face. Lets you tell apart "no data at all" from "data on
		//                  some faces only" purely from the colour pattern visible.
		if (biasMetres <= -0.5f && biasMetres > -1.5f) return 0.0f;
		if (biasMetres <= -1.5f && biasMetres > -2.5f) return 0.5f;
		if (biasMetres <= -2.5f && biasMetres > -3.5f) return sampledNdcZ;
		if (biasMetres <= -3.5f && biasMetres > -4.5f) return saturate((1.0f - sampledNdcZ) * 100.0f);
		if (biasMetres <= -4.5f && biasMetres > -5.5f) return saturate(froxelLinearDepth / 20.0f);
		if (biasMetres <= -5.5f && biasMetres > -6.5f) return saturate(sampledNdcZ * 1000.0f);
		if (biasMetres <= -6.5f && biasMetres > -7.5f)
		{
			// Encode the dominant axis as a visibility value so each cube face shows a
			// different brightness in the volumetric. +X=0.16, -X=0.33, +Y=0.5, -Y=0.66,
			// +Z=0.83, -Z=1.0. If all 6 are visible the direction math is correct.
			float ax = abs(toFroxel.x), ay = abs(toFroxel.y), az = abs(toFroxel.z);
			if (ax >= ay && ax >= az) return toFroxel.x > 0.0f ? 0.16f : 0.33f;
			if (ay >= ax && ay >= az) return toFroxel.y > 0.0f ? 0.50f : 0.66f;
			return toFroxel.z > 0.0f ? 0.83f : 1.0f;
		}
		if (biasMetres <= -7.5f && biasMetres > -8.5f)
		{
			// Staircase histogram of sampledNdcZ. Distinct brightness bands tell you what
			// VALUE the cube faces actually hold:
			//   nearly black (0.05)  -> sampledNdcZ == 0    (uncleared / clear value lost)
			//   dim grey     (0.25)  -> 0 < sampledNdcZ <= 0.1 (geometry at near plane)
			//   mid grey     (0.50)  -> 0.1 < sampledNdcZ <= 0.5 (geometry at mid-distance)
			//   bright       (0.75)  -> 0.5 < sampledNdcZ <= 0.99 (geometry at far range)
			//   white        (1.00)  -> sampledNdcZ >= 0.99 (cleared = far plane = no occluder)
			// If the whole sphere is bright = clear=1.0 is taking effect AND PS isn't writing.
			// If the whole sphere is near-black = clear=1.0 NOT taking effect (still clearing
			// to 0) OR the shader is still writing 1.0-mask (old PS).
			if (sampledNdcZ <= 0.001f) return 0.05f;
			if (sampledNdcZ <= 0.1f)   return 0.25f;
			if (sampledNdcZ <= 0.5f)   return 0.50f;
			if (sampledNdcZ <= 0.99f)  return 0.75f;
			return 1.00f;
		}
		if (biasMetres <= -8.5f)
		{
			// WORKAROUND test: treat any sampledNdcZ below 0.02 as "uncleared / no data" and
			// fall back to "no occluder, fully lit" for that direction. If this turns the
			// blob back into a proper sphere, the bug is confirmed as "clear/write is leaving
			// bogus near-zero values" and we'd promote this clamp into the production path.
			// Otherwise the small-cube symptom has a different cause.
			const float effectiveNdcZ = sampledNdcZ < 0.02f ? 1.0f : sampledNdcZ;
			const float far_ws = max(lightRadius, 1.0f + 1e-3f);
			const float clamped = min(effectiveNdcZ, 0.99999f);
			const float occluder = (1.0f * far_ws) / max(far_ws - clamped * (far_ws - 1.0f), 1e-4f);
			return (froxelLinearDepth <= occluder + 0.1f) ? 1.0f : 0.0f;
		}

		const float ndcZClamped = min(sampledNdcZ, 0.99999f);
		const float occluderLinearDepth = (near * far) / max(far - ndcZClamped * (far - near), 1e-4f);
		return (froxelLinearDepth <= occluderLinearDepth + biasMetres) ? 1.0f : 0.0f;
	}

	// Per-froxel point-light contribution. Returns scatter radiance
	// (pre-MIE_COEFF, summed by caller). Shadow gated via TextureCubeArray
	// sample when this forward point has a shadow slot assigned (only the
	// closest-N shadow-casting points fit; rest fall through unshadowed
	// and shine through walls - same v1 limitation as too-many spots).
	float3 EvalPointLightScatter(uint i, float3 worldPos, float3 rayDir, float phaseG)
	{
		const float4 posR = g_fwdPointPosRadius[i];
		const float4 colS = g_fwdPointColorStrength[i];
		const float strength = colS.w;
		if (strength <= 0.0f)
			return float3(0.0f, 0.0f, 0.0f);

		const float3 toLight = posR.xyz - worldPos;
		const float distSq = dot(toLight, toLight);
		const float radius = posR.w;
		if (distSq >= radius * radius)
			return float3(0.0f, 0.0f, 0.0f);

		const float invDist = rsqrt(max(distSq, 1e-6f));
		const float3 lightDir = toLight * invDist;
		const float falloff = LocalLightFalloff(distSq, radius);

		// Phase: angle between view ray (camera->froxel) and froxel->light.
		// Aligned (looking toward light) = max forward-scatter value.
		const float mu = dot(rayDir, lightDir);
		const float phase = MiePhaseHG(mu, phaseG);

		// Shadow occlusion via cubemap array if this forward point has
		// a shadow slot. Lights without a slot (-1) shine through walls.
		float pointVis = 1.0f;
		const int shadowSlot = (int)g_pointShadowSlotPerForward[i].x;
		if (shadowSlot >= 0 && g_pointShadowParams.y > 0.5f)
			pointVis = SamplePointShadow(shadowSlot, worldPos, posR.xyz, radius, g_pointShadowParams.x);

		return colS.rgb * strength * falloff * phase * pointVis;
	}

	// Sample one of the (up to 4) spot-light shadow maps with a 2x2 PCF.
	// SM5.0 sampler-array indices must be literal, so this dispatches
	// on `slot` via a switch with one case per slot. Returns visibility
	// in [0,1]: 1 = fully lit, 0 = fully shadowed.
	//
	// DEBUG VIS MODE: when g_spotShadowParams.w >= 100.0, returns a
	// hard-coded constant per shadow-slot so we can verify the mapping
	// reaches the shader at all. Slot 0 -> 0.2, slot 1 -> 0.4, ...
	// This lets us tell apart "mapping never reaches shader" from
	// "shadow comparison is wrong".
	float SampleSpotShadowSlot(int slot, uint forwardSpotIdx, float3 worldPos)
	{
		const float4 sh = mul(float4(worldPos, 1.0f), g_spotShadowVPs[slot]);
		const float invW = 1.0f / max(sh.w, 1e-6f);
		const float2 uvShadow = float2(
			sh.x * invW * 0.5f + 0.5f,
			-sh.y * invW * 0.5f + 0.5f);
		const float depthFromLight = sh.z * invW;

		// Out of the spot's frustum -> no shadow data, treat as lit
		// (cone attenuation in the caller zeros it anyway).
		if (uvShadow.x < 0.0f || uvShadow.x > 1.0f ||
		    uvShadow.y < 0.0f || uvShadow.y > 1.0f ||
		    sh.w <= 0.0f || depthFromLight > 1.0f || depthFromLight < 0.0f)
			return 1.0f;

		// NDC.z comparison. g_spotShadowParams.x is the bias in NDC.z
		// units. Perspective projection compresses depth toward 1.0
		// far from the light, so spots need a larger bias than the
		// directional (orthographic) cascades.
		const float compare = depthFromLight - g_spotShadowParams.x;

		const float2 texel = g_spotShadowParams.yz;
		float acc = 0.0f;
		[unroll] for (int oy = 0; oy < 2; ++oy)
		{
			[unroll] for (int ox = 0; ox < 2; ++ox)
			{
				const float2 uvSample = uvShadow + float2(ox - 0.5f, oy - 0.5f) * texel;
				float d = 1.0f;
				switch (slot)
				{
					case 0: d = g_spotShadow0.SampleLevel(g_shadowPointSampler, uvSample, 0).x; break;
					case 1: d = g_spotShadow1.SampleLevel(g_shadowPointSampler, uvSample, 0).x; break;
					case 2: d = g_spotShadow2.SampleLevel(g_shadowPointSampler, uvSample, 0).x; break;
					case 3: d = g_spotShadow3.SampleLevel(g_shadowPointSampler, uvSample, 0).x; break;
				}
				acc += compare <= d ? 1.0f : 0.0f;
			}
		}
		return acc * 0.25f;
	}

	// Per-froxel spot-light contribution. Same as point + smooth cone
	// falloff between inner/outer cone angles.
	float3 EvalSpotLightScatter(uint i, float3 worldPos, float3 rayDir, float phaseG)
	{
		const float4 posR    = g_fwdSpotPosRadius[i];
		const float4 colS    = g_fwdSpotColorStrength[i];
		const float4 dirCone = g_fwdSpotDirCone[i];
		const float  cosInner = g_fwdSpotInnerCone[i].x;
		const float  strength = colS.w;
		if (strength <= 0.0f)
			return float3(0.0f, 0.0f, 0.0f);

		const float3 toLight = posR.xyz - worldPos;
		const float distSq = dot(toLight, toLight);
		const float radius = posR.w;
		if (distSq >= radius * radius)
			return float3(0.0f, 0.0f, 0.0f);

		const float invDist = rsqrt(max(distSq, 1e-6f));
		const float3 lightDir = toLight * invDist;

		// Cone falloff: spot emits along dirCone.xyz; we test the
		// light->froxel direction (= -lightDir) against the cone axis.
		// dirCone.w = cos(outerHalfAngle); cosInner = cos(innerHalfAngle).
		// Cones with smaller angle have larger cosine, so cosInner > cosOuter.
		const float cosOuter = dirCone.w;
		const float cosTheta = dot(-lightDir, dirCone.xyz);
		if (cosTheta < cosOuter)
			return float3(0.0f, 0.0f, 0.0f);
		const float coneFalloff = smoothstep(cosOuter, cosInner, cosTheta);

		const float falloff = LocalLightFalloff(distSq, radius);
		const float mu = dot(rayDir, lightDir);
		const float phase = MiePhaseHG(mu, phaseG);

		// Shadow occlusion: if this forward spot index has a shadow
		// slot assigned, sample the slot's shadow map and gate the
		// contribution. Lights with no shadow slot (-1) fall through
		// unshadowed (v1 behaviour, shines through walls). Skipping
		// the lookup costs one float compare per froxel per light.
		float spotVis = 1.0f;
		const int shadowSlot = (int)g_spotShadowSlotPerForward[i].x;
		if (shadowSlot >= 0 && g_spotShadowParams.w > 0.5f)
			spotVis = SampleSpotShadowSlot(shadowSlot, i, worldPos);

		return colS.rgb * strength * coneFalloff * falloff * phase * spotVis;
	}

	[numthreads(8, 8, 8)]
	void ShaderMain(uint3 dtid : SV_DispatchThreadID)
	{
		const uint3 dims = uint3(g_volumeDimsAndFar.xyz);
		if (any(dtid >= dims))
			return;

		// Centre of froxel in (uv, w) parameter space, jittered by the
		// per-frame Halton offset so consecutive frames sample different
		// sub-froxel positions. The integrate pass's temporal EMA blend
		// averages them into a stable image - this is what hides the
		// raw froxel-boundary stairsteps without needing to bump the
		// volume resolution. The +0.5f keeps the un-jittered position
		// at the cell centre when g_jitter.xyz == (0,0,0).
		const float3 uvw = (float3(dtid) + 0.5f + g_jitter.xyz) / float3(dims);

		// Reconstruct world ray direction from screen UV. Same trick the AP
		// volume uses: unproject the far-plane NDC, take direction from eye.
		const float2 ndcXY = float2(uvw.x * 2.0f - 1.0f, 1.0f - uvw.y * 2.0f);
		const float4 farH  = mul(float4(ndcXY, 1.0f, 1.0f), g_viewProjectionMatrixInverse);
		const float3 worldFar = farH.xyz / max(farH.w, 1e-6f);
		const float3 rayDir   = normalize(worldFar - g_eyePos.xyz);

		// Froxel world position via exp depth.
		const float depthM = VolumeWToDepth(uvw.z, g_volumeDimsAndFar.w);
		const float3 worldPos = g_eyePos.xyz + rayDir * depthM;

		// Participating media density. Combines:
		//   - Atmospheric Mie (a flat low-altitude contribution)
		//   - Authored height fog with exponential profile around the pivot
		// Resulting "density" is in units of per-metre extinction.
		const float baseExt    = g_volumeMediumParams.x;
		const float heightDens = g_volumeMediumParams.y;
		const float heightPiv  = g_volumeMediumParams.z;
		const float heightFall = g_volumeMediumParams.w;
		const float heightBand = exp2(-(worldPos.y - heightPiv) * heightFall);
		const float density    = baseExt + heightDens * heightBand;
		// Extinction stored per-step (volume integration multiplies by
		// step length later). Keep separate so integration can apply
		// per-froxel step distance.
		const float extinction = max(0.0f, density);

		// Sun light contribution.
		const float3 sunDir       = g_sunDirAndIntensity.xyz;
		const float  sunIntensity = g_sunDirAndIntensity.w;
		const float3 sunColour    = g_sunColourAndPhaseG.rgb;
		const float  phaseG       = g_sunColourAndPhaseG.a;

		// Multi-cascade sun shadow lookup. Walk cascades 0..N-1 (N from
		// g_shadowParams.w), use the first one whose NDC bounds contain
		// the froxel position. Each cascade covers progressively more
		// world space at progressively lower resolution, so picking the
		// nearest valid one gives the sharpest shadows where available.
		//
		// Visibility is GATED on shadow data: if no cascade contains the
		// froxel, visibility stays at 0. The legacy per-pixel shader did
		// the same (stopped marching past shadow range), which is what
		// gives the "directional rays only where occlusion exists" look
		// instead of the previous uniform-haze-everywhere result.
		//
		// Per-cascade PCF: 2x2 manual kernel for soft edges so adjacent
		// froxels at the cascade transitions don't show stair-steps.
		// Combine the sun shadow across ALL cascades that comfortably contain the
		// froxel by taking the MINIMUM visibility (most-shadowed wins), instead
		// of only sampling the single highest-detail cascade. This fixes sun
		// light leaking into enclosed interiors: with a low sun an overhead
		// occluder (a building roof) gets clipped out of the tight NEAR cascade's
		// depth range, but a WIDER far cascade still captures it - min-combining
		// lets that far cascade shadow the froxel even when the near one shows
		// "no occluder". Only well-contained cascades (fade > 0.5, i.e. > 2.5%
		// interior margin) take part, so we never min against garbage sampled at
		// a cascade border. If no cascade contains the froxel, visibility stays 0
		// (out of shadow range -> no sun inscatter, the original gating).
		float sunVisibility = 0.0f;
		float anyCascade = 0.0f;
		const uint cascadeCount = (uint)g_shadowParams.w;
		const float bias        = g_shadowParams.x;
		const float2 texel      = g_shadowParams.yz;
		// SM5.0 sampler-array indices MUST be literal expressions -
		// runtime loop counters don't qualify even with [unroll]. So
		// we manually instantiate the per-cascade logic with literal
		// indices via a macro. Same body for each cascade; the
		// cascadeCount / bestFade guards short-circuit unused work.
		#define TRY_CASCADE(IDX)                                                                      \
			if (cascadeCount > (IDX))                                                                 \
			{                                                                                         \
				const float4 shadowH_##IDX = mul(float4(worldPos, 1.0f), g_sunCascadeVPs[IDX]);       \
				const float invW_##IDX = 1.0f / max(shadowH_##IDX.w, 1e-6f);                          \
				const float2 shadowUv_##IDX = float2(                                                 \
					shadowH_##IDX.x * invW_##IDX * 0.5f + 0.5f,                                       \
					-shadowH_##IDX.y * invW_##IDX * 0.5f + 0.5f);                                     \
				const float depthFromLight_##IDX = shadowH_##IDX.z * invW_##IDX;                      \
				const float marginX_##IDX = min(shadowUv_##IDX.x, 1.0f - shadowUv_##IDX.x);           \
				const float marginY_##IDX = min(shadowUv_##IDX.y, 1.0f - shadowUv_##IDX.y);           \
				const float marginZ_##IDX = min(depthFromLight_##IDX, 1.0f - depthFromLight_##IDX);   \
				const float marginMin_##IDX = min(min(marginX_##IDX, marginY_##IDX), marginZ_##IDX);  \
				const float fade_##IDX = saturate(marginMin_##IDX / 0.05f);                           \
				if (fade_##IDX > 0.5f)                                                                \
				{                                                                                     \
					const float compare_##IDX = depthFromLight_##IDX - bias;                          \
					float acc_##IDX = 0.0f;                                                           \
					[unroll] for (int oy_##IDX = 0; oy_##IDX < 2; ++oy_##IDX)                         \
					{                                                                                 \
						[unroll] for (int ox_##IDX = 0; ox_##IDX < 2; ++ox_##IDX)                     \
						{                                                                             \
							const float2 uv_##IDX = shadowUv_##IDX +                                  \
								float2(ox_##IDX - 0.5f, oy_##IDX - 0.5f) * texel;                     \
							const float d_##IDX =                                                     \
								g_sunCascades[IDX].SampleLevel(g_shadowPointSampler, uv_##IDX, 0).x;  \
							acc_##IDX += compare_##IDX <= d_##IDX ? 1.0f : 0.0f;                      \
						}                                                                             \
					}                                                                                 \
					const float vis_##IDX = acc_##IDX * 0.25f;                                        \
					sunVisibility = (anyCascade > 0.0f) ? min(sunVisibility, vis_##IDX) : vis_##IDX;  \
					anyCascade = 1.0f;                                                                \
				}                                                                                     \
			}

		// CSM cascades typically nest (cascade N+1 strictly contains
		// cascade N), so the per-cascade 5% interior-margin fade-out
		// naturally cross-blends with the next cascade's full-coverage
		// interior. We try each cascade and keep the BEST fade (largest
		// interior margin) = highest-detail cascade that comfortably
		// contains the point. The `bestFade < 1.0f` guard early-outs
		// once we have a fully-interior hit.
		TRY_CASCADE(0)
		TRY_CASCADE(1)
		TRY_CASCADE(2)
		TRY_CASCADE(3)
		#undef TRY_CASCADE

		// Phase function evaluated for this view ray's angle to sun.
		const float mu = dot(rayDir, sunDir);
		const float phase = MiePhaseHG(mu, phaseG);

		// Atmospheric transmittance for the sun's path TO this froxel.
		// This is what makes god rays orange at sunset - blue light is
		// absorbed by the long horizontal atmosphere path, so what
		// reaches the fog volume to scatter is already reddened. Same
		// physics as Hillaire's aerial perspective integrand.
		//
		// Sun direction in sky-aligned local frame (matches transmittance
		// LUT param convention: azimuth-symmetric, only y/zenith matters).
		float3 sunTransmittance = float3(1.0f, 1.0f, 1.0f);
		if (g_jitter.w > 0.5f)
		{
			const float sunCosZenithLocal = clamp(sunDir.y, -1.0f, 1.0f);
			const float sunSinZenithLocal = sqrt(max(0.0f, 1.0f - sunCosZenithLocal * sunCosZenithLocal));
			const float3 sunDirLocal = float3(sunSinZenithLocal, sunCosZenithLocal, 0.0f);

			// Lift world position into Hillaire's planet-centred frame
			// (Mm units, +Y = up). groundRadiusMM comes from
			// AtmosphereCommon (engine-wide constant 6.360 Mm).
			const float3 pAtm = float3(worldPos.x * 1e-6f,
			                            groundRadiusMM + worldPos.y * 1e-6f,
			                            worldPos.z * 1e-6f);
			const float pAlt    = length(pAtm);
			const float pSunCos = dot(normalize(pAtm), sunDirLocal);
			const float2 transUv = TransmittanceLutParamsToUv(pAlt, pSunCos);
			sunTransmittance = g_transmittanceLUT.SampleLevel(g_linearSamplerAtm, transUv, 0).rgb;
		}

		// Scatter radiance entering this froxel. Independent of fog
		// density (god rays visible on clear days), modulated by the
		// atmospheric transmittance for sunset/sunrise warmth.
		//
		// MIE_COEFF is the per-metre scattering rate. Tuned to the
		// "rays only where shadows have gaps" approach - now that
		// visibility is gated on cascade data (zero outside), the
		// inscatter only accumulates in actually-lit slivers, so we
		// can afford a much lower base coefficient. Result: visible
		// directional rays where light punches through gaps, near-zero
		// haze everywhere else - matches the legacy per-pixel shader's
		// silhouette behaviour. env_volumetricStrength multiplies on
		// top for artist control.
		const float MIE_COEFF = 0.025f;
		const float3 effectiveSunColour = sunColour * sunTransmittance;
		float3 totalScatter = effectiveSunColour * sunIntensity * sunVisibility * phase;

		// Per-froxel point + spot light contributions. NO shadow gating
		// in v1 - local lights shine through walls. Same forward-lights
		// cbuffer the transparency pass consumes, populated by
		// SceneRenderer::SetupForwardLights once per frame. Each light
		// adds its own phase-weighted contribution: looking toward a
		// light = bright shaft, sideways = dim, behind = near zero.
		float3 localScatter = float3(0.0f, 0.0f, 0.0f);
		const uint fwdPointCount = (uint)g_fwdCountsAndParams.x;
		const uint fwdSpotCount  = (uint)g_fwdCountsAndParams.y;
		[loop] for (uint pi = 0u; pi < fwdPointCount; ++pi)
			localScatter += EvalPointLightScatter(pi, worldPos, rayDir, phaseG);
		[loop] for (uint si = 0u; si < fwdSpotCount; ++si)
			localScatter += EvalSpotLightScatter(si, worldPos, rayDir, phaseG);

		// Screen-space EMISSIVE injection: surfaces with emissive materials
		// (neon, lit windows, screens) glow into the fog around them. The
		// froxel's xy is a screen UV; the gbuffer sample at that UV is the
		// surface this froxel's ray hits. Distance falloff between the froxel
		// and the surface world position spreads the glow into the volume in
		// front of the emitter; froxels behind the surface also accumulate
		// but never display (the apply pass samples the volume at the scene
		// depth, so occluded slices simply aren't read).
		//
		// LIMITATIONS (screen-space by construction): emitters off-screen or
		// fully occluded contribute nothing, and the glow fades with the
		// emitter at screen edges. The principled upgrade would be sampling
		// the DiffuseGI voxel volume (which already has emissive baked into
		// its radiance) - revisit if the screen-space artefacts ever bother.
		if (g_emissiveParams.x > 0.0f)
		{
			// One froxel column covers a LARGE screen footprint (the volume is
			// only 128x72 across the screen - roughly 15x15 pixels per froxel
			// at 1080p). A single point tap of the gbuffer aliases any high-
			// frequency emitter (neon sign letters) into froxel-sized blocks:
			// the tap either lands on a letter (full glow) or between letters
			// (none). Instead, take 4 rotated-grid taps spread across the
			// froxel's footprint with the LINEAR sampler - each tap is itself
			// a bilinear average, so 4 taps approximate a box filter over the
			// cell and the glow follows the sign's average coverage smoothly.
			//
			// The falloff is evaluated PER TAP (not on averaged values): the
			// position RT has depth discontinuities at emitter silhouettes
			// (sign face vs wall behind), and averaging positions first would
			// fabricate mid-air surfaces at wrong distances.
			const float2 cellUv = 1.0f / float2(dims.xy);
			const float2 tapOffsets[4] = {
				float2( 0.375f,  0.125f),
				float2(-0.125f,  0.375f),
				float2(-0.375f, -0.125f),
				float2( 0.125f, -0.375f)
			};

			const float range = g_emissiveParams.y;
			const float soft = max(range * 0.08f, 0.25f);
			const float softSqr = soft * soft;
			const float invRangeSq = 1.0f / (range * range);

			float3 emissiveGlow = float3(0.0f, 0.0f, 0.0f);
			[unroll]
			for (int e = 0; e < 4; ++e)
			{
				const float2 tapUv = uvw.xy + tapOffsets[e] * cellUv;
				const float4 posEm = g_gbufferPosition.SampleLevel(g_linearSamplerAtm, tapUv, 0);
				// .w <= 0 covers both "no emission" (0) and the sky flag (-1).
				if (posEm.w <= 0.0f)
					continue;

				const float3 tint = g_gbufferDiffuse.SampleLevel(g_linearSamplerAtm, tapUv, 0).rgb;
				const float3 toSurface = posEm.xyz - worldPos;
				const float distSq = dot(toSurface, toSurface);
				// Same falloff family as LocalLightFalloff: quartic window to
				// zero at `range`, soft inverse-square so the peak right at
				// the surface doesn't blow out a single froxel.
				const float dRel2 = distSq * invRangeSq;
				const float window = saturate(1.0f - dRel2 * dRel2);
				const float falloff = (window * window) / (distSq + softSqr);
				emissiveGlow += tint * posEm.w * falloff;
			}
			localScatter += emissiveGlow * (0.25f * g_emissiveParams.x);
		}
		// Local lights need a stronger coefficient than the sun: the
		// sun's "intensity" slot already encodes a multiplier matched
		// to the atmospheric LUT scale, while local lights' strength
		// values come from PBR direct-lighting tuning (where 1/d^2
		// makes the lit pixel bright but very little energy lands far
		// from the source). For volumetrics we want the whole COLUMN
		// of fog the light passes through to be visible, so the per-
		// metre coefficient is higher. Scales with env_volumetricStrength
		// via sunIntensity (which factors in scatteringStrength).
		const float LOCAL_MIE_COEFF = MIE_COEFF * 6.0f;

		// Ambient inscatter of the medium itself: extinction * premultiplied
		// ambient colour. Unlike the sun/local terms (which use constant Mie
		// coefficients so god rays stay visible on clear days), this scales
		// with the actual fog density - thin haze gains almost nothing, a
		// blizzard's medium glows toward the ambient colour. As integration
		// accumulates, inscatter saturates toward scatter/extinction =
		// g_fogAmbient.rgb, i.e. distant thick fog converges exactly on the
		// weather-authored ambient - which is also what the apply pass's
		// beyond-range analytic continuation converges to, keeping the
		// 128m handoff seamless.
		// Skylight is occluded inside buildings just like the sun. With no per-
		// froxel sky-visibility term, gate the ambient by the sun's shadow
		// visibility - the one occlusion signal we have here. Fog in sun-shadowed
		// volumes (building interiors, deep shade) then stops glowing with full
		// open-sky skylight, which is what made the froxels read as "sun leaking
		// through walls" in enclosed rooms. A small floor keeps outdoor open
		// shade from going pitch black (it still sees most of the sky). It is an
		// approximation: outdoor shadow loses a little ambient haze too, but it
		// removes the interior leak cheaply. Tunable via the 0.1 floor.
		const float ambientSkyAccess = lerp(0.1f, 1.0f, sunVisibility);
		const float3 ambientScatter = g_fogAmbient.rgb * extinction * ambientSkyAccess;

		const float3 scatter = MIE_COEFF * totalScatter + LOCAL_MIE_COEFF * localScatter + ambientScatter;

		g_scatterVolume[dtid] = float4(scatter, extinction);
	}
}
