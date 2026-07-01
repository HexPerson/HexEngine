"GlobalIncludes"
{
	Global
}
"Global"
{
	// Hillaire 2020 ("A Scalable and Production Ready Sky and Atmosphere
	// Rendering Technique") style spherical-earth atmosphere model.
	//
	// Replaces the flat-earth approximation in AtmospherePhysical.shader.
	// All distances here are in MEGAMETRES (Mm = 1000 km) - the standard
	// scale for atmospheric models. Doing this at planet scale in metres
	// chews float precision; Mm gives the integrators comfortable headroom.
	//
	// Plumbing world position into atmospheric coordinates:
	//   atmospherePos.y = groundRadiusMM + (worldPos.y in metres) / 1e6
	// i.e. the camera sits at altitude (cameraY metres) above the ground
	// shell, but the shell itself starts at groundRadiusMM. The helpers
	// below expect inputs already in this convention - see
	// WorldYToAtmosphereAltitudeMM() at the bottom.
	//
	// LUT parameterisations (used by Phase B):
	//   TransmittanceLUT  (256x64):
	//     u  = sun-zenith-cosine remap  -> safeacos compressed near horizon
	//     v  = altitude in [groundRadius, topRadius] -> sqrt-compressed
	//   MultiScatteringLUT (32x32):
	//     u  = sun-zenith-cosine
	//     v  = altitude in [groundRadius, topRadius]
	//   SkyViewLUT (192x108):
	//     u  = sun-azimuth-relative camera view azimuth
	//     v  = view-zenith-cosine with horizon-bias remap

	static const float kATM_PI = 3.14159265359f;

	// Planet & atmosphere shell in Megametres.
	static const float groundRadiusMM = 6.360f;
	static const float topRadiusMM    = 6.460f;   // 100 km shell - safe headroom over the standard 60-80 km nominal

	// Density scale heights (Mm). Rayleigh ~8 km, Mie ~1.2 km.
	static const float rayleighScaleHeightMM = 0.008f;
	static const float mieScaleHeightMM      = 0.0012f;

	// Ozone tent profile - peak absorption near 25 km altitude.
	static const float ozoneCentreMM    = 0.025f;
	static const float ozoneHalfWidthMM = 0.015f;

	// Scattering / extinction coefficients are NO LONGER static here. They
	// must be supplied at call time via SampleAtmosphereMediumWithCoefs()
	// because the engine drives them per-frame from C++ via the cbuffer
	// at b5 (see AtmosphereLUTs::Params and the weather plugin's
	// env_density / env_rayleighStrength / env_mieStrength HVars). Each
	// LUT shader wraps SampleAtmosphereMediumWithCoefs() in a local
	// SampleAtmosphereMedium() that forwards the cbuffer values, so call
	// sites read the same as before but the values flow through the
	// runtime cbuffer instead of compiled-in constants.

	struct AtmosphereSamplePoint
	{
		float3 rayleighScattering; // per-Mm at this altitude
		float  mieScattering;      // per-Mm at this altitude
		float3 extinction;         // per-Mm at this altitude (rayleighSc + mieEx + ozoneAbs)
	};

	// Returns altitude above ground in Mm given a position in atmosphere
	// space (where length(pos) is distance from planet centre in Mm).
	float AtmosphereAltitudeMM(float3 pos)
	{
		return max(0.0f, length(pos) - groundRadiusMM);
	}

	// Sample the medium properties at `pos`. Coefficients are passed in
	// (not declared as statics) so each shader can route them from its
	// own cbuffer-driven values. See the wrapper SampleAtmosphereMedium()
	// in the three LUT generators.
	AtmosphereSamplePoint SampleAtmosphereMediumWithCoefs(
		float3 pos,
		float3 rayleighScatterPerMM,
		float  mieScatterPerMM,
		float  mieExtinctPerMM,
		float3 ozoneAbsorbPerMM)
	{
		const float altMM = AtmosphereAltitudeMM(pos);

		const float rayleighDensity = exp(-altMM / rayleighScaleHeightMM);
		const float mieDensity      = exp(-altMM / mieScaleHeightMM);
		// Tent profile clamped to [0, 1].
		const float ozoneDensity    = max(0.0f, 1.0f - abs(altMM - ozoneCentreMM) / ozoneHalfWidthMM);

		AtmosphereSamplePoint s;
		s.rayleighScattering = rayleighScatterPerMM * rayleighDensity;
		s.mieScattering      = mieScatterPerMM      * mieDensity;
		s.extinction         = s.rayleighScattering
		                     + mieExtinctPerMM * mieDensity
		                     + ozoneAbsorbPerMM * ozoneDensity;
		return s;
	}

	// Solve for the parametric t along (origin + t*dir) where the ray exits
	// a sphere of given radius centred at the origin. Returns the larger
	// root (the exit hit); negative if no intersection or behind origin.
	// Both origin and radius in Mm.
	float RaySphereIntersectExit(float3 origin, float3 dir, float radius)
	{
		const float b = dot(origin, dir);
		const float c = dot(origin, origin) - radius * radius;
		const float disc = b * b - c;
		if (disc < 0.0f)
			return -1.0f;
		const float sq = sqrt(disc);
		return -b + sq;
	}

	// As above, but returns the nearer root (entry hit). Used for ground-
	// intersection tests during LUT precomputation - a sun ray that hits the
	// ground has zero transmittance through that shell.
	float RaySphereIntersectEntry(float3 origin, float3 dir, float radius)
	{
		const float b = dot(origin, dir);
		const float c = dot(origin, origin) - radius * radius;
		const float disc = b * b - c;
		if (disc < 0.0f)
			return -1.0f;
		const float sq = sqrt(disc);
		const float t = -b - sq;
		return t;
	}

	// Rayleigh + Henyey-Greenstein-approximated Mie phase functions.
	// mu = cos(angle between view dir and light dir).
	float RayleighPhaseAtm(float mu)
	{
		return (3.0f / (16.0f * kATM_PI)) * (1.0f + mu * mu);
	}

	float MiePhaseCornetteShanksAtm(float mu, float g)
	{
		// Cornette-Shanks - better forward peak than plain HG, same single
		// scalar tunable. g in (-1, 1); positive = forward scattering.
		const float g2 = g * g;
		const float num = 3.0f * (1.0f - g2) * (1.0f + mu * mu);
		const float den = 8.0f * kATM_PI * (2.0f + g2) * pow(max(1e-3f, 1.0f + g2 - 2.0f * g * mu), 1.5f);
		return num / den;
	}

	// World-space y (in engine metres) → atmosphere-space Y (Mm,
	// referenced from planet centre). The engine's "ground" lives at y=0,
	// so we add the planet radius. Used by callers that want to lift a
	// world position into the atmosphere model without picking a
	// hemisphere/origin convention.
	float WorldYToAtmosphereAltitudeMM(float worldY_metres)
	{
		return groundRadiusMM + worldY_metres * 1e-6f;
	}

	// ---------------------------------------------------------------------
	// LUT sample helpers (Phase B). Shaders that want to read the
	// precomputed LUTs declare the textures themselves at whatever t-slots
	// suit them and then call these helpers to do the UV remapping. Kept
	// out of the texture declaration so the same helpers compose with
	// different sky / aerial-perspective / scattering binding setups.
	// ---------------------------------------------------------------------

	float2 TransmittanceLutParamsToUv(float viewHeight, float viewZenithCos)
	{
		const float H   = sqrt(max(0.0f, topRadiusMM * topRadiusMM - groundRadiusMM * groundRadiusMM));
		const float rho = sqrt(max(0.0f, viewHeight * viewHeight - groundRadiusMM * groundRadiusMM));
		const float discriminant = viewHeight * viewHeight * (viewZenithCos * viewZenithCos - 1.0f) + topRadiusMM * topRadiusMM;
		const float d = max(0.0f, -viewHeight * viewZenithCos + sqrt(max(0.0f, discriminant)));
		const float dMin = topRadiusMM - viewHeight;
		const float dMax = rho + H;
		const float xMu = (dMax - dMin) > 1e-6f ? (d - dMin) / (dMax - dMin) : 0.0f;
		const float xR  = H > 1e-6f ? (rho / H) : 0.0f;
		return float2(saturate(xMu), saturate(xR));
	}

	float2 MultiScatteringLutParamsToUv(float viewHeight, float sunCosZenith)
	{
		const float u = sunCosZenith * 0.5f + 0.5f;
		const float v = saturate((viewHeight - groundRadiusMM) / max(topRadiusMM - groundRadiusMM, 1e-6f));
		return float2(u, v);
	}

	// SkyView LUT UV from a world-space view direction and sun direction.
	// view zenith uses the horizon-bias remap (cos^2 split at v=0.5) so
	// the sky stays sharp at the horizon. azimuth wraps cleanly at 0/1.
	float2 SkyViewLutParamsToUv(float3 viewDir, float3 sunDir)
	{
		const float cosZenith = clamp(viewDir.y, -1.0f, 1.0f);

		// View horizontal length; goes to zero at the zenith/nadir poles.
		// We need it to compute azimuth (atan2 of perpendicular over
		// parallel components against the sun's horizontal direction).
		// At the poles the azimuth is geometrically undefined - all
		// azimuth values converge to the same direction. Previously we
		// added small epsilons inside the normalize() to dodge a NaN,
		// but the epsilons dominated near the poles and snapped u to a
		// discontinuous value depending on which side of zero viewDir.xz
		// happened to land on. That produced a hard dark spot at the top
		// of the sky sphere where adjacent pixels sampled wildly
		// different texels of the top LUT row.
		const float viewHorizLen = sqrt(max(0.0f, 1.0f - cosZenith * cosZenith));

		float u;
		if (viewHorizLen < 1e-4f)
		{
			// Pole case: u undefined. Sky is rotationally symmetric here
			// so any consistent value works; pick 0 so the whole pole
			// resolves to a single LUT column.
			u = 0.0f;
		}
		else
		{
			// Normalise the in-plane components ourselves so we don't pay
			// the epsilon hit. Same handling for the sun side, with a
			// fallback when the sun is directly overhead/below.
			const float invView = 1.0f / viewHorizLen;
			const float vX = viewDir.x * invView;
			const float vZ = viewDir.z * invView;

			const float sunHorizLen = sqrt(sunDir.x * sunDir.x + sunDir.z * sunDir.z);
			float sX, sZ;
			if (sunHorizLen < 1e-4f)
			{
				// Sun at zenith/nadir: there's no "sun azimuth" to measure
				// against either. Sky becomes azimuth-symmetric so any
				// consistent reference works.
				sX = 1.0f; sZ = 0.0f;
			}
			else
			{
				const float invSun = 1.0f / sunHorizLen;
				sX = sunDir.x * invSun;
				sZ = sunDir.z * invSun;
			}

			float azimuth = atan2(vX * sZ - vZ * sX, vX * sX + vZ * sZ);
			if (azimuth < 0.0f)
				azimuth += 2.0f * kATM_PI;
			u = azimuth / (2.0f * kATM_PI);
		}

		// Inverse of the SkyViewLUT generator's UV->cosZenith mapping.
		float v;
		if (cosZenith >= 0.0f)
		{
			v = 0.5f - 0.5f * sqrt(cosZenith);
		}
		else
		{
			v = 0.5f + 0.5f * sqrt(-cosZenith);
		}
		return float2(u, saturate(v));
	}
}
