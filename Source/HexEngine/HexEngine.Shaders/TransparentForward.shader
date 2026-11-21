"GlobalIncludes"
{
	// Listed in reverse: the compiler prepends each, so the LAST name lands first.
	ShadowUtils
	EnvMapCommon
	MeshCommon
	Utils
	Atmosphere
	PBRutils
}
"Global"
{
#ifndef TRANSPARENTFORWARD_SHADER
#define TRANSPARENTFORWARD_SHADER
	// Forward lighting for the transparency phase, shared by DefaultPixel and the
	// material-graph compiler (graph-authored glass). Every resource below is bound
	// for the whole pass by SceneRenderer::RenderTransparent, independent of the
	// material, so any shader that includes this gets them. Material textures must
	// stay below t10. The includer declares the s0 sampler and passes it in.

	// Screen-space inputs used by the transparency-phase PBR path. Bound by SceneRenderer
	// before the transparent pass; only sampled when g_material.isInTransparencyPhase != 0.
	Texture2D g_sceneColourTex   : register(t10); // opaque beauty (snapshot pre-transparency)
	Texture2D g_sceneDepthTex    : register(t11); // opaque depth buffer (linear-encoded raw depth)
	Texture2D g_sceneNormalTex   : register(t12); // opaque world normal (xyz) + viewspace depth (w)
	Texture2D g_scenePositionTex : register(t13); // opaque world position (xyz)
	// Prefiltered sky environment atlas (IBL). The transparency reflection falls
	// back to this wherever the screen-space march finds nothing - without it,
	// glass reflects black, which is why window panes read as dark holes.
	Texture2D g_iblSkyEnvFwd     : register(t14);
	// Per-fragment atmosphere for the transparency phase (see
	// TransparentAtmosphere.shader): froxel integration volume + aerial
	// perspective volume, bound by RenderTransparent.
	Texture3D g_transFogVolume   : register(t24);
	Texture3D g_transApVolume    : register(t21);
	SamplerState g_transLinearSampler : register(s4);
	#include "TransparentAtmosphere.shader"

	SamplerComparisonState g_cmpSampler : register(s1);
	// Engine-global point sampler (same s2 every deferred pass uses);
	// CalculateShadows wants it alongside the comparison sampler.
	SamplerState g_pointSamplerFwd : register(s2);

	// Sun shadow cascades for the transparency phase, bound by
	// SceneRenderer::RenderTransparent when r_transparentShadows is on.
	// t15..t20 - the one free six-slot run in this shader's layout. Unbound
	// maps read as fully shadowed, which is why the shader gates on
	// g_taaParams.z rather than sampling unconditionally.
	// (A/B-measured 2026-07-30: this block + the PCSS include cost nothing
	// measurable when the gate is off - the fps<20 hunt ruled it out.)
	SHADOWMAPS_RESOURCE(15)

	// Up to 16 point + 16 spot lights gathered in SceneRenderer::SetupForwardLights().
	// Slot b7 is shared with the particle path which uses the same packing.
	cbuffer ForwardLightsBuffer : register(b7)
	{
		float4 g_fwdCountsAndParams; // x=pointCount, y=spotCount
		float4 g_fwdReserved;
		float4 g_fwdPointPosRadius[16];
		float4 g_fwdPointColorStrength[16];
		float4 g_fwdSpotPosRadius[16];
		float4 g_fwdSpotDirCone[16];           // (dir.xyz, cos(outerHalfAngle))
		float4 g_fwdSpotColorStrength[16];
		float4 g_fwdSpotInnerCone[16];         // .x = cos(innerHalfAngle)
	};

	// Clustered light lists (Phase 2 slice 4). When g_clusterForwardActive is
	// set, forward-lit surfaces (glass, alpha-blend) read ALL local lights
	// from these instead of the closest-16 arrays above. t27+ to stay clear
	// of every material/shadow/env slot; null binds read zero counts.
	struct ClFwdLight
	{
		float4 posRadius;
		float4 colorStrength;
		float4 dirCone;   // spot: xyz dir, w cos(outer)
		float4 params;    // x cos(inner) | area range, y type (0 point, 1 spot, 2 tube, 3 rect), z shadowed
		float4 shape;     // area lights - see PBRutils' packing doc
	};
	StructuredBuffer<ClFwdLight> g_clfLights : register(t27);
	StructuredBuffer<uint>       g_clfCounts : register(t28);
	StructuredBuffer<uint>       g_clfLists  : register(t29);

	// Direct-only PBR shading for a single analytical light (no ambient, no lightning extras).
	// Mirrors the BRDF inside CalculatePBRSurface so glass / alpha-blended meshes get the same
	// energy-conserving response the deferred opaque pipeline produces.
	float3 PBRDirectLight(float3 worldPos, float3 worldNormal, float3 baseColour,
		float metalness, float perceptualRoughness, float3 L, float3 lightColour, float attenuation)
	{
		metalness = saturate(metalness);
		perceptualRoughness = clamp(perceptualRoughness, MinRoughness, 1.0f);
		perceptualRoughness = ApplySpecularAntiAliasing(worldNormal, perceptualRoughness);
		const float alphaRoughness = perceptualRoughness * perceptualRoughness;

		const float3 diffuseColor = (baseColour * (float3(1.0f, 1.0f, 1.0f) - f0)) * (1.0f - metalness);
		const float3 specularColor = lerp(f0, baseColour, metalness);
		const float reflectance = max(max(specularColor.r, specularColor.g), specularColor.b);
		const float reflectance90 = saturate(reflectance * 25.0f);
		const float3 R0 = specularColor;
		const float3 R90 = float3(1.0f, 1.0f, 1.0f) * reflectance90;

		const float3 V = normalize(g_eyePos.xyz - worldPos);
		const float3 H = normalize(L + V);
		const float NdotL = clamp(dot(worldNormal, L), 0.001f, 1.0f);
		const float NdotV = abs(dot(worldNormal, V)) + 0.001f;
		const float NdotH = saturate(dot(worldNormal, H));
		const float VdotH = saturate(dot(V, H));

		const float3 F = specularReflection(R0, R90, VdotH);
		const float G = geometricOcclusion(NdotL, NdotV, alphaRoughness);
		const float D = microfacetDistribution(NdotH, alphaRoughness);
		const float3 diffuseContrib = (1.0f - F) * diffuse(diffuseColor);
		const float3 specContrib = F * G * D / (4.0f * NdotL * NdotV);
		return NdotL * lightColour * attenuation * (diffuseContrib + specContrib);
	}

	// Accumulate forward point + spot light contributions. Direct-only (caller adds ambient
	// once for the whole surface to avoid the N-times-ambient bug).
	float3 AccumulateForwardLights_PBR(float3 worldPos, float3 worldNormal, float3 baseColour,
		float metalness, float roughness)
	{
		float3 accum = float3(0.0f, 0.0f, 0.0f);

		// Clustered path: every local light, uncapped, from the same lists the
		// deferred apply and froxel volume consume. Shadowed lights are NOT
		// skipped here - the forward path has never sampled local shadows, so
		// including them matches the old arrays' behaviour exactly, just
		// without the closest-16 cap. The 16+16 arrays are not read at all in
		// this branch, so there is nothing to double-count.
		if (g_clusterForwardActive > 0.5f)
		{
			float4 clip = mul(float4(worldPos, 1.0f), g_viewProjectionMatrix);
			if (clip.w > 0.0f)
			{
				const float2 ndc = clip.xy / clip.w;
				const float2 cuv = float2(ndc.x * 0.5f + 0.5f, 1.0f - (ndc.y * 0.5f + 0.5f));
				const float4 viewPos = mul(float4(worldPos, 1.0f), g_viewMatrix);
				const float viewDepth = -viewPos.z;
				// Same grid + exponential slicing as ClusterLightCull.
				const uint ccx = min((uint)(saturate(cuv.x) * 16.0f), 15u);
				const uint ccy = min((uint)(saturate(cuv.y) * 9.0f), 8u);
				const float cw = log(max(viewDepth, 0.1f) / 0.1f) / log(128.0f / 0.1f);
				const uint ccz = min((uint)(saturate(cw) * 32.0f), 31u);
				const uint clusterIdx = (ccz * 9u + ccy) * 16u + ccx;
				const uint cCount = min(g_clfCounts[clusterIdx], 64u);
				[loop]
				for (uint ci = 0u; ci < cCount; ++ci)
				{
					const ClFwdLight cl = g_clfLights[g_clfLists[clusterIdx * 64u + ci]];
					const float3 clToLight = cl.posRadius.xyz - worldPos;
					const float clDistSq = dot(clToLight, clToLight);
					const float clRadius = max(0.05f, cl.posRadius.w);
					if (clDistSq >= clRadius * clRadius)
						continue;
					const float clDist = sqrt(max(1e-6f, clDistSq));
					// Area lights (tube / rect) - PBRutils' ShadeAreaLight; the
					// bounding-radius reject above already applied.
					if (cl.params.y > 1.5f)
					{
						accum += ShadeAreaLight(worldPos, worldNormal, baseColour, metalness, roughness,
							cl.colorStrength.rgb * cl.colorStrength.w, cl.params.y,
							cl.posRadius.xyz, cl.dirCone, cl.shape, cl.params.x);
						continue;
					}
					const float3 clL = clToLight / clDist;
					float coneAtten = 1.0f;
					if (abs(cl.params.y - 1.0f) < 0.5f)
					{
						const float cosOuter = cl.dirCone.w;
						const float cosInner = max(cl.params.x, cosOuter + 1e-4f);
						coneAtten = smoothstep(cosOuter, cosInner, dot(-clL, normalize(cl.dirCone.xyz)));
						if (coneAtten <= 0.0f)
							continue;
					}
					const float clMinDistSqr = 0.01f * 0.01f;
					float clFalloff = saturate(1.0f - pow(clDist / clRadius, 4.0f));
					clFalloff *= clFalloff;
					const float clAtten = (clFalloff / max(clDistSq, clMinDistSqr)) * coneAtten;
					accum += PBRDirectLight(worldPos, worldNormal, baseColour, metalness, roughness,
						clL, cl.colorStrength.rgb * cl.colorStrength.w, clAtten);
				}
			}
			return accum;
		}

		const uint pointCount = min((uint)g_fwdCountsAndParams.x, 16u);
		[loop]
		for (uint pi = 0u; pi < pointCount; ++pi)
		{
			const float3 lightPos = g_fwdPointPosRadius[pi].xyz;
			const float radius = max(0.05f, g_fwdPointPosRadius[pi].w);
			const float3 toLight = lightPos - worldPos;
			const float distSq = dot(toLight, toLight);
			const float radiusSq = radius * radius;
			if (distSq >= radiusSq)
				continue;
			const float dist = sqrt(max(1e-6f, distSq));
			const float3 L = toLight / dist;
			// Physical inverse-square + smooth-window attenuation, same form as the
			// deferred PointLight/SpotLight shaders so forward-transparent surfaces
			// (glass, alpha-blended decals) match the opaque pipeline's falloff.
			const float minDistSqr = 0.01f * 0.01f;
			float distanceFalloff = saturate(1.0f - pow(dist / radius, 4.0f));
			distanceFalloff *= distanceFalloff;
			const float atten = distanceFalloff / max(distSq, minDistSqr);
			const float3 colour = g_fwdPointColorStrength[pi].rgb * g_fwdPointColorStrength[pi].w;
			accum += PBRDirectLight(worldPos, worldNormal, baseColour, metalness, roughness,
				L, colour, atten);
		}

		const uint spotCount = min((uint)g_fwdCountsAndParams.y, 16u);
		[loop]
		for (uint si = 0u; si < spotCount; ++si)
		{
			const float3 lightPos = g_fwdSpotPosRadius[si].xyz;
			const float radius = max(0.05f, g_fwdSpotPosRadius[si].w);
			const float3 toLight = lightPos - worldPos;
			const float distSq = dot(toLight, toLight);
			const float radiusSq = radius * radius;
			if (distSq >= radiusSq)
				continue;
			const float dist = sqrt(max(1e-6f, distSq));
			const float3 L = toLight / dist;
			const float3 spotFwd = normalize(g_fwdSpotDirCone[si].xyz);
			const float cosOuter = g_fwdSpotDirCone[si].w;
			// Inner-cone cosine pulled from the parallel array. CPU clamps inner <= outer
			// so cosInner >= cosOuter; we still guard the smoothstep against equal cosines
			// to avoid a divide-by-zero shape at the boundary.
			const float cosInner = max(g_fwdSpotInnerCone[si].x, cosOuter + 1e-4f);
			const float cosAngle = dot(-L, spotFwd);
			const float coneAtten = smoothstep(cosOuter, cosInner, cosAngle);
			if (coneAtten <= 0.0f)
				continue;
			const float minDistSqr = 0.01f * 0.01f;
			float distanceFalloff = saturate(1.0f - pow(dist / radius, 4.0f));
			distanceFalloff *= distanceFalloff;
			const float atten = (distanceFalloff / max(distSq, minDistSqr)) * coneAtten;
			const float3 colour = g_fwdSpotColorStrength[si].rgb * g_fwdSpotColorStrength[si].w;
			accum += PBRDirectLight(worldPos, worldNormal, baseColour, metalness, roughness,
				L, colour, atten);
		}

		return accum;
	}

	// Cheap screen-space reflection used inline by the transparency PBR path. Marches a ray
	// derived from the surface's reflection vector against the opaque depth buffer and
	// samples the opaque beauty texture on hit. Returns black on miss (caller blends with
	// fallback / leaves base radiance intact). Roughness biases step count + early-out.
	bool TraceTransparentSSR(float3 surfaceWorldPos, float3 reflectDirWorld, float roughness, SamplerState samp,
		out float3 reflectedColour, out float hitConfidence)
	{
		reflectedColour = float3(0.0f, 0.0f, 0.0f);
		hitConfidence = 0.0f;

		// Skip for very rough surfaces — output is dominated by diffuse anyway and the trace
		// would just produce noise without temporal accumulation.
		if (roughness > 0.85f)
			return false;

		const int kMaxSteps = 48;
		const float kStrideWorld = 0.12f;       // world-space step length, scaled by distance below
		const float kThicknessWorld = 0.35f;    // depth-difference window for accepting a hit

		// Step length grows with distance from camera so distant rays don't take many steps.
		const float distFromEye = length(g_eyePos.xyz - surfaceWorldPos);
		const float strideWorld = kStrideWorld * max(0.5f, distFromEye * 0.08f);

		float3 rayPos = surfaceWorldPos + reflectDirWorld * (strideWorld * 0.5f);

		[loop]
		for (int step = 0; step < kMaxSteps; ++step)
		{
			rayPos += reflectDirWorld * strideWorld;

			// Project the ray sample into clip / screen space.
			const float4 clip = mul(float4(rayPos, 1.0f), g_viewProjectionMatrix);
			if (clip.w <= 0.0f)
				return false;
			const float2 ndc = clip.xy / clip.w;
			if (any(abs(ndc) > 1.0f))
				return false;

			const float2 uv = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);

			// Compare ray's view-space depth with the opaque scene at the same UV. The G-buffer
			// stores world-space-z in normal.w; ray's view depth is mul(rayPos, view).z negated.
			const float rayViewZ = -mul(float4(rayPos, 1.0f), g_viewMatrix).z;
			const float sceneViewZ = g_sceneNormalTex.SampleLevel(samp, uv, 0).w;

			// Skip the sky / very far depth.
			if (sceneViewZ <= 0.0f || sceneViewZ >= g_frustumDepths[3] * 0.999f)
				continue;

			const float dz = rayViewZ - sceneViewZ;
			if (dz > 0.0f && dz < kThicknessWorld)
			{
				reflectedColour = g_sceneColourTex.SampleLevel(samp, uv, 0).rgb;
				// Fade out near screen edges to hide the obvious missing-data band.
				const float2 edgeFade = smoothstep(0.0f, 0.1f, uv) * smoothstep(0.0f, 0.1f, 1.0f - uv);
				hitConfidence = saturate(edgeFade.x * edgeFade.y);
				return true;
			}
		}
		return false;
	}

	// Full forward lighting for a TRANSPARENT surface (glass, alpha-blended meshes):
	// sun with cascade shadows + ambient, every local light, and a Fresnel-weighted
	// reflection (screen-space march, falling back to the prefiltered sky). Shared by
	// DefaultPixel and the material-graph compiler's transparency phase so graph
	// glass and Default glass shade identically - the graph path used to be sun-only
	// with no shadows or reflections, which is why building windows read flat.
	//
	// Output is composed for STRAIGHT alpha blending (BlendState Transparency:
	// SRC_ALPHA / INV_SRC_ALPHA). Glass is not a fog-like blend: the body (tint,
	// diffuse, sun highlight) covers `opacity` of the background, but the
	// reflection REPLACES a Fresnel fraction of it on top. Plain blending
	// multiplied the reflection by the opacity too, so 10%-opaque windows kept
	// 10% of their reflection. Here
	//   alpha = opacity + (1 - opacity) * reflectCoverage
	//   rgb   = (body * opacity + reflection) / alpha
	// so the blend yields body*opacity + reflection + background*(1 - alpha).
	// (Under Additive blending rgb is not scaled by alpha and this over-brightens
	// - glass materials should use Transparency.) The caller applies emission
	// and atmosphere to rgb.
	float4 ShadeTransparentSurface(float3 positionWS, float3 worldNormal, float3 albedo,
		float metalness, float roughness, float opacity, SamplerState samp)
	{
		// Sun cascade shadows (Phase 2 slice 5). This argument was a
		// hardcoded 1.0f - transparent surfaces received NO shadows at
		// all, glass in a shadowed interior lit as if outdoors. Same
		// ShadowInput/bias formulation as the deferred pass, so the glass
		// and the wall behind it agree about where the shadow falls.
		// Gated: the cascades + b2 caster constants are only valid when
		// SceneRenderer bound them for this pass.
		// Cheap PCF, not full PCSS: glass needs "am I in shadow", not
		// contact hardening, and PCSS at g_shadowConfig.samples cost
		// ~4 ms on window-heavy views (measured 2026-07-30).
		float sunShadow = 1.0f;
		if (g_taaParams.z > 0.5f)
		{
			const float ndl = dot(worldNormal, normalize(g_shadowCasterLightDir.xyz));
			const float shadowBias = g_shadowConfig.biasMultiplier * (1.0f - ndl);
			sunShadow = CalculateShadowsCheapPCF(positionWS, g_cmpSampler, SHADOWMAPS, shadowBias);
		}

		// Sun (analytical). CalculatePBRSurface already includes a single ambient term and
		// lightning flash contribution — don't add ambient again below.
		const float4 sunLit = CalculatePBRSurface(
			metalness,
			roughness,
			worldNormal,
			positionWS,
			-normalize(g_lightDirection.xyz),
			getSunColour(),
			albedo,
			sunShadow,
			g_globalLight[0]);

		// Direct-only contributions for forward point + spot lights so ambient isn't
		// scaled by the light count.
		const float3 forwardDirect = AccumulateForwardLights_PBR(positionWS,
			worldNormal, albedo, metalness, roughness);

		// Screen-space reflection from the opaque scene snapshot captured before this pass.
		const float3 V = normalize(g_eyePos.xyz - positionWS);
		const float3 R = reflect(-V, worldNormal);
		const float NdotV = saturate(dot(worldNormal, V));
		const float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metalness);
		const float fresnel = pow(1.0f - NdotV, 5.0f);
		const float3 F = F0 + (float3(1.0f, 1.0f, 1.0f) - F0) * fresnel;

		float3 reflection = float3(0.0f, 0.0f, 0.0f);
		float reflectionWeight = 0.0f;
		float3 ssrColour = float3(0.0f, 0.0f, 0.0f);
		float ssrConfidence = 0.0f;
		const float glossiness = saturate(1.0f - roughness);

		if (TraceTransparentSSR(positionWS, R, roughness, samp, ssrColour, ssrConfidence))
		{
			// Gloss-only SSR (no blur), so fade out as roughness rises.
			reflectionWeight = ssrConfidence * glossiness;
			reflection = ssrColour;
		}

		// Environment fallback wherever the screen-space march found nothing.
		//
		// Without this, `reflection` stays BLACK on a miss - and for a window
		// the reflected ray usually leaves the screen on the first step, so it
		// misses almost always. The Fresnel term then multiplies black, and the
		// pane renders as a dark hole instead of glass. That is the "black
		// window panes" artifact; it was never the SSR march being wrong, just
		// nothing behind it.
		//
		// The prefiltered sky atlas is the same environment the deferred IBL
		// uses, so glass and opaque surfaces agree about what the sky looks
		// like. Weighted by (1 - ssrWeight) so a real screen-space hit always
		// wins - screen data is more accurate than a distant-environment
		// approximation when it exists.
		{
			const float3 envColour = SampleEnvAtlas(g_iblSkyEnvFwd, samp, R, roughness);
			// Downward rays would otherwise pick up horizon sky and light the
			// undersides of glass; the atlas carries no ground radiance.
			const float envHorizon = saturate(R.y * 3.0f + 0.35f);
			const float envWeight = (1.0f - reflectionWeight) * glossiness * envHorizon * g_glassEnvStrength;

			reflection = reflection * reflectionWeight + envColour * envWeight;
			reflectionWeight = saturate(reflectionWeight + envWeight);
			// `reflection` is now premultiplied by its weight, so undo that -
			// the composition below multiplies by reflectionWeight again.
			reflection = reflectionWeight > 1e-4f ? reflection / reflectionWeight : 0.0f.xxx;
		}

		// Sum: analytical sun (incl. ambient) + forward direct + Fresnel-weighted reflection.
		const float3 specularReflectionTerm = F * reflection * reflectionWeight;
		const float3 body = sunLit.rgb + forwardDirect;
		const float reflectCoverage = saturate(max(F.r, max(F.g, F.b)) * reflectionWeight);
		const float alpha = saturate(opacity + (1.0f - opacity) * reflectCoverage);
		const float3 rgb = alpha > 1e-4f ? (body * opacity + specularReflectionTerm) / alpha : 0.0f.xxx;
		return float4(rgb, alpha);
	}
#endif
}
