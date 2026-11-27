"Global"
{
	// Shared cloud definitions: the constant buffer and the density function,
	// used by the cloud render (VolumetricClouds), the cloud shadow-map pass
	// (CloudShadowMap) and any consumer of the shadow map (Deferred, fog).
	//
	// ONE density function. It was hand-duplicated into Deferred.shader for
	// ground shadows and drifted twice (an inverted erosion curve, then the
	// whole weather-map rework), each time producing ground shadows cast by
	// clouds that no longer existed. Textures are passed in so every caller
	// can bind its noise volumes at whatever slots it likes.

	cbuffer CloudConstants : register(b4)
	{
		float4 g_cloudBoundsMin;
		float4 g_cloudBoundsMax;
		float4 g_cloudParams0; // x=density, y=coverage, z=erosion, w=maxDistance
		float4 g_cloudParams1; // x=absorption, y=powder, z=anisotropy, w=stepScale
		float4 g_cloudParams2; // x=shapeScale, y=detailScale, z=windSpeed, w=animationSpeed
		float4 g_cloudParams3; // x=viewAbsorption, y=ambientStrength, z=shadowFloor, w=phaseBoost
		float4 g_cloudParams4; // x=silverLiningStrength, y=silverLiningExponent, z=multiScatterStrength, w=heightTintStrength
		float4 g_cloudParams5; // x=tintWarmth, y=skyTintInfluence, z=directionalDiffuse, w=ambientOcclusion
		float4 g_cloudWindDirection; // xyz=wind direction, w=quality preset
		float4 g_cloudWindOffset; // xyz=accumulated wind offset, w=reserved
		float4 g_cloudMarch; // x=view steps, y=light steps, z=ground shadow steps, w=ground shadow strength
		// Cached cloud SHADOW MAP placement (see CloudShadowMap.shader). The
		// map is a top-down transmittance image parameterised on the cloud
		// BASE plane: any ground point projects along the sun ray onto that
		// plane, which is exact for parallel light. Replaces the per-pixel
		// slab re-march the deferred sun pass used to do.
		float4 g_cloudShadowMapOrigin; // xyz = map centre on the base plane, w = half extent (0 = no map this frame)
		float4 g_cloudShadowMapAxisX;  // xyz = map +U axis (world, horizontal)
		float4 g_cloudShadowMapAxisZ;  // xyz = map +V axis (world, horizontal)
		float4 g_cloudShadowMapSun;    // xyz = sun direction (surface -> sun), w = shadow strength
		// S5 cloud types. x = type bias (0 = force stratus, 0.5 = natural
		// mix from the type field, 1 = force cumulus), y/z/w reserved.
		// TAIL-APPENDED: every CloudConstants consumer recompiles together
		// (VolumetricClouds, CloudShadowMap, Deferred, VolumetricScatterDensity).
		float4 g_cloudParams6;
	};

	float CloudHash12(float2 p)
	{
		const float h = dot(p, float2(127.1f, 311.7f));
		return frac(sin(h) * 43758.5453123f);
	}

	float CloudValueNoise2(float2 p)
	{
		const float2 pi = floor(p);
		const float2 pf = frac(p);
		const float2 w = pf * pf * (3.0f - 2.0f * pf);
		const float n00 = CloudHash12(pi);
		const float n10 = CloudHash12(pi + float2(1.0f, 0.0f));
		const float n01 = CloudHash12(pi + float2(0.0f, 1.0f));
		const float n11 = CloudHash12(pi + float2(1.0f, 1.0f));
		return lerp(lerp(n00, n10, w.x), lerp(n01, n11, w.x), w.y);
	}

	// 2D WEATHER MAP - the root fix for clouds never filling the sky. Cloud
	// PRESENCE is decided per XZ column by this horizontal field, so
	// placement is altitude-independent and the coverage cvar maps ~linearly
	// onto actual sky fraction (1.0 = a true overcast is reachable). The 3D
	// Perlin-Worley noises only SCULPT the shapes the map dictates. Advected
	// by the same integrated wind offset as the shape noise, at a slower
	// rate - weather systems drift slower than the cloud tops churn.
	float CloudWeatherCoverage(float2 xz, float2 windXz)
	{
		const float2 p = (xz + windXz * 220.0f) * (1.0f / 1400.0f); // ~1.4 km systems
		const float fbm =
			CloudValueNoise2(p) * 0.55f +
			CloudValueNoise2(p * 2.3f + 17.1f.xx) * 0.30f +
			CloudValueNoise2(p * 5.1f + 41.7f.xx) * 0.15f;
		return saturate(fbm);
	}

	// S5: cloud TYPE per column, 0 = stratus (low flat deck) -> 1 = cumulus
	// (tall billowing tower). An independent lower-frequency field (~4 km
	// systems) so a sky can hold both a stratus bank and a cumulus cluster
	// at once; g_cloudParams6.x biases the whole field (0.5 = as-authored,
	// 0 = everything stratus, 1 = everything cumulus).
	float CloudWeatherType(float2 xz, float2 windXz)
	{
		const float2 p = (xz + windXz * 160.0f) * (1.0f / 4200.0f);
		const float field =
			CloudValueNoise2(p + 71.3f.xx) * 0.65f +
			CloudValueNoise2(p * 2.6f + 9.3f.xx) * 0.35f;
		return saturate(field + (g_cloudParams6.x - 0.5f) * 1.6f);
	}

	// Global "storm sky" gate: coverage high enough + type bias toward
	// towering cumulus (Storm preset). Shared by the density function,
	// VolumetricClouds' effective slab top and the rain shafts.
	float CloudStormSky()
	{
		return smoothstep(0.50f, 0.65f, saturate(g_cloudParams0.y)) * smoothstep(0.55f, 0.85f, g_cloudParams6.x);
	}

	// Weather-map column coverage including the storm-scale blend (see the
	// STORM-SCALE SYSTEMS note in SampleCloudDensityTexImpl).
	float CloudColumnCoverage(float2 xz, float2 windXz)
	{
		float wm = CloudWeatherCoverage(xz, windXz);
		const float stormSky = CloudStormSky();
		if (stormSky > 0.001f)
		{
			const float wmLarge = CloudWeatherCoverage(xz * (1.0f / 2.6f) + float2(3711.0f, -1907.0f), windXz * (1.0f / 2.6f));
			// Blending two independent fields shrinks the spread (sd x0.76),
			// which would silently raise coverage - re-stretch about 0.5.
			const float mixed = saturate(0.5f + (wmLarge * 0.70f + wm * 0.30f - 0.5f) * 1.31f);
			wm = lerp(wm, mixed, stormSky);
		}
		return wm;
	}

	float2 CloudRayBoxDist(float3 boundsMin, float3 boundsMax, float3 rayOrigin, float3 rayDir)
	{
		float3 safeDir = rayDir;
		safeDir.x = abs(safeDir.x) < 1e-5f ? (safeDir.x < 0.0f ? -1e-5f : 1e-5f) : safeDir.x;
		safeDir.y = abs(safeDir.y) < 1e-5f ? (safeDir.y < 0.0f ? -1e-5f : 1e-5f) : safeDir.y;
		safeDir.z = abs(safeDir.z) < 1e-5f ? (safeDir.z < 0.0f ? -1e-5f : 1e-5f) : safeDir.z;
		const float3 invDir = 1.0f / safeDir;
		const float3 t0 = (boundsMin - rayOrigin) * invDir;
		const float3 t1 = (boundsMax - rayOrigin) * invDir;
		const float3 tmin = min(t0, t1);
		const float3 tmax = max(t0, t1);

		const float dstA = max(max(tmin.x, tmin.y), tmin.z);
		const float dstB = min(tmax.x, min(tmax.y, tmax.z));

		const float dstToBox = max(0.0f, dstA);
		const float dstInsideBox = max(0.0f, dstB - dstToBox);

		return float2(dstToBox, dstInsideBox);
	}

	// withDetail=false: COARSE density for LIGHT marches (sun/shadow). At
	// grazing sun angles (sunset - THE cinematic hour) the detail-eroded
	// field has huge high-frequency variance along a horizontal light path,
	// and the few light steps turn that into per-pixel optical-depth noise -
	// the deck rendered as white speckle. Lighting through the smooth
	// shape/coverage boundary integrates cleanly (and drops one 3D fetch per
	// light step). The VIEW march keeps full detail.
	// detailAmount: 1 = full detail erosion (near view samples), 0 = coarse
	// (light marches, far view samples - detail at distance aliases into
	// sparkle grain, worst against a bright dusk sun).
	float SampleCloudDensityTexImpl(Texture3D shapeNoise, Texture3D detailNoise, SamplerState noiseSampler, float3 worldPos, float3 boundsMin, float3 boundsMax, float3 windOffset, float detailAmount)
	{
		const float3 boundsSize = max(boundsMax - boundsMin, 1e-3f.xxx);
		const float3 localUVW = (worldPos - boundsMin) / boundsSize;

		if (any(localUVW < 0.0f.xxx) || any(localUVW > 1.0f.xxx))
			return 0.0f;

		// Column presence from the 2D weather map (see WeatherCoverage). The
		// old path thresholded the 3D shape noise directly, so whether a
		// cloud existed depended on what that noise contained at this
		// position AND altitude - coverage was accidental and no cvar value
		// could produce an overcast.
		const float coverage = saturate(g_cloudParams0.y);
		const float cloudType = CloudWeatherType(worldPos.xz, windOffset.xz);
		// STORM CELLS (cumulonimbus). Only when the sky is BOTH tall-cumulus
		// typed and heavily covered (Storm preset); fair-weather cumulus
		// (Clear/Hot: type ~0.85-0.9 at coverage ~0.1) never qualifies.
		const float storminess = smoothstep(0.72f, 0.95f, cloudType) * smoothstep(0.50f, 0.65f, coverage);

		// ABSOLUTE-METRE HEIGHT PROFILES. Every profile used to be a fraction
		// of the slab, so a taller slab stretched ALL clouds - a 5 km slab
		// turned fair-weather cumulus into vertical curtains - and real
		// cumulonimbus (8-12 km towers, the stormy-sunset reference) were
		// impossible. Base terms (lift, base ramp, wispy base) now run on a
		// fixed 2050 m design depth (= the old 350-2400 m slab, so the look
		// is unchanged there); crown terms (top, taper, anvil, height
		// erosion) run on a tower depth that only STORM cells stretch to the
		// full slab. Raise r_cloudAabbMax.y to give storms room; every other
		// type keeps its height.
		const float altitude = worldPos.y - boundsMin.y;
		const float kDesignDepth = 2050.0f;
		const float towerDepth = max(1.0f, min(boundsSize.y, lerp(kDesignDepth, max(boundsSize.y, kDesignDepth), storminess)));
		const float hBase = saturate(altitude / kDesignDepth);

		// WIND SHEAR: columns lean downwind with altitude (storm towers
		// most), so tall clouds stop reading as the 2D map extruded straight
		// up and anvils stream ahead of their cells.
		float2 shearDir = g_cloudWindDirection.xz;
		shearDir = dot(shearDir, shearDir) > 1e-6f ? normalize(shearDir) : float2(1.0f, 0.0f);
		const float2 columnXz = worldPos.xz - shearDir * altitude * lerp(0.05f, 0.16f, storminess);
		// STORM-SCALE SYSTEMS. The ~1.4 km weather map suits cumulus fields,
		// but cumulonimbus cells are 5-10 km across with wide gaps - which
		// is why a storm reads as distant towers over open sky rather than
		// one base overhead. Storm skies (global gate, same as
		// VolumetricClouds' stormPotential, so it's continuous everywhere)
		// sample the map ~2.6x larger, keeping 30% of the small map for
		// lobed cell edges.
		const float wm = CloudColumnCoverage(columnXz, windOffset.xz);
		// HONEST coverage mapping. The previous remap biased upward (0.80
		// nominal rendered ~95% cover). The 3-octave value fbm spans roughly
		// [0.2, 0.8] around 0.5, so the cvar maps to a quantile-ish
		// threshold across that range: 0.5 = about half the sky, 0.8 = heavy
		// cover with real breaks, 1.0 = closed deck. The smoothstep band
		// keeps cloud boundaries soft instead of every column snapping to
		// full strength.
		const float threshold = lerp(0.80f, 0.16f, coverage);
		// Crown-profile height (tower-relative; = slab fraction for non-storm
		// types when the slab is the 2050 m design depth).
		const float height = saturate(altitude / towerDepth);
		// TOWER TAPER. The 2D weather column extruded straight up through
		// the height profile rendered every cumulus as a vertical pillar -
		// the "columnar / striated" look on tall faces. Raising the coverage
		// threshold with height narrows each column toward its top, so
		// towers build as a broad base thinning into a cauliflower crown.
		// Type-weighted: stratus decks keep their flat extent.
		// A storm cell's updraft core punches through the taper (the tower
		// stays fat all the way up) and spreads into a flat ANVIL at the
		// tropopause stand-in (top ~15% of the tower): the coverage threshold
		// drops there, so the crown widens out over neighbouring columns.
		const float anvil = storminess * smoothstep(0.70f, 0.88f, height) * (1.0f - smoothstep(0.95f, 1.0f, height));
		const float taper = smoothstep(0.30f, 1.0f, height) * lerp(0.02f, 0.26f, cloudType) * (1.0f - 0.55f * storminess) /* full removal = vertical walls */
		                  - anvil * 0.10f; // 0.22 merged towers into one ceiling overhead
		const float columnCoverage = smoothstep(threshold + taper, threshold + 0.24f + taper, wm);
		if (columnCoverage <= 0.002f)
			return 0.0f;

		// Vertical anisotropy on the shape noise for cumulus: a higher
		// vertical frequency breaks the column into stacked billows instead
		// of one smear that barely changes with altitude. Decks stay
		// isotropic (they read as horizontal sheets).
		const float3 shapeFreq = float3(1.0f, lerp(1.0f, 1.9f, cloudType), 1.0f);
		const float shape = shapeNoise.SampleLevel(noiseSampler, worldPos * g_cloudParams2.x * shapeFreq + windOffset, 0.0f).r;
		// DETAIL with a turbulent DOMAIN WARP (curl-noise stand-in). Plain
		// detail erosion carves the same isotropic cell pattern into every
		// edge, which reads as uniform "cotton". A low-frequency warp
		// (one extra fetch of the detail volume at ~1/4 its frequency) swirls
		// the erosion pattern so edges tear into wisps and curls; the warp
		// grows toward the base where real clouds are most turbulent.
		float detail = 0.5f;
		if (detailAmount > 0.001f)
		{
			const float3 detailP = worldPos * g_cloudParams2.y + windOffset * 1.7f;
			const float w = detailNoise.SampleLevel(noiseSampler, detailP * 0.23f + float3(0.37f, 0.11f, 0.73f), 0.0f).r;
			const float a = w * 6.2831853f;
			const float3 warp = float3(sin(a), (w - 0.5f) * 1.2f, cos(a)) * lerp(0.42f, 0.22f, saturate(hBase * 2.0f));
			const float rawDetail = detailNoise.SampleLevel(noiseSampler, detailP + warp, 0.0f).r;
			// Nubis: WISPY (non-inverted cellular) at the base, BILLOWY
			// (inverted - cauliflower) higher up the column.
			const float shapedDetail = lerp(1.0f - rawDetail, rawDetail, saturate((hBase - 0.04f) * 7.0f));
			detail = lerp(0.5f, shapedDetail, detailAmount);
		}

		// LUMPY UNDERSIDE: a flat cloud base renders a full deck as one
		// featureless slab - real overcast reads as cloud because the base
		// altitude varies and the light march turns those bumps into the
		// light/dark mottling you actually see from below. Lift the base by
		// the weather map and shape noise per column.
		const float baseLift = (1.0f - wm) * 0.26f + (1.0f - shape) * 0.12f;

		// S5 TYPE-DRIVEN HEIGHT PROFILE. The slab default grew 700 m ->
		// ~2 km, and one fixed vertical shape can't serve both a flat
		// stratus deck and a towering cumulus in it. The per-column type
		// field picks the profile: stratus tops out ~35% of the slab
		// (~700 m) with a soft flat top; cumulus builds to ~98% (2 km
		// towers). Bases stay put - flat bases are what sells fair-weather
		// cumulus (reference: RDR2 midday shots).
		// Storm cells build to the slab ceiling so the anvil band above has
		// mass (topStart stays < topEnd: 0.84 max vs 0.99).
		// DOMED storm crowns: the old taper narrowed every storm column to
		// a POINT (the weather-map bump's contour shrinking with height ->
		// cone/mountain-peak tops). Storm columns now keep their width and
		// instead reach a height that follows the column's fullness, so
		// the crown traces the map bump itself - a rounded dome, tallest
		// over the cell core, before the anvil band.
		const float domeK = lerp(1.0f, lerp(0.55f, 1.0f, smoothstep(threshold, threshold + 0.30f, wm)), storminess);
		const float topStart = (lerp(0.22f, 0.72f, cloudType) + storminess * 0.12f) * domeK;
		const float topEnd   = min(0.995f, lerp(0.38f, 0.98f, cloudType) + storminess * 0.03f) * domeK;
		// Base ramp on the METRE scale (hBase), crown on the tower scale.
		const float heightMask = smoothstep(0.03f + baseLift, lerp(0.10f, 0.24f, cloudType) + baseLift, hBase)
		                       * (1.0f - smoothstep(topStart, topEnd, height));
		// Fuller weather columns build TALLER clouds: widen the vertical core
		// with coverage so an overcast reads as a deck, scattered as puffs.
		// coreTop is derived FROM the type band (topStart/topEnd) so it can
		// never cross topEnd: an earlier independent lerp let coreTop exceed
		// topEnd for low types at high coverage, and the resulting malformed
		// smoothstep (edge0 > edge1) returned 1 below the band - zero density
		// exactly where the weather map was FULLEST. That rendered rain/storm
		// decks as hollow blobs with dense outlines (interior cc=1 = empty,
		// rim cc~0.5 = valid).
		const float coreTop = lerp(topStart * 0.80f, topEnd * 0.92f, columnCoverage);
		const float verticalCore = smoothstep(0.05f + baseLift * 0.8f,
			lerp(0.55f, 0.35f, columnCoverage) * lerp(0.55f, 1.0f, cloudType) + baseLift * 0.5f, hBase)
			* (1.0f - smoothstep(coreTop, topEnd, height));

		// 3D Perlin-Worley SCULPTS the column the map dictates. REMAP form
		// (Schneider), not raw subtraction: plain subtraction stacked on a
		// base that is already a product of <=1 terms crushed interior
		// density to ~0.1 and made the whole sky nearly invisible. The remap
		// (x - e) / (1 - e) eats from the BOTTOM - edges erode away while a
		// full column keeps ~full density.
		float cloud = columnCoverage * heightMask * verticalCore;
		const float shapeErode = (1.0f - shape) * lerp(0.62f, 0.30f, columnCoverage);
		cloud = saturate((cloud - shapeErode) / max(0.05f, 1.0f - shapeErode));

		// MID-SCALE BILLOWS (the missing octave). The shape volume tiles every
		// ~5.9 km (billows ~1.5 km) and the scene's detail volume every
		// ~150 m (cells ~12 m - sub-pixel beyond a couple of km). Nothing
		// lived in the 100-500 m band that makes a cumulus read as stacked
		// cauliflower lobes and a storm base as a lumpy, sagging underside -
		// the deck rendered as a smooth blanket. Re-sample the Perlin-Worley
		// shape volume at ~4.7x frequency (lobes ~250-350 m) and erode the
		// boundary with it (remap: interiors stay full, surfaces get lobed).
		// Applied in the COARSE path too: the light march must see the lobes
		// or they don't self-shadow and the surface is lit flat again.
		// Rotated + offset domain so it never lines up with the base octave.
		{
			const float3 midP = float3(worldPos.x * 0.8f + worldPos.z * 0.6f, worldPos.y, worldPos.z * 0.8f - worldPos.x * 0.6f)
			                  * (g_cloudParams2.x * 4.7f) * shapeFreq + windOffset * 1.4f + float3(0.31f, 0.17f, 0.53f);
			const float mid = shapeNoise.SampleLevel(noiseSampler, midP, 0.0f).r;
			// Stronger on cumulus/storm towers, gentler on stratus decks;
			// strongest at the base and crown where lobes are most visible.
			const float edgeBand = 1.0f - 0.35f * smoothstep(0.25f, 0.55f, height) * (1.0f - smoothstep(0.60f, 0.85f, height));
			const float midErode = saturate((1.0f - mid) * lerp(0.40f, 0.58f, cloudType) * edgeBand);
			cloud = saturate((cloud - midErode) / max(0.05f, 1.0f - midErode));

			// Second billow octave (~2.4x the first: lobes ~100-150 m) - the
			// cauliflower-on-cauliflower of a growing cumulus. Detail path
			// only (view march + the short near light steps), so the long
			// light marches / shadow map / froxels don't pay for it.
			if (detailAmount > 0.001f)
			{
				const float3 mid2P = float3(worldPos.x * 0.6f - worldPos.z * 0.8f, worldPos.y, worldPos.z * 0.6f + worldPos.x * 0.8f)
				                   * (g_cloudParams2.x * 11.3f) * shapeFreq + windOffset * 2.1f + float3(0.71f, 0.43f, 0.29f);
				const float mid2 = shapeNoise.SampleLevel(noiseSampler, mid2P, 0.0f).r;
				const float mid2Erode = saturate((1.0f - mid2) * lerp(0.18f, 0.40f, cloudType) * edgeBand * detailAmount);
				cloud = saturate((cloud - mid2Erode) / max(0.05f, 1.0f - mid2Erode));
			}
		}
		// Cumulus tops billow harder than stratus decks; scale the height
		// erosion with type so towers get carved detail and decks stay smooth.
		const float erosionByHeight = lerp(0.55f, 1.45f, smoothstep(0.25f, 0.95f, height)) * lerp(0.75f, 1.1f, cloudType);
		if (detailAmount > 0.001f)
		{
			const float detailErode = saturate((1.0f - detail) * g_cloudParams0.z * erosionByHeight * detailAmount);
			cloud = saturate((cloud - detailErode) / max(0.05f, 1.0f - detailErode));
		}

		// STRUCTURE. The remap above deliberately pins full columns at 1 so
		// coverage stays honest - but that erases density VARIATION, and a
		// full-cover sky rendered as one flat grey blob. Real decks vary
		// 40-100% in optical depth across metres; the sculpt noises modulate
		// density INSIDE the boundary the remap defines - that variation is
		// what the light march turns into underside shading and billow
		// structure.
		const float structure =
			lerp(0.42f, 1.0f, shape) *
			lerp(0.68f, 1.05f, detail) *
			lerp(0.78f, 1.0f, wm);
		// CRISP BOUNDARY. The old lerp(cloud^2, cloud, 0.55) is SOFTER than
		// linear: the outer third of every cloud held ~5-20% density, a
		// ~250 m ramp to optical depth 1 that rendered as fuzzy cotton-wool
		// edges. Front-loading the ramp (smoothstep over the first 45%)
		// doubles edge density so silhouettes read sharp and sculpted - the
		// boundary POSITION (and so coverage) is unchanged, only how fast
		// density builds inside it.
		const float densityShape = (smoothstep(0.0f, 0.45f, cloud) * 0.6f + cloud * 0.4f) * structure;

		return min(densityShape * g_cloudParams0.x, 2.0f);
	}

	float SampleCloudDensityTex(Texture3D shapeNoise, Texture3D detailNoise, SamplerState noiseSampler, float3 worldPos, float3 boundsMin, float3 boundsMax, float3 windOffset)
	{
		return SampleCloudDensityTexImpl(shapeNoise, detailNoise, noiseSampler, worldPos, boundsMin, boundsMax, windOffset, 1.0f);
	}

	float SampleCloudDensityTexCoarse(Texture3D shapeNoise, Texture3D detailNoise, SamplerState noiseSampler, float3 worldPos, float3 boundsMin, float3 boundsMax, float3 windOffset)
	{
		return SampleCloudDensityTexImpl(shapeNoise, detailNoise, noiseSampler, worldPos, boundsMin, boundsMax, windOffset, 0.0f);
	}

	// Sample the cached cloud shadow map for a world position. Returns the sun
	// visibility multiplier in [0,1] (1 = no cloud shadow). Points above the
	// cloud top are unshadowed; points inside the slab use the full-slab
	// value (acceptable: tall towers / aircraft). The map edge fades to
	// unshadowed so the extent boundary never draws a line on the ground.
	float SampleCloudShadowMap(Texture2D shadowMap, SamplerState mapSampler, float3 worldPos)
	{
		const float halfExtent = g_cloudShadowMapOrigin.w;
		const float strength = saturate(g_cloudShadowMapSun.w);
		if (halfExtent <= 0.0f || strength <= 0.0001f)
			return 1.0f;
		if (worldPos.y > g_cloudBoundsMax.y)
			return 1.0f;

		const float3 sunDir = g_cloudShadowMapSun.xyz;
		if (sunDir.y <= 0.02f)
			return 1.0f;

		// Project along the sun ray up onto the cloud base plane.
		const float planeY = g_cloudBoundsMin.y;
		const float t = max(0.0f, (planeY - worldPos.y) / sunDir.y);
		const float3 q = worldPos + sunDir * t;
		const float3 local = q - g_cloudShadowMapOrigin.xyz;
		const float2 uv = float2(
			dot(local, g_cloudShadowMapAxisX.xyz),
			dot(local, g_cloudShadowMapAxisZ.xyz)) / halfExtent * 0.5f + 0.5f;

		// Edge fade (last 8%) - the sampler may WRAP, so clamp explicitly.
		const float2 edge = min(uv, 1.0f - uv);
		const float edgeFade = smoothstep(0.0f, 0.08f, min(edge.x, edge.y));
		if (edgeFade <= 0.0f)
			return 1.0f;
		const float2 cuv = clamp(uv, 0.002f, 0.998f);
		const float transmittance = shadowMap.SampleLevel(mapSampler, cuv, 0.0f).r;
		return lerp(1.0f, transmittance, strength * edgeFade);
	}
}
