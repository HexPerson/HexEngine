"Requirements"
{
	// EMPTY on purpose (O1 modernisation). The legacy flags (GBuffer, Beauty)
	// made Scene::RenderInstance bind gbuffer t0-t4 + beauty t5 via the
	// implicit slot counter, pushing the material textures up to t6..t13 -
	// colliding with the modern transparent-pass binds (scene colour t10,
	// gbuffer normal t12 / position t13, sky atlas t14, sun cascades t15+,
	// SceneRenderer::RenderTransparent ~:5259-5309). With no requirements the
	// implicit counter starts at 0: material textures land at the STANDARD
	// t0..t7 slots (same as Default.shader) and the pass binds are read
	// directly at their explicit registers below.
}
"InputLayout"
{
	PosNormTanBinTex_INSTANCED
}
"VertexShaderIncludes"
{
	MeshCommon
	WaterCommon
}
"HullShaderIncludes"
{
	MeshCommon
	WaterCommon
}
"DomainShaderIncludes"
{
	MeshCommon
	WaterCommon
}
"PixelShaderIncludes"
{
	MeshCommon
	WaterCommon
	ShadowUtils
	Utils
	Atmosphere
	AtmospherePhysical
	PBRutils
	EnvMapCommon
}
"GlobalIncludes"
{
	Global
}
"VertexShader"
{
	// Tessellation VS (O7): no waves, no projection - transform each
	// control point to WORLD space and hand it on. The 17x17-vert sea
	// tiles have ~8 m triangles; the wave shape now comes from the
	// subdivided domain-shader evaluation instead of being carried by the
	// coarse grid.
	WaterCP ShaderMain(MeshVertexInput input, MeshInstanceData instance, uint instanceID : SV_INSTANCEID)
	{
		WaterCP o;
		input.position.w = 1.0f;

		o.worldPos  = mul(input.position, instance.world).xyz;
		o.worldPrev = mul(input.position, instance.worldPrev).xyz;

		// Bump advection via the CPU-INTEGRATED wind-scroll phase
		// (g_timeParams2.zw) - the stateless dir x g_time x rate form slews
		// during weather transitions; an integral cannot (see O5).
		o.texcoord = (input.texcoord - g_timeParams2.zw) * 1.4f;
		o.instanceID = instanceID + entityId;
		return o;
	}
}
"HullShader"
{
	// Distance LOD from the EDGE MIDPOINT in world space: a shared tile
	// edge computes identical factors on both sides regardless of which
	// tile draws it - that agreement is what keeps the 92-tile sea
	// watertight while it animates. Factor 16 over ~8 m triangles = ~0.5 m
	// segments near the camera, collapsing to the raw grid by ~165 m.
	float WaterTessFactor(float3 worldMid)
	{
		const float d = distance(worldMid, g_eyePos.xyz);
		return max(1.0f, lerp(16.0f, 1.0f, saturate((d - 15.0f) / 150.0f)));
	}

	WaterHSConst ConstantHS(InputPatch<WaterCP, 3> ip, uint pid : SV_PrimitiveID)
	{
		WaterHSConst o;
		const float3 c0 = ip[0].worldPos;
		const float3 c1 = ip[1].worldPos;
		const float3 c2 = ip[2].worldPos;
		// SV_TessFactor[i] governs the edge OPPOSITE control point i.
		o.edges[0] = WaterTessFactor((c1 + c2) * 0.5f);
		o.edges[1] = WaterTessFactor((c2 + c0) * 0.5f);
		o.edges[2] = WaterTessFactor((c0 + c1) * 0.5f);
		o.inside   = (o.edges[0] + o.edges[1] + o.edges[2]) / 3.0f;
		return o;
	}

	[domain("tri")]
	[partitioning("fractional_odd")]
	[outputtopology("triangle_cw")]
	[outputcontrolpoints(3)]
	[patchconstantfunc("ConstantHS")]
	WaterCP ShaderMain(InputPatch<WaterCP, 3> ip, uint i : SV_OutputControlPointID, uint pid : SV_PrimitiveID)
	{
		return ip[i];
	}
}
"DomainShader"
{
	// Subdivided wave evaluation (O7): EvalOcean at the interpolated world
	// position, at BOTH g_time (position/TBN/crest) and g_timePrev (motion
	// vectors) - exactly what the pre-tess VS did per grid vertex, but at
	// LOD'd density, so storm swell gets real shape and a broken
	// silhouette instead of riding 8 m triangles. LOW-frequency waves only
	// live here (the four-wave table); fine detail stays per-pixel bump -
	// the snow lesson: displacement must not contain frequencies the tess
	// density cannot represent.
	[domain("tri")]
	MeshPixelInput ShaderMain(WaterHSConst patchConst, float3 bary : SV_DomainLocation, const OutputPatch<WaterCP, 3> patch)
	{
		MeshPixelInput o = (MeshPixelInput)0;

		const float3 gridPos  = patch[0].worldPos  * bary.x + patch[1].worldPos  * bary.y + patch[2].worldPos  * bary.z;
		const float3 gridPrev = patch[0].worldPrev * bary.x + patch[1].worldPrev * bary.y + patch[2].worldPrev * bary.z;
		const float2 uv       = patch[0].texcoord  * bary.x + patch[1].texcoord  * bary.y + patch[2].texcoord  * bary.z;

		float2 windDir;
		float windAlign, ampScale;
		OceanWindParams(g_weatherSurface.windDirectionAndSpeed, g_oceanConfig2.x,
			windDir, windAlign, ampScale);

		float3 tangent, binormal;
		float crest01;
		float3 p = EvalOcean(gridPos, g_time, windDir, windAlign, ampScale, tangent, binormal, crest01);
		const float3 normal = normalize(cross(binormal, tangent));

		// Detail-spectrum displacement (realism pass): the longest waves of
		// the per-pixel detail set also move real geometry, band-limited
		// against the local tessellation segment (same LOD curve as the
		// hull shader: ~8 m grid triangles / factor). Normals for these
		// waves come from the PIXEL shader's analytic gradient, so the
		// interpolated TBN stays the four-wave swell basis.
		const float detailSteep = OceanDetailSteepness(ampScale);
		// Shared helper: OceanSurfaceOffset (and its CPU mirror) must apply
		// the identical band-limit or the surface query disagrees with the
		// geometry it is querying.
		const float segmentLength = OceanTessSegmentLength(distance(gridPos, g_eyePos.xyz));
		p.y += OceanDetailDisplacement(gridPos.xz, g_time, detailSteep, segmentLength);

		// Storms foam harder: scale the crest factor the PS thresholds.
		crest01 *= saturate(0.35f + ampScale);

		o.positionWS = float4(p, 1.0f);
		o.position = mul(float4(p, 1.0f), g_viewProjectionMatrix);

		// Motion vectors: same evaluation at g_timePrev; the tiles are
		// static, so the displacement delta is the whole velocity.
		{
			float3 tPrev, bPrev;
			float cPrev;
			float3 pPrev = EvalOcean(gridPrev, g_timePrev, windDir, windAlign, ampScale, tPrev, bPrev, cPrev);
			pPrev.y += OceanDetailDisplacement(gridPrev.xz, g_timePrev, detailSteep, segmentLength);
			o.previousPositionUnjittered = mul(float4(pPrev, 1.0f), g_viewProjectionMatrixPrev);
		}
		o.currentPositionUnjittered = o.position;

		// TAA jitter, matching the standard VS.
		o.position.xy += g_jitterOffsets * o.position.w;

		o.texcoord = uv;
		o.normal = normal;
		o.tangent = tangent;
		o.binormal = binormal;
		o.viewDirection = float4(normalize(g_eyePos.xyz - p), 0.0f);
		// colour.x = crest factor for foam (O4).
		o.colour = float4(crest01, 0.0f, 0.0f, 1.0f);
		o.instanceID = patch[0].instanceID;
		o.cullDistance = 1.0f;
		return o;
	}
}
"PixelShader"
{
	// Material textures at the STANDARD implicit slots (empty Requirements ->
	// the counter starts at 0; same array order every material uses:
	// Albedo/Normal/Roughness/Metallic/Height/Emission/Opacity/AO).
	Texture2D g_albedoMap : register(t0);
	Texture2D g_normalMap : register(t1);

	// Transparent-pass binds (SceneRenderer::RenderTransparent ~:5259-5309;
	// same registers DefaultPixel's transparency path reads).
	// Scene colour: the pre-transparency opaque beauty snapshot.
	Texture2D g_sceneColourTex : register(t10);
	// Opaque gbuffer normal: xyz = world normal, w = view depth (-1 = sky).
	Texture2D g_sceneNormalTex : register(t12);
	// Opaque gbuffer position: xyz = world position.
	Texture2D g_scenePositionTex : register(t13);

	// Prefiltered sky environment atlas - the same environment the deferred
	// IBL and glass use, so the sea and every other surface agree about what
	// the sky looks like (a storm sky reflects as overcast, not clear blue).
	Texture2D g_iblSkyEnvFwd : register(t14);
	// Per-fragment atmosphere (see TransparentAtmosphere.shader).
	Texture3D g_transFogVolume : register(t24);
	Texture3D g_transApVolume  : register(t21);
	SamplerState g_transLinearSampler : register(s4);

	// Sun cascade shadow maps - bound pass-wide for transparents at t15+
	// (SceneRenderer::RenderTransparent), same slots DefaultPixel uses.
	SHADOWMAPS_RESOURCE(15);

	SamplerState g_TexSamplerAniso : register(s0);
	SamplerComparisonState g_cmpSampler : register(s1);
	SamplerState g_TexSamplerPoint : register(s2);

	// Inline screen-space reflection for water (O3) - the DefaultPixel
	// transparency pattern (third copy; dedup across DefaultPixel /
	// DefaultAnimated / here is a tracked follow-up). Replaces the legacy
	// 24-step self-contained march this shader carried since before the SSR
	// stack existed. Water-specific tuning: a wider thickness window (the
	// reflecting surface is a DISPLACED wavy plane, so ray/depth
	// disagreements up to a wave amplitude are normal, not misses).
	bool TraceWaterSSR(float3 surfaceWorldPos, float3 reflectDirWorld,
		out float3 reflectedColour, out float hitConfidence)
	{
		reflectedColour = float3(0.0f, 0.0f, 0.0f);
		hitConfidence = 0.0f;

		const int kMaxSteps = 48;
		const float kStrideWorld = 0.12f;    // world-space step length, scaled by distance below
		const float kThicknessWorld = 0.6f;  // wider than glass's 0.35 - see header comment

		// Step length grows with distance from camera so distant rays don't take many steps.
		const float distFromEye = length(g_eyePos.xyz - surfaceWorldPos);
		const float strideWorld = kStrideWorld * max(0.5f, distFromEye * 0.08f);

		// The acceptance window must scale WITH the stride: a fixed 0.6 m
		// window under a ~1 m distant stride steps clean over thin geometry,
		// so one pixel hits (dark cliff) and its neighbour misses (bright
		// env) - the per-pixel speckle in distant reflections.
		const float thickness = max(kThicknessWorld, strideWorld * 1.5f);

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

			// Compare ray's view-space depth with the opaque scene at the same
			// UV (normal.w carries view depth; -1/far = sky).
			const float rayViewZ = -mul(float4(rayPos, 1.0f), g_viewMatrix).z;
			const float sceneViewZ = g_sceneNormalTex.SampleLevel(g_TexSamplerPoint, uv, 0).w;

			// Skip the sky / very far depth.
			if (sceneViewZ <= 0.0f || sceneViewZ >= g_frustumDepths[3] * 0.999f)
				continue;

			const float dz = rayViewZ - sceneViewZ;
			if (dz > 0.0f && dz < thickness)
			{
				reflectedColour = g_sceneColourTex.SampleLevel(g_TexSamplerPoint, uv, 0).rgb;
				// Fade out near screen edges to hide the missing-data band.
				const float2 edgeFade = smoothstep(0.0f, 0.1f, uv) * smoothstep(0.0f, 0.1f, 1.0f - uv);
				hitConfidence = saturate(edgeFade.x * edgeFade.y);
				return true;
			}
		}
		return false;
	}

	// FROM BELOW: what the refracted ray sees of the world above the water.
	// The opaque scene colour is lit before any water draws, so it holds the
	// un-refracted above-water world across the whole screen - including the
	// part of it the total-internal-reflection zone hides. Marching the
	// TRANSMITTED ray through that depth buffer is what bends a pier or a hull
	// into Snell's window instead of leaving it a disc of pure sky.
	// Strides grow geometrically (0.25 m -> ~330 m in 24 steps): the ray leaves
	// the surface heading for the horizon, so what it hits is either right here
	// (a hull, a pier) or far away (the shore), and a fixed stride can't do both.
	// Misses - off-screen, sky, or nothing in the way - fall back to the env
	// atlas at the call site, which is also what owns the sky itself: the dome
	// in scene colour carries the raw HDR sun disc and no clouds.
	bool TraceRefractedScene(float3 surfaceWorldPos, float3 refractDirWorld, float2 pixelXY,
		out float3 sceneColour, out float hitConfidence)
	{
		sceneColour = float3(0.0f, 0.0f, 0.0f);
		hitConfidence = 0.0f;

		const int kMaxSteps = 24;
		const float kFirstStride = 0.25f;
		const float kStrideGrowth = 1.35f;

		// Interleaved gradient noise on the start offset turns the geometric
		// strides' depth banding into grain the temporal resolve eats.
		const float jitter = frac(52.9829189f * frac(dot(pixelXY, float2(0.06711056f, 0.00583715f))));

		float stride = kFirstStride;
		float travelled = kFirstStride * jitter;
		float prevTravelled = 0.0f;

		[loop]
		for (int step = 0; step < kMaxSteps; ++step)
		{
			prevTravelled = travelled;
			travelled += stride;
			const float3 rayPos = surfaceWorldPos + refractDirWorld * travelled;

			const float4 clip = mul(float4(rayPos, 1.0f), g_viewProjectionMatrix);
			if (clip.w <= 0.0f)
				return false;
			const float2 ndc = clip.xy / clip.w;
			if (any(abs(ndc) > 1.0f))
				return false;
			const float2 uv = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);

			const float rayViewZ = -mul(float4(rayPos, 1.0f), g_viewMatrix).z;
			const float sceneViewZ = g_sceneNormalTex.SampleLevel(g_TexSamplerPoint, uv, 0).w;

			const bool sceneIsGeometry = sceneViewZ > 0.0f && sceneViewZ < g_frustumDepths[3] * 0.999f;
			const float dz = rayViewZ - sceneViewZ;
			if (sceneIsGeometry && dz > 0.0f && dz < stride * 2.0f + 0.5f)
			{
				// Bisect between the last clear sample and this one: the late
				// strides are tens of metres long, and an unrefined hit slides
				// the image around as the waves move the ray.
				float lo = prevTravelled;
				float hi = travelled;
				float2 hitUv = uv;
				for (int refine = 0; refine < 5; ++refine)
				{
					const float mid = 0.5f * (lo + hi);
					const float3 midPos = surfaceWorldPos + refractDirWorld * mid;
					const float4 midClip = mul(float4(midPos, 1.0f), g_viewProjectionMatrix);
					const float2 midNdc = midClip.xy / max(midClip.w, 1e-4f);
					const float2 midUv = float2(midNdc.x * 0.5f + 0.5f, 0.5f - midNdc.y * 0.5f);
					const float midRayZ = -mul(float4(midPos, 1.0f), g_viewMatrix).z;
					const float midSceneZ = g_sceneNormalTex.SampleLevel(g_TexSamplerPoint, midUv, 0).w;
					if (midSceneZ > 0.0f && midRayZ > midSceneZ)
					{
						hi = mid;
						hitUv = midUv;
					}
					else
					{
						lo = mid;
					}
				}

				// The ray climbs out of the water, so a genuine hit is above
				// it. Anything below the waterline is the depth test catching
				// submerged geometry that merely lines up on screen.
				const float3 hitPosWS = g_scenePositionTex.SampleLevel(g_TexSamplerPoint, hitUv, 0).xyz;
				if (hitPosWS.y < g_oceanConfig3.x - 0.25f)
					return false;

				sceneColour = g_sceneColourTex.SampleLevel(g_TexSamplerPoint, hitUv, 0).rgb;
				const float2 edgeFade = smoothstep(0.0f, 0.08f, hitUv) * smoothstep(0.0f, 0.08f, 1.0f - hitUv);
				hitConfidence = saturate(edgeFade.x * edgeFade.y);
				return true;
			}

			stride *= kStrideGrowth;
		}
		return false;
	}

	// Screen-space refraction: offset the scene-colour lookup along the
	// refracted direction, depth-rejected so geometry NEARER than the water
	// surface never smears into the refraction. Rebuilt properly in O4.
	float4 GetWorldColour(float3 eyeDir, inout float2 screenPos, float3 worldNormal, float4 originalWorldDiffuse, float3 pixelPos, float pixelDepth, float offsetScale)
	{
		float eta = 0.75f;

		float2 origScreenPos = screenPos;

		float3 refractedNormal = refract(eyeDir, -(worldNormal), eta);

		// Project the refracted DIRECTION and use it as a screen-space UV
		// offset, SCALED by the water-column depth (offsetScale): centimetres
		// of water over a shore stone barely displace it, a deep column bends
		// hard. The unscaled version smeared the shoreline.
		float4 jitterNormal = float4(refractedNormal, 0.0f);
		jitterNormal = mul(jitterNormal, g_viewProjectionMatrix);

		const float jitterAmmount = 0.018f;

		jitterNormal = jitterNormal * (jitterAmmount * offsetScale);

		screenPos = screenPos + jitterNormal.xy;

		if (screenPos.x < 0.0f || screenPos.x > 1.0f || screenPos.y < 0.0f || screenPos.y > 1.0f)
		{
			screenPos = origScreenPos;
			return originalWorldDiffuse;
		}

		float fragDepth = g_sceneNormalTex.Sample(g_TexSamplerPoint, screenPos).w;

		if (fragDepth < pixelDepth)
		{
			screenPos = origScreenPos;
			return originalWorldDiffuse;
		}

		// resample the scene at the refracted position
		float4 jitterDiffuse = g_sceneColourTex.Sample(g_TexSamplerPoint, screenPos);
		jitterDiffuse.a = 1.0f;

		return jitterDiffuse;
	}

	// Tangent-space normal mapping with the CORRECT [0,1] -> [-1,1] unpack.
	// The legacy version had the expansion commented out, so the raw texel
	// fed the TBN mix and the resulting basis was biased toward +tangent
	// +binormal - water bump normals have been mathematically wrong for
	// years (visibly: lighting that never quite tracked the waves).
	float3 ANM(float3 worldNormal, float3 tangent, float3 binormal, Texture2D normalTex, SamplerState samp, float2 texcoord, float strength)
	{
		float3 bumpMap = normalTex.Sample(samp, texcoord).xyz;

		// Expand the range of the normal value from (0, +1) to (-1, +1).
		bumpMap = (bumpMap * 2.0f) - 1.0f;

		// Strength scales the tangent-plane deflection only. NOTE (user-
		// found): the years-broken unpack was accidentally ATTENUATING the
		// map - raw [0,1] texels perturb at half amplitude around a constant
		// bias - so fixing it unleashed the texture at full strength and the
		// per-texel normal scatter shredded the SSR mirror image. The unpack
		// is correct; the amplitude needed an explicit dial (and the
		// reflection ray now uses a mostly-Gerstner normal besides).
		bumpMap.xy *= strength;

		float3 bumpNormal =
			(bumpMap.x * tangent) +
			(bumpMap.y * binormal) +
			(bumpMap.z * worldNormal);

		return normalize(bumpNormal);
	}

	// P4.4: the transparent pass binds the gbuffer velocity RT at slot 4
	// (matching GBufferOut's SV_TARGET4, so the Default-family shaders need
	// no change). Water was the one transparent-phase shader with a single
	// SV_Target - its Gerstner motion computed in the DS never landed
	// anywhere, which is why waves ghosted under TAA and can't motion-blur.
	#include "TransparentAtmosphere.shader"

	struct WaterOut
	{
		float4 colour   : SV_Target0;
		float2 velocity : SV_TARGET4;
	};

	// Cellular (Worley F1) helper for the foam lace. Real foam is not a
	// smooth field - it is a WEB: dark bubble holes ringed by bright
	// filaments. F1 distance to animated feature points gives exactly that
	// topology; the smoothstep band in the caller turns cell interiors into
	// holes and cell borders into lace.
	float2 FoamHash22(float2 p)
	{
		float3 q = frac(float3(p.xyx) * float3(0.1031f, 0.1030f, 0.0973f));
		q += dot(q, q.yzx + 33.33f);
		return frac((q.xx + q.yz) * q.zy);
	}

	float FoamWorleyF1(float2 p, float t)
	{
		const float2 cell = floor(p);
		const float2 f = frac(p);
		float f1 = 8.0f;
		[unroll]
		for (int y = -1; y <= 1; ++y)
		{
			[unroll]
			for (int x = -1; x <= 1; ++x)
			{
				const float2 o = FoamHash22(cell + float2((float)x, (float)y));
				// Feature points orbit their cell at a fixed slow rate -
				// organic churn with bounded motion (no teleporting when
				// parameters change; see the wind/phase rule in the VS).
				const float2 wobble = 0.5f.xx + 0.38f * sin(t + 6.2831853f * o);
				const float2 d = float2((float)x, (float)y) + wobble - f;
				f1 = min(f1, dot(d, d));
			}
		}
		return sqrt(f1);
	}

	WaterOut ShaderMain(MeshPixelInput input)
	{
		float4 specular = float4(0, 0, 0, 1);

		float3 eyeVector = normalize(g_eyePos.xyz - input.positionWS.xyz);
		const float cameraDistance = length(input.positionWS.xyz - g_eyePos.xyz);
		const float nearQualityDistance = g_oceanConfig.reflectionNearDistance;
		const float midQualityDistance = lerp(g_oceanConfig.reflectionNearDistance, g_oceanConfig.reflectionFarDistance, 0.6f);
		const float farQualityDistance = max(g_oceanConfig.reflectionFarDistance, midQualityDistance + 1.0f);
		const float refractionQualityWeight = 1.0f - saturate((cameraDistance - nearQualityDistance) / max(midQualityDistance - nearQualityDistance, 1.0f));
		const float ssrQualityWeight = 1.0f - saturate((cameraDistance - nearQualityDistance) / max(farQualityDistance - nearQualityDistance, 1.0f));
		const float distantNormalFade = ssrQualityWeight;

		float3 worldNormal = normalize(input.normal.xyz);
		// The four-wave swell alone - the from-below interface needs to tell the
		// swell's tilt (kept as is) from the wavelets' (exaggerated).
		const float3 swellNormal = worldNormal;

		// DETAIL SPECTRUM (realism pass). The interpolated normal only knows
		// the four swell waves; everything that makes a sea read as water -
		// the wavelet faces, each with its own Fresnel and glint - comes from
		// the analytic 20-wave gradient evaluated HERE, band-limited to the
		// pixel footprint (derivatives taken before any branching). Whatever
		// the footprint filtered out comes back as lostVariance -> roughness.
		//
		// The footprint is ANALYTIC (distance x pixel angle / grazing cosine),
		// NOT ddx/ddy(positionWS): interpolated positions have derivatives that
		// are constant per triangle and jump at triangle edges, so a
		// derivative-based footprint stepped the band-limit weights across
		// every tessellated triangle and drew the mesh as a faint lattice
		// over the sea (first capture of this pass). _22 of the projection is
		// 1/tan(fovY/2). The grazing stretch uses cos^0.7 rather than 1/cos:
		// the footprint is anisotropic (long only ALONG the view), and the
		// full 1/cos would over-blur the waves running across it.
		const float pixelAngle = 2.0f / (max(abs(g_projectionMatrix._22), 1e-3f) * (float)g_screenHeight);
		const float grazingCos = max(abs(eyeVector.y), 0.03f);
		const float pixelFootprint = max(cameraDistance * pixelAngle / pow(grazingCos, 0.7f), 1e-3f);

		float2 detailWindDir;
		float detailWindAlign, detailAmpScale;
		OceanWindParams(g_weatherSurface.windDirectionAndSpeed, g_oceanConfig2.x,
			detailWindDir, detailWindAlign, detailAmpScale);

		float2 detailSlope;
		float detailHeight01, lostVariance, totalVariance;
		OceanDetailWaves(input.positionWS.xz, g_time, OceanDetailSteepness(detailAmpScale), pixelFootprint,
			detailSlope, detailHeight01, lostVariance, totalVariance);

		// The swell normal is near-vertical, so the height-field gradient
		// composes by simple subtraction in XZ.
		worldNormal = normalize(float3(worldNormal.x - detailSlope.x, worldNormal.y, worldNormal.z - detailSlope.y));

		float3 refractionNormal = -worldNormal;

		// "original" = the resolved WAVE normal (swell + analytic detail),
		// before the normal-map micro layer. Fresnel and reflection key off
		// this, so reflectance varies facet by facet.
		float3 originalWorldNormal = worldNormal;

		float3 lightDir = -normalize(g_lightDirection.xyz);

		float4 worldViewPosition = mul(input.positionWS, g_viewMatrix);
		float pixelDepth = -worldViewPosition.z;

		float2 screenPos = float2(input.position.x / (float)g_screenWidth, input.position.y / (float)g_screenHeight);

		// The pre-transparency opaque scene colour at this pixel.
		float4 worldDiffuse = g_sceneColourTex.Sample(g_TexSamplerPoint, screenPos);
		worldDiffuse.a = 1.0f;

		// make a copy, we might need this again
		float4 originalWorldDiffuse = worldDiffuse;

		// BUMP MAPPING - fades out with distance (far water keeps the smooth
		// Gerstner normal; per-texel detail at the horizon just aliases).
		if (distantNormalFade > 0.001f)
		{
			// r_oceanBumpStrength: live normal-map deflection dial.
			// Micro layer on top of the analytic waves: two samples at unrelated
			// scales/orientations so the tile never reads as a repeat.
			float3 bumpNormal = ANM(worldNormal, input.tangent, input.binormal, g_normalMap, g_TexSamplerAniso, input.texcoord, g_oceanConfig2.w);
			const float3 bumpNormal2 = ANM(worldNormal, input.tangent, input.binormal, g_normalMap, g_TexSamplerAniso,
				// 31 deg, not 90: two axis-aligned tilings of the same map form a grid.
				float2(input.texcoord.x * 0.857f - input.texcoord.y * 0.515f, input.texcoord.x * 0.515f + input.texcoord.y * 0.857f) * 0.37f + float2(0.173f, 0.619f), g_oceanConfig2.w * 0.8f);
			bumpNormal = normalize(bumpNormal + bumpNormal2 - worldNormal);
			bumpNormal = normalize(lerp(worldNormal, bumpNormal, distantNormalFade));

			refractionNormal = bumpNormal;
			worldNormal = bumpNormal;
		}

		// Rain ripples (O6): impact rings dimple the surface while
		// precipitation falls. Gated out under snow (blizzard flakes don't
		// ring like raindrops). No shelter sampling - the shelter map isn't
		// bound in the transparent pass (Mesh.shader has the same
		// constraint), and open water is rarely sheltered.
		const float rainAmount = saturate(g_weatherSurface.precipitationIntensity)
			* saturate(1.0f - g_weatherSurface.snowCoverage * 3.0f);
		if (rainAmount > 0.001f)
		{
			worldNormal = ApplyRainRipples(worldNormal, input.positionWS.xyz, g_time, rainAmount);
			refractionNormal = worldNormal;
		}

		// Sea state for SHADING (the VS already couples wave geometry to this).
		// A storm sea must stay legible through CONTRAST, not mirror
		// reflection: in a blizzard the env atlas and the fog converge to the
		// same grey, so reflected-minus-body goes to ~zero and untreated water
		// simply vanishes into the weather (user screenshot). Wind therefore
		// drives: darker slate body, boosted whitecaps, rougher (softer,
		// broader) glints and blurrier env reflection.
		const float seaState = saturate(g_weatherSurface.windDirectionAndSpeed.w / 30.0f);

		float4 normalAndDepth = g_sceneNormalTex.Sample(g_TexSamplerPoint, screenPos);
		float worldDepth = normalAndDepth.w;

		if (refractionQualityWeight > 0.001f && (worldDepth >= pixelDepth || worldDepth == -1.0f))
		{
			// Pre-refraction column estimate just for the offset scale (the
			// accurate metre-based column is computed below at the final UV).
			const float preColumn = (worldDepth == -1.0f) ? 100.0f : (worldDepth - pixelDepth);
			const float refractOffsetScale = saturate(preColumn * 0.6f);

			float4 refractedWorldDiffuse = GetWorldColour(-eyeVector, screenPos, refractionNormal, worldDiffuse, input.positionWS.xyz, pixelDepth, refractOffsetScale);
			worldDiffuse = lerp(originalWorldDiffuse, refractedWorldDiffuse, refractionQualityWeight);

			// re-read depth at the refracted position so the shore/absorption
			// terms below use the surface the refraction actually shows
			normalAndDepth = g_sceneNormalTex.Sample(g_TexSamplerPoint, screenPos);
			worldDepth = normalAndDepth.w;
		}

		// Sun cascade shadows (O2): water was never shadowed - a dock's shadow
		// stopped dead at the waterline while the sea sparkled underneath it.
		// Same cheap-PCF + gate as DefaultPixel's transparency path (the
		// cascades + b2 caster constants are only valid when the pass bound
		// them; g_taaParams.z carries that).
		float sunShadow = 1.0f;
		if (g_taaParams.z > 0.5f)
		{
			const float ndl = dot(worldNormal, normalize(g_shadowCasterLightDir.xyz));
			const float shadowBias = g_shadowConfig.biasMultiplier * (1.0f - ndl);
			sunShadow = CalculateShadowsCheapPCF(input.positionWS.xyz, g_cmpSampler, SHADOWMAPS, shadowBias);
		}

		float lightIntensity = dot(worldNormal, lightDir) * g_globalLight[0] * sunShadow;

		// SURFACE ROUGHNESS from unresolved wave slopes. Beckmann/GGX alpha^2
		// ~ 2 x slope variance, so the ripples the pixel footprint filtered
		// out return as microfacet roughness: near water stays glassy with
		// razor glints on resolved wavelets, the far sea broadens into the
		// soft glitter path and blurred sky band of the reference.
		const float waterBasePerceptual = lerp(0.05f, 0.30f, seaState);
		const float waterAlpha = sqrt(saturate(pow(waterBasePerceptual, 4.0f) + 2.0f * lostVariance));
		const float waterPerceptualFromVariance = sqrt(waterAlpha);

		if (lightIntensity > 0.0f)
		{
			const float waterMetallic = 0.0f;
			const float3 viewDir = normalize(g_eyePos.xyz - input.positionWS.xyz);
			const float3 halfVector = normalize(lightDir + viewDir);
			// Full detail normal: the glitter IS the wavelet faces.
			const float3 specularNormal = worldNormal;
			const float NdotL = clamp(dot(specularNormal, lightDir), 0.001f, 1.0f);
			const float NdotV = abs(dot(specularNormal, viewDir)) + 0.001f;
			const float NdotH = saturate(dot(specularNormal, halfVector));
			const float VdotH = saturate(dot(viewDir, halfVector));
			float waterPerceptualRoughness = ApplySpecularAntiAliasing(specularNormal, waterPerceptualFromVariance);
			const float alphaRoughness = waterPerceptualRoughness * waterPerceptualRoughness;
			// Water F0 = 0.02 (IOR 1.33), not the dielectric default 0.04.
			const float3 specularColor = lerp(float3(0.02f, 0.02f, 0.02f), float3(1.0f, 1.0f, 1.0f), waterMetallic);
			const float reflectance = max(max(specularColor.r, specularColor.g), specularColor.b);
			// Water reaches full reflectance at grazing (the x25 heuristic would
			// stop at 0.5 for F0 = 0.02).
			const float reflectance90 = 1.0f;
			const float3 F = specularReflection(specularColor, float3(1.0f, 1.0f, 1.0f) * reflectance90, VdotH);
			const float G = geometricOcclusion(NdotL, NdotV, alphaRoughness);
			const float D = microfacetDistribution(NdotH, alphaRoughness);
			const float3 directSpecular = F * G * D / max(4.0f * NdotL * NdotV, 0.001f);
			// Sun radiance in the same units the glass path uses
			// (getSunColour() x g_globalLight[0], Frostbite convention) instead
			// of the ad-hoc ComputePhysicalSunColour x5.75 boost that was
			// compensating for the old LDR clamp. Shadowed like the body term.
			// Recolour the glint with the sun DISC's actual hue - the sky
			// sampled in the sun's direction - instead of getSunColour(), which
			// Reinhard-tonemaps toward white and desaturates the warm sunset
			// sun, so the water glint no longer matched the disc it reflects.
			// getSunColour()'s LUMINANCE is kept, so the (already-tuned) glint
			// strength is unchanged; only the colour now tracks the disc.
			const float3 sunLumaWeights = float3(0.2126f, 0.7152f, 0.0722f);
			const float3 skySun = SampleEnvAtlas(g_iblSkyEnvFwd, g_TexSamplerAniso, lightDir, 0.0f);
			const float skySunLuma = max(dot(skySun, sunLumaWeights), 1e-4f);
			const float3 sunRadiance = getSunColour() * g_globalLight[0];
			const float3 sunTinted = (skySun / skySunLuma) * dot(sunRadiance, sunLumaWeights);
			float3 sunSpecular = sunTinted * (NdotL * directSpecular) * sunShadow;
			// Physical cap. A specular reflection of the sun can be at most its
			// Fresnel-weighted radiance - a mirror cannot be brighter than what
			// it reflects. GGX for near-mirror water spikes far past that at the
			// exact reflection angle because a directional sun is treated as a
			// zero-size delta (no ~0.5 degree solar disc), which is why the
			// water's sun glint was outshining the sun disc itself - and it is
			// then double-counted against the env/SSR reflection that already
			// contains the sky's sun. Clamp the analytic highlight to F x the
			// sun's radiance so the reflection tops out at the sun, not above it.
			const float fresnelPeak = max(max(F.r, F.g), F.b);
			sunSpecular = min(sunSpecular, sunTinted * (fresnelPeak * sunShadow));
			specular = float4(sunSpecular, 1.0f);
		}

		// Water column (O4): METRES of water along the view path, from the
		// opaque world position behind the surface. The legacy terms
		// normalised the view-depth difference by the FAR PLANE, so every
		// absorption knob was scene-scale dependent and the first metre of
		// water - where all the shore detail lives - occupied a sliver of the
		// parameter range.
		const float3 scenePosWS = g_scenePositionTex.Sample(g_TexSamplerPoint, screenPos).xyz;
		const float columnDepth = (worldDepth <= 0.0f)
			? 500.0f
			: max(distance(scenePosWS, input.positionWS.xyz), 0.0f);

		const float fresnelPow = g_oceanConfig.fresnelPow;

		// Beer-Lambert absorption per METRE. reflection_pad0 (per-scene)
		// overrides when set (> 0); otherwise the live r_oceanAbsorption cvar.
		const float absorbK = g_oceanConfig.reflection_pad0 > 0.0f ? g_oceanConfig.reflection_pad0 : max(g_oceanConfig2.z, 0.005f);
		// SPECTRAL absorption. Water eats red within the first couple of
		// metres, green survives several, blue longest - which is why a sand
		// bottom goes turquoise then navy with depth instead of fading to
		// grey. The scalar exp() this replaces dimmed all channels equally:
		// the see-through read as a tinted glass sheet over the beach. The
		// per-channel weights average ~1.2, so r_oceanAbsorption keeps its
		// overall meaning.
		const float3 transmission = exp(-columnDepth * absorbK * float3(2.30f, 0.80f, 0.55f));

		// CONTROL SEPARATION (user-clarified semantics):
		//  - shoreFadeStrength: how fast DEPTH fades shallowColour->deepColour
		//  - absorption (pad0): how fast the refracted scene stops showing
		//    through (transmission above)
		//  - Fresnel ALONE decides reflectance - depth plays no part in it
		const float colourFade = 1.0f - exp(-columnDepth * g_oceanConfig.shoreFadeStrength / 30.0f);
		float4 fadeColour = lerp(g_oceanConfig.shallowColour, g_oceanConfig.deepColour, colourFade);
		// Storm seas read DARK SLATE - more absorption, less back-scatter.
		// This is also what keeps the water visible in a blizzard: the body
		// separates from the fog instead of matching it.
		fadeColour.rgb *= (1.0f - 0.45f * seaState);

		// SCHLICK Fresnel, not the legacy 1-cos^pow: that curve sat near 0.98
		// at ordinary viewing angles, so the exponent shifted the WHOLE sea's
		// reflectivity. Real water reflects ~2% at normal incidence and only
		// approaches a mirror toward grazing - which also means you can see
		// INTO the water near the camera. fresnelPow shapes the grazing rise
		// (5 = physical; lower = reflectivity comes in earlier).
		//
		// Evaluated on the per-pixel WAVE normal, so every wavelet face gets
		// its own reflectance: faces tilted toward the eye go dark (you see
		// into the water), faces tilted away go bright with sky. That facet
		// contrast is the texture of the reference sea. The incidence cosine
		// is floored by the RMS wave slope: a rough sea never presents a
		// perfectly grazing facet, so horizon reflectance tops out around
		// 0.5-0.65 instead of a chrome 1.0 (measured sea-surface albedo).
		const float rmsSlope = sqrt(max(totalVariance, 0.0f));
		const float fresnelCos = max(saturate(dot(eyeVector, originalWorldNormal)), 0.5f * rmsSlope);
		float fresnel = 0.02f + 0.98f * pow(1.0f - fresnelCos, max(fresnelPow, 0.5f));

		// Procedural foam (O4): crest foam where the waves peak (VS crest
		// interpolant) + a shore band where the column is centimetres deep.
		// No foam textures exist in the project - two octaves of ValueNoise3
		// shape both, advected slowly so the pattern churns. reflection_pad1
		// scales overall coverage (> 0 to override).
		float foam = 0.0f;
		{
			// Per-scene pad overrides when set; otherwise the live r_oceanFoam cvar.
			const float foamScale = g_oceanConfig.reflection_pad1 > 0.0f ? g_oceanConfig.reflection_pad1 : g_oceanConfig2.y;
			// SLOW noise churn. The first version advected at 0.22 and put a
			// hard smoothstep threshold on the crest factor - which
			// oscillates at wave-phase speed - so foam snapped on/off as each
			// crest swept past the threshold and the sea strobed ("looks
			// like lightning"). Continuous power curves + slow erosion make
			// foam wax and wane with the swell instead of flashing.
			const float3 np = float3(input.positionWS.x * 0.35f, g_time * 0.06f, input.positionWS.z * 0.35f);
			const float n = ValueNoise3(np) * 0.65f + ValueNoise3(np * 3.1f + float3(0.0f, g_time * 0.03f, 0.0f)) * 0.35f;

			// Crests: continuous response (no threshold to flash across),
			// noise shaping the coverage into streaks. The exponent RELAXES
			// with sea state - a storm sea whitecaps far below the theoretical
			// max crest, a calm sea only foams at true peaks.
			const float crest = saturate(input.colour.x);
			const float crestFoam = pow(crest, lerp(3.0f, 1.6f, seaState)) * (0.35f + 0.65f * n);

			// Shore: strongest at zero depth, fading over the first ~1.5 m,
			// continuous curve, noise-broken.
			const float shoreBand = saturate(1.0f - columnDepth / 1.5f);
			const float shoreFoam = shoreBand * shoreBand * (0.45f + 0.7f * n);

			// Whitecap coverage climbs with the wind - white water on the
			// darkened storm body is what keeps the sea legible when the sky,
			// fog and reflection all converge to grey.
			const float foamMask = saturate((crestFoam + shoreFoam) * foamScale * (1.0f + seaState));

			// FOAM STRUCTURE. The mask above says WHERE foam lives; the
			// smooth noise it used to output directly is why foam read as
			// soft mathematical blobs. Two scales of cellular lace (dark
			// bubble holes, bright filament web) + a micro grain give it the
			// texture of white water, and coverage-driven EROSION does the
			// rest: patch cores fill dense while edges dissolve into wisps of
			// surviving filament. Drift rates are fixed (not wind-coupled) so
			// weather changes never teleport the pattern.
			// Advect the lace with the CPU-INTEGRATED wind scroll
			// (g_timeParams2.zw - the same integral the bump layer uses), so
			// foam travels WITH the water instead of sliding across it. The
			// 64.0 factor converts the UV-space integral to world metres at
			// the same effective speed as the bump advection. Weather changes
			// bend this motion (integral property), never teleport it. The
			// accum wraps every ~20+ minutes of runtime, which reads as a
			// single churn event in an already-chaotic pattern - accepted.
			const float2 scrollWorld = g_timeParams2.zw * 64.0f;
			const float2 wp = input.positionWS.xz - scrollWorld;
			const float coarseF1 = FoamWorleyF1(wp * 0.55f, g_time * 0.35f);
			const float fineF1 = FoamWorleyF1(wp * 1.9f + 37.7f.xx, g_time * 0.5f);
			const float laceCoarse = smoothstep(0.18f, 0.80f, coarseF1);
			const float laceFine = smoothstep(0.12f, 0.85f, fineF1);
			float lace = laceCoarse * 0.62f + laceFine * 0.38f;
			// Micro grain: fine unresolved bubbles shimmering inside the web.
			lace *= 0.86f + 0.28f * ValueNoise3(float3(wp.x * 6.1f, g_time * 0.4f, wp.y * 6.1f));

			// Dissolve erosion: the mask sets a threshold the lace must clear.
			// Full mask -> threshold 0 (dense white with bubble-hole shading);
			// weak mask -> only the brightest filaments survive (wispy fringe).
			const float threshold = 1.0f - saturate(foamMask * 1.3f);
			foam = saturate((lace - threshold) / 0.28f);
			foam *= foam * (3.0f - 2.0f * foam); // soften the dissolve edge
			foam *= saturate(0.35f + foamMask);  // wisps stay lighter than cores
			// ...but a WEAK mask must produce no foam at all, not a 35%-opacity
			// film: those rendered as flat translucent pale-blue blobs on the
			// dark body near the camera. Foam is opaque white water or absent.
			foam *= smoothstep(0.10f, 0.40f, foamMask);
		}

		float4 ambient = float4(g_atmosphere.ambientLight.rgb * fadeColour.rgb, 1.0f);

		// BODY = volume in-scatter, NOT a lit surface. The old term was
		// Lambert (fadeColour x N.L): it shaded every wave face like painted
		// plastic, the single most "cartoon" cue in the shader. Light that
		// comes back OUT of water has been scattered inside the volume, so it
		// depends on how much sun enters the sea (sun elevation), not on the
		// facet normal. 0.40 keeps the overall level of the old term for a
		// flat surface while removing the per-facet shading.
		const float sunUpBody = saturate(lightDir.y);
		const float bodySun = 0.40f * sunUpBody * g_globalLight[0] * lerp(0.45f, 1.0f, sunShadow);
		float3 litBody = fadeColour.rgb * bodySun + ambient.rgb * 0.35f;

		// Crest subsurface glow: looking toward the sun, light crosses the
		// thin top of a wave and exits green-turquoise. Height comes from the
		// analytic detail field + the swell crest interpolant; strongest with
		// a low sun (long path through the crest).
		{
			const float3 sunAzimuth = normalize(float3(lightDir.x, 0.0f, lightDir.z) + float3(1e-5f, 0.0f, 0.0f));
			const float towardSun = pow(saturate(dot(-eyeVector, sunAzimuth) * 0.5f + 0.5f), 3.0f);
			const float crestHeight = saturate(detailHeight01 * 0.75f + saturate(input.colour.x) * 0.5f);
			const float sss = towardSun * crestHeight * crestHeight
				* lerp(1.0f, 0.45f, sunUpBody) * g_globalLight[0] * sunShadow * (0.25f + 0.75f * seaState);
			litBody += float3(0.02f, 0.26f, 0.22f) * sss * 0.55f;
		}

		// Optically thin water shows the refracted scene; thick water shows
		// the in-scatter colour. ONE blend, driven by (spectral) absorption
		// alone - the old second lerp keyed on fresnel*shoreFade coupled body
		// colour to reflectance, which is why the controls fought each other.
		float3 waterBodyColour = lerp(litBody, worldDiffuse.rgb, transmission);
		float4 retCol = float4(waterBodyColour, 1.0f);

		// Reflections (O3): inline screen-space march for near-field content
		// + prefiltered sky-atlas fallback everywhere the march misses (off-
		// screen, behind camera, beyond march range, and the whole far sea -
		// the atlas is weather-tinted, so a storm sky reflects as OVERCAST).
		// This replaced both the legacy 24-step march and the "cheap
		// reflection" (beauty at the pixel's own position - positionally
		// meaningless, it reflected whatever was BEHIND the water).
		{
			// Reflect off a mostly-GERSTNER normal: a mirror image needs a
			// far smoother surface than shading does (per-texel bump scatter
			// sends adjacent SSR rays to unrelated targets and shreds the
			// reflection - the deferred SSR's puddle-flatten exists for the
			// same reason). 0.35 bump influence near the camera, fading to
			// PURE Gerstner with distance: far pixels cover many bump texels,
			// so any bump residue there is per-pixel ray divergence = noise.
			float3 reflectionNormal = normalize(lerp(originalWorldNormal, worldNormal, 0.35f * distantNormalFade));

			// Camera below the WAVE surface (CPU-evaluated, g_oceanConfig3.z).
			// This used to test g_eyePos.y <= 0 - sea level hardcoded to world
			// zero, so it never fired in any scene whose ocean sits elsewhere.
			if (g_oceanConfig3.z > 0.0f)
				reflectionNormal *= -1.0f;

			float3 R = normalize(reflect(-eyeVector, reflectionNormal));
			// Back-facing wavelets send R below the horizon. In reality that
			// ray strikes the next wave and carries on to the low sky; the old
			// code instead FADED the reflection out (envHorizon), leaving the
			// refracted brown bottom showing through in blotches across the
			// mid-field. Fold the ray back above the horizon.
			if (R.y < 0.02f)
			{
				R.y = 0.02f + abs(R.y) * 0.5f;
				R = normalize(R);
			}

			float3 reflection = float3(0.0f, 0.0f, 0.0f);
			float reflectionWeight = 0.0f;

			// Near-field: march the opaque depth. Distance-gated - far rays
			// take the env path directly (matches the old ssrQualityWeight
			// ramp and keeps the horizon cheap).
			if (ssrQualityWeight > 0.001f)
			{
				float3 ssrColour;
				float ssrConfidence;
				if (TraceWaterSSR(input.positionWS.xyz, R, ssrColour, ssrConfidence))
				{
					reflectionWeight = ssrConfidence * ssrQualityWeight;
					reflection = ssrColour;
				}
			}

			// Environment fallback wherever the march found nothing. The atlas
			// row follows the sea state: calm water mirrors a sharp bright
			// sky; a wind-chopped surface reflects a blurred (and naturally
			// dimmer) prefiltered row - which also takes the edge off the
			// clear-sky brightness on rippled water.
			{
				// Roughness from the unresolved slope variance (see waterAlpha),
				// with the sea-state floor on top.
				const float envRoughness = max(waterPerceptualFromVariance, lerp(0.06f, 0.5f, seaState));
				const float3 envColour = SampleEnvAtlas(g_iblSkyEnvFwd, g_TexSamplerAniso, R, envRoughness);
				// R is already folded above the horizon, so the env term always
				// has a valid sky direction - no horizon fade-out.
				const float envWeight = (1.0f - reflectionWeight);

				reflection = reflection * reflectionWeight + envColour * envWeight;
				reflectionWeight = saturate(reflectionWeight + envWeight);
				reflection = reflectionWeight > 1e-4f ? reflection / reflectionWeight : float3(0.0f, 0.0f, 0.0f);
			}

			// Compose: reflection replaces body colour by FRESNEL x artist
			// strength - depth/shore terms removed from reflectance (they
			// belong to colour and see-through, not to how mirror-like the
			// surface is). The sun glint ADDS on top (its GGX F term carries
			// its own Fresnel). Foam suppresses both - scattered white water
			// is matte, not a mirror.
			// NO saturate: linear HDR into an R16G16B16A16_FLOAT target.
			const float reflectionStrength = g_oceanConfig.reflectionStrength;
			retCol.xyz = lerp(retCol.xyz, reflection,
				saturate(reflectionStrength * fresnel * reflectionWeight) * (1.0f - foam));
			retCol.xyz += specular.xyz * (1.0f - foam);
		}

		// Foam sits ON the surface: matte white water lit by ambient + sun
		// diffuse (shadowed), replacing whatever is beneath it.
		if (foam > 0.001f)
		{
			// Wrapped lighting on the smooth SWELL normal. Foam is a thick
			// multiply-scattering layer: it does not shade facet-by-facet. With
			// the per-pixel wave normal, any patch sitting on a wavelet tilted
			// away from the sun dropped to ambient-only and rendered as a flat
			// grey-blue blob next to brilliant white neighbours.
			const float foamNdl = saturate(dot(normalize(input.normal.xyz), lightDir) * 0.55f + 0.45f);
			const float3 foamLit = float3(0.86f, 0.88f, 0.90f)
				* (g_atmosphere.ambientLight.rgb
					+ getSunColour() * g_globalLight[0] * foamNdl * sunShadow);
			retCol.xyz = lerp(retCol.xyz, foamLit, foam);
		}

		// SURFACE SEEN FROM BELOW (underwater S2). Everything above shades the
		// AIR side of the interface; from underneath it rendered as a flat
		// grey sheet. Below the surface the optics invert:
		//  - inside SNELL'S WINDOW (the ~97 deg cone where a ray can still
		//    refract out of n=1.33 water into air) you see the whole sky
		//    compressed into that disc, its edge distorted by every wavelet;
		//  - outside it is TOTAL INTERNAL REFLECTION - the surface is a mirror
		//    of the water below, i.e. the dim in-scatter of the deep.
		// refract() returns 0 past the critical angle, which IS the window
		// edge; Fresnel (on the transmitted angle) softens the rim. The
		// underwater post pass then fogs the path from the lens to here.
		const bool viewFromBelow = g_oceanConfig3.z > 0.0f;
		if (viewFromBelow)
		{
			const float3 incident = -eyeVector;            // lens -> surface, heading up
			// Refraction sees the surface from a couple of metres away, where
			// the capillary-scale slope the band-limited shading normal has
			// smoothed out is exactly what frays the window rim and shatters
			// the sun. Exaggerate the normal's tilt for the INTERFACE only
			// (first capture: the rim was a smooth cartoon blob).
			// ...but ONLY up close. Applied everywhere, it tipped distant
			// facets (seen at grazing angles, where total internal reflection
			// should be near-total) past the critical angle, and the window
			// smeared down to the horizon as a flat cyan sheet. Fades to the
			// true normal by ~18 m.
			const float interfaceTilt = lerp(1.9f, 1.0f, saturate(cameraDistance / 18.0f));
			// ...and ONLY the wavelets. Scaling the whole normal doubled the
			// SWELL's tilt too: on a 12 degree swell face the window slid ~25
			// degrees off the zenith and total internal reflection swallowed
			// most of a straight-up view.
			// It is the WAVE normal (swell + analytic wavelets) that gets this, not
			// the bump-mapped shading normal: at a couple of metres the normal-map
			// layer tilts nearly every texel past 32 degrees (debug view), which
			// from below is past the critical angle more often than not.
			const float3 interfaceNormal = normalize(swellNormal + (originalWorldNormal - swellNormal) * interfaceTilt);
			const float3 facingNormal = -interfaceNormal;  // interface normal on the viewer's side
			const float3 transmitted = refract(incident, facingNormal, 1.333f);

			const float3 bodyLight = 0.40f * saturate(lightDir.y) * g_globalLight[0] + g_atmosphere.ambientLight.rgb * 0.35f;
			const float3 mirrorOfDeep = lerp(g_oceanConfig.deepColour.rgb, g_oceanConfig.shallowColour.rgb, 0.22f) * bodyLight;

			float3 underside = mirrorOfDeep;
			if (dot(transmitted, transmitted) > 1e-4f)
			{
				const float3 skyDir = normalize(transmitted);
				const float cosT = saturate(dot(skyDir, worldNormal));
				const float rimFresnel = 0.02f + 0.98f * pow(1.0f - cosT, 5.0f);
				float3 sky = SampleEnvAtlas(g_iblSkyEnvFwd, g_TexSamplerAniso, skyDir, 0.04f);
				// The sun itself through the window: the env atlas carries no
				// disc, and the shimmering sun ball is the signature of looking
				// up from under water.
				const float sunThrough = pow(saturate(dot(skyDir, lightDir)), 900.0f);
				sky += getSunColour() * g_globalLight[0] * (sunThrough * 6.0f * sunShadow);
				// Whatever stands above the water along the refracted ray - pier,
				// hull, shoreline - replaces the sky there (and hides the sun
				// behind it, which the analytic disc above would otherwise
				// shine straight through).
				float3 aboveWater;
				float aboveWaterConfidence;
				if (TraceRefractedScene(input.positionWS.xyz, skyDir, input.position.xy, aboveWater, aboveWaterConfidence))
					sky = lerp(sky, aboveWater, aboveWaterConfidence);
				// WAVELET FOCUSING. A prefiltered sky is smooth, so refracting it
				// through the waves alone shows nothing - overhead the first
				// version was a featureless blue disc. What draws the rippling
				// light-net on a real underside is that each facet passes light
				// in proportion to how squarely it faces the sun (the same
				// mechanism that makes caustics on the seabed). Ratio against a
				// flat surface, so the mean stays ~1.
				// Squared was invisible (a +/-0.2 ratio); the light-net on a real
				// underside is HIGH contrast - bright filaments, dim cells -
				// because focusing is strongly non-linear in facet tilt. ^6 on
				// the exaggerated interface normal, renormalised by its flat
				// value (1) so the mean brightness holds.
				const float facetSun = saturate(dot(interfaceNormal, lightDir)) / max(lightDir.y, 0.15f);
				const float focus = pow(max(facetSun, 0.0f), 6.0f);
				sky *= lerp(1.0f, clamp(focus, 0.25f, 2.6f), saturate(lightDir.y * 4.0f));
				// Exposure under water adapts to the dim in-scatter, against which
				// the raw HDR sky is ~20x over: the window clipped to a shapeless
				// white bloom blob at shallow angles. 0.38 keeps it the brightest
				// thing in frame (it should be) while its structure survives the
				// tonemapper.
				sky *= 0.38f;
				underside = lerp(sky, mirrorOfDeep, rimFresnel);
			}
			retCol.xyz = underside;
		}

		retCol.a = 1.0f;

		// Water renders after the fog / AP applies: fog it at its own depth.
		// (Not from below: that path is water, and the underwater pass owns it.)
		if (!viewFromBelow)
			retCol.rgb = ApplyTransparentAtmosphere(retCol.rgb, input.positionWS.xyz, input.position.xy, g_transFogVolume, g_transApVolume, g_transLinearSampler);

		WaterOut o;
		o.colour = retCol;
		// Same clip-space [0,1] delta convention as the gbuffer (consumers
		// negate y) - see Utils.shader CalcVelocity.
		//
		// GUARDS (found the hard way): the ocean grid extends to the horizon,
		// where the interpolated clip positions' w approaches zero - and the
		// PREVIOUS-frame position can even land behind the projection plane
		// (w <= 0). CalcVelocity divides by w unchecked, so far/grazing water
		// wrote hundreds-of-pixels garbage velocity (and NaNs) across the
		// whole sky region once the transparent pass gained a velocity RT -
		// TAA/motion blur then smeared the frame. Genuine wave motion is
		// small: zero the velocity when w is degenerate and clamp hard.
		o.velocity = 0.0f.xx;
		if (input.currentPositionUnjittered.w > 1e-3f && input.previousPositionUnjittered.w > 1e-3f)
		{
			float2 vel = CalcVelocity(input.currentPositionUnjittered, input.previousPositionUnjittered,
				float2(g_screenWidth, g_screenHeight));
			const float maxUvDelta = 0.03f; // ~2 tiles of blur at any resolution
			const float len = length(vel);
			if (len > maxUvDelta)
				vel *= maxUvDelta / len;
			// Belt and braces: a NaN here poisons the tile-max chain.
			if (!isnan(vel.x) && !isnan(vel.y))
				o.velocity = vel;
		}
		return o;
	}
}
