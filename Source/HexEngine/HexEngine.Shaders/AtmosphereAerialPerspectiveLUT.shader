"ComputeShaderIncludes"
{
	Global
	AtmosphereCommon
}
"ComputeShader"
{
	// Hillaire 2020 aerial perspective volume generator.
	//
	// 32x32x32 RGBA16F camera-frustum-aligned froxel volume. Each froxel
	// stores the accumulated atmospheric in-scatter (RGB) and view-ray
	// transmittance (A) from the camera to that froxel's centre. Forward
	// pixels read from it to apply distance haze: distant geometry blends
	// toward the atmospheric scattering colour, removing the hard-silhouette
	// boundary the sky-view LUT alone leaves on terrain/buildings.
	//
	// Parameterisation:
	//   u, v : screen-space UV in [0, 1] (same axes as the camera image).
	//   w    : linear ray distance from camera in [0, MAX_DIST_M]. Linear
	//          rather than exp because the AP range (32 km) is short
	//          enough that linear works fine, and the apply pass needs
	//          a cheap forward mapping (w = rayDist / MAX_DIST_M).
	//
	// World-space integration coordinates:
	//   Each ray walks from camera to depth d. The world position at each
	//   step is lifted into Hillaire's planet-centred atmosphere frame via
	//   the same WorldYToAtmosphereAltitudeMM convention the sky shaders
	//   use, so the medium sampling and sun-transmittance lookups stay
	//   consistent with the SkyView LUT.
	//
	// Outputs at each froxel:
	//   RGB = sum_{n} stepTransmittance_n * phaseScatter_n * sunTransmittance_n
	//          + isoScatter_n * msContribution_n   (all multiplied by sunIntensity)
	//   A   = view-ray transmittance from camera to froxel centre
	//         (scalar, packed into A by averaging RGB-channel transmittance).

	RWTexture3D<float4> g_aerialPerspectiveVolume : register(u0);
	Texture2D           g_transmittanceLUT       : register(t0);
	Texture2D           g_multiScatteringLUT     : register(t1);
	SamplerState        g_linearSampler          : register(s4);

	cbuffer AtmosphereParams : register(b5)
	{
		float g_groundRadiusMM;
		float g_topRadiusMM;
		float g_rayleighScaleHeightMM;
		float g_mieScaleHeightMM;
		float g_ozoneCentreMM;
		float g_ozoneHalfWidthMM;
		float g__pad0;
		float g__pad1;
		float3 g_rayleighScatteringPerMM;
		float  g_mieScatteringPerMM;
		float g_mieExtinctionPerMM;
		float g_miePhaseG;
		float g__pad2;
		float g__pad3;
		float3 g_ozoneAbsorptionPerMM;
		float  g__pad4;
	};

	// Sun direction + intensity from the same per-frame sky-view cbuffer
	// the SkyView LUT generator uses. cameraHeightMM is included but the
	// compute shader uses g_eyePos.y from the engine per-frame cbuffer
	// directly so distant froxels can step through atmosphere correctly.
	cbuffer SkyViewParams : register(b6)
	{
		float  g_cameraHeightMM;
		float  g_skyViewPad0;
		float  g_skyViewPad1;
		float  g_skyViewPad2;
		float3 g_sunDirection;
		float  g_sunIntensity;
	};

	AtmosphereSamplePoint SampleAtmosphereMedium(float3 pos)
	{
		return SampleAtmosphereMediumWithCoefs(
			pos,
			g_rayleighScatteringPerMM,
			g_mieScatteringPerMM,
			g_mieExtinctionPerMM,
			g_ozoneAbsorptionPerMM);
	}

	float3 SampleTransmittance(float viewHeight, float viewZenithCos)
	{
		const float2 uv = TransmittanceLutParamsToUv(viewHeight, viewZenithCos);
		return g_transmittanceLUT.SampleLevel(g_linearSampler, uv, 0).rgb;
	}

	float3 SampleMultiScattering(float viewHeight, float sunCosZenith)
	{
		const float2 uv = MultiScatteringLutParamsToUv(viewHeight, sunCosZenith);
		return g_multiScatteringLUT.SampleLevel(g_linearSampler, uv, 0).rgb;
	}

	// Lift a world-space position (engine metres) into the atmosphere
	// frame (Mm, planet-centred). Same convention as the sky path - the
	// horizontal world XZ get scaled to Mm and the altitude is added to
	// the planet radius. Curvature at AP range (32 km) is negligible but
	// naturally falls out because length(pos) - groundR captures it.
	float3 WorldToAtmosphere(float3 worldP)
	{
		return float3(worldP.x * 1e-6f,
		              g_groundRadiusMM + worldP.y * 1e-6f,
		              worldP.z * 1e-6f);
	}

	static const uint3 LUT_DIMS = uint3(32u, 32u, 32u);
	static const uint  MARCH_STEPS_PER_FROXEL = 12u;
	static const float MAX_DIST_M = 32000.0f;

	[numthreads(8, 8, 8)]
	void ShaderMain(uint3 dtid : SV_DispatchThreadID)
	{
		if (any(dtid >= LUT_DIMS))
			return;

		// Centre of the froxel in (uv, w) parameter space.
		const float3 uvw = (float3(dtid) + 0.5f) / float3(LUT_DIMS);

		// NDC.xy from screen UV (D3D convention: y flipped). We use the
		// far plane (ndc.z=1) to compute the view ray direction, then
		// scale that direction by the froxel's depth to find the integration
		// endpoint.
		const float2 ndcXY = float2(uvw.x * 2.0f - 1.0f, 1.0f - uvw.y * 2.0f);
		const float4 farH  = mul(float4(ndcXY, 1.0f, 1.0f), g_viewProjectionMatrixInverse);
		const float3 worldFar = farH.xyz / max(farH.w, 1e-6f);
		const float3 rayDir   = normalize(worldFar - g_eyePos.xyz);

		const float depthM = uvw.z * MAX_DIST_M;
		const float dtM    = depthM / float(MARCH_STEPS_PER_FROXEL);
		const float dtMM   = dtM * 1e-6f;

		// Sky-aligned sun direction: matches the SkyView LUT generator's
		// convention. Symmetric around the sun zenith axis so we only need
		// the elevation (g_sunDirection.y); the horizontal direction lives
		// along +X in the local atmosphere frame.
		const float sunCosZenithLocal = clamp(g_sunDirection.y, -1.0f, 1.0f);
		const float sunSinZenithLocal = sqrt(max(0.0f, 1.0f - sunCosZenithLocal * sunCosZenithLocal));
		const float3 sunDirLocal = float3(sunSinZenithLocal, sunCosZenithLocal, 0.0f);

		const float cosViewSun = dot(rayDir, g_sunDirection);
		const float phaseR     = RayleighPhaseAtm(cosViewSun);
		const float phaseM     = MiePhaseCornetteShanksAtm(cosViewSun, g_miePhaseG);

		float3 luminance     = 0.0f.xxx;
		float3 transmittance = 1.0f.xxx;

		// Trapezoidal mid-point march: sample at the centre of each segment
		// to avoid double-counting endpoints between adjacent froxels.
		[loop]
		for (uint s = 0u; s < MARCH_STEPS_PER_FROXEL; ++s)
		{
			const float t = (float(s) + 0.5f) * dtM;
			const float3 pWorld = g_eyePos.xyz + rayDir * t;
			const float3 pAtm   = WorldToAtmosphere(pWorld);

			const AtmosphereSamplePoint sp = SampleAtmosphereMedium(pAtm);

			const float pAlt    = length(pAtm);
			const float pSunCos = dot(normalize(pAtm), sunDirLocal);
			const float3 sunTransmittance = SampleTransmittance(pAlt, pSunCos);
			const float3 msContribution   = SampleMultiScattering(pAlt, pSunCos);

			const float3 scatterPhased = sp.rayleighScattering * phaseR
			                           + sp.mieScattering.xxx * phaseM;
			const float3 isoScatter    = sp.rayleighScattering + sp.mieScattering.xxx;

			const float3 inscatter = (scatterPhased * sunTransmittance + isoScatter * msContribution) * g_sunIntensity;

			const float3 sampleTransmittance = exp(-sp.extinction * dtMM);
			const float3 stepGain = (1.0f.xxx - sampleTransmittance) / max(sp.extinction, 1e-6f.xxx);
			luminance += transmittance * inscatter * stepGain;

			transmittance *= sampleTransmittance;
		}

		// Pack the scalar transmittance into .a. We pick the luminance-
		// weighted channel mean so the apply pass can use one channel as
		// the "how much of the original beauty pixel survives". A per-
		// channel transmittance would be more accurate but doubles
		// storage; the perceptual difference for AP is small.
		const float aOut = dot(transmittance, float3(0.2126f, 0.7152f, 0.0722f));
		g_aerialPerspectiveVolume[dtid] = float4(luminance, aOut);
	}
}
