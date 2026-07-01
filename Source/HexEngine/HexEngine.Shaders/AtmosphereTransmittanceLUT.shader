"ComputeShaderIncludes"
{
	Global
	AtmosphereCommon
}
"ComputeShader"
{
	// Hillaire 2020 transmittance LUT generator.
	//
	// Per-texel: solve for the transmittance from a sample point at
	// (viewHeight, viewZenithCos) to the top of the atmosphere shell.
	// This LUT is sampled by every other atmosphere pass for "how much
	// of the sun is left at this altitude looking in this direction" -
	// the most-touched piece of the LUT stack and so the one whose
	// parameterisation has to be carefully chosen for accuracy near the
	// horizon, where transmittance falls off rapidly.
	//
	// Parameterisation (Hillaire, §5.1):
	//   u = xMu = remap of view-zenith-cosine through a horizon-distance
	//             function so most of the texel budget lands on the
	//             grazing-angle range. See UvToTransmittanceLutParams().
	//   v = xR  = altitude remap, sqrt-distributed in [groundRadius,
	//             topRadius] - puts more texel density near the ground
	//             where transmittance changes most rapidly with altitude.
	//
	// Output format: RGBA16F. RGB = transmittance, A = 1.0 (unused).

	RWTexture2D<float4> g_transmittanceLUT : register(u0);

	// Atmosphere param cbuffer. The shader's hard-coded radii in
	// AtmosphereCommon.shader are the source of truth for now; this
	// cbuffer is bound for forward compatibility once the C++ layer
	// starts driving stylised tunables.
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

	static const uint2 LUT_SIZE = uint2(256u, 64u);
	static const uint  TRANSMITTANCE_STEPS = 40u;

	// Forward cbuffer-driven coefficients into the common helper. Allows
	// the runtime AtmosphereParams (which the weather plugin writes via
	// env_density / env_rayleighStrength / env_mieStrength) to actually
	// affect this LUT - previously the static-const values in
	// AtmosphereCommon were used regardless of the cbuffer.
	AtmosphereSamplePoint SampleAtmosphereMedium(float3 pos)
	{
		return SampleAtmosphereMediumWithCoefs(
			pos,
			g_rayleighScatteringPerMM,
			g_mieScatteringPerMM,
			g_mieExtinctionPerMM,
			g_ozoneAbsorptionPerMM);
	}

	// UV -> (viewHeight, viewZenithCos) per Hillaire §5.1. Both inputs
	// derived from a horizon-distance parameterisation so the LUT keeps
	// accuracy at grazing sun angles where extinction matters most.
	void UvToTransmittanceLutParams(float2 uv, out float viewHeight, out float viewZenithCos)
	{
		const float xMu = uv.x;
		const float xR  = uv.y;

		const float H   = sqrt(topRadiusMM * topRadiusMM - groundRadiusMM * groundRadiusMM);
		const float rho = H * xR;

		viewHeight = sqrt(rho * rho + groundRadiusMM * groundRadiusMM);

		const float dMin = topRadiusMM - viewHeight;
		const float dMax = rho + H;
		const float d    = dMin + xMu * (dMax - dMin);

		viewZenithCos = (d <= 1e-6f) ? 1.0f : ((H * H - rho * rho - d * d) / (2.0f * viewHeight * d));
		viewZenithCos = clamp(viewZenithCos, -1.0f, 1.0f);
	}

	// Trapezoidal ray-march of extinction along (origin, dir) for tMax.
	// Returns the total transmittance exp(-integrated extinction).
	float3 IntegrateTransmittance(float3 origin, float3 dir, float tMax, uint steps)
	{
		float3 opticalDepth = 0.0f.xxx;
		const float dt = tMax / (float)steps;

		// Trapezoidal: half-weight on endpoints, full on the interior. Cheap
		// quality bump over plain midpoint for the same step count.
		[loop]
		for (uint i = 0u; i <= steps; ++i)
		{
			const float t = (float)i * dt;
			const float3 p = origin + dir * t;
			const AtmosphereSamplePoint sp = SampleAtmosphereMedium(p);
			const float weight = (i == 0u || i == steps) ? 0.5f : 1.0f;
			opticalDepth += sp.extinction * weight;
		}

		return exp(-opticalDepth * dt);
	}

	[numthreads(8, 8, 1)]
	void ShaderMain(uint3 dtid : SV_DispatchThreadID)
	{
		if (any(dtid.xy >= LUT_SIZE))
			return;

		// Sample the texel CENTRE so the LUT covers [0, 1] continuously when
		// sampled bilinearly downstream.
		const float2 uv = (float2(dtid.xy) + 0.5f) / float2(LUT_SIZE);

		float viewHeight, viewZenithCos;
		UvToTransmittanceLutParams(uv, viewHeight, viewZenithCos);

		// Origin sits on the +Y axis at altitude viewHeight; the view
		// direction lives in the YZ plane. The LUT is rotationally
		// symmetric so any choice of azimuth here is fine.
		const float3 origin = float3(0.0f, viewHeight, 0.0f);
		const float  sinTheta = sqrt(max(0.0f, 1.0f - viewZenithCos * viewZenithCos));
		const float3 dir = float3(sinTheta, viewZenithCos, 0.0f);

		// March to the atmosphere top. If the ray hits the ground first,
		// transmittance is zero - the sun is below the horizon for this
		// (altitude, angle) pair.
		const float tTop    = RaySphereIntersectExit(origin, dir, topRadiusMM);
		const float tGround = RaySphereIntersectEntry(origin, dir, groundRadiusMM);
		const bool  hitsGround = tGround > 0.0f;
		const float tMax    = hitsGround ? tGround : tTop;

		float3 transmittance = 0.0f.xxx;
		if (tMax > 0.0f && !hitsGround)
			transmittance = IntegrateTransmittance(origin, dir, tMax, TRANSMITTANCE_STEPS);

		g_transmittanceLUT[dtid.xy] = float4(transmittance, 1.0f);
	}
}
