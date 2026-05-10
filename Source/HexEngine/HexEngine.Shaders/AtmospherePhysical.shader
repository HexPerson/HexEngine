"GlobalIncludes"
{
	Global
}
"Global"
{
	static const float ATM_PI = 3.14159265359f;

	struct PhysicalAtmosphereSample
	{
		float3 inscatter;
		float3 transmittance;
	};

	static const float ATMOSPHERE_BASE_HEIGHT = 0.0f;
	static const float ATMOSPHERE_OZONE_CENTER = 240.0f;
	static const float ATMOSPHERE_OZONE_HALF_WIDTH = 220.0f;

	float3 AtmosphereBetaO()
	{
		// Subtle ozone-like absorption. Keep it gentle to avoid magenta/purple skies.
		return float3(0.00045f, 0.00095f, 0.00012f);
	}

	float RayleighPhase(float mu)
	{
		return (3.0f / (16.0f * ATM_PI)) * (1.0f + mu * mu);
	}

	float MiePhaseHG(float mu, float g)
	{
		float g2 = g * g;
		float denom = pow(max(1e-3f, 1.0f + g2 - 2.0f * g * mu), 1.5f);
		return (1.0f - g2) / (4.0f * ATM_PI * denom);
	}

	void AtmosphereDensitiesAtPos(float3 pos, out float rayleighD, out float mieD)
	{
		// Flat-earth style density profile (engine world-space friendly).
		float h = max(0.0f, pos.y - ATMOSPHERE_BASE_HEIGHT);
		float Hr = max(10.0f, 260.0f); // Rayleigh scale height in engine units
		float Hm = max(4.0f, 70.0f);   // Mie scale height in engine units
		rayleighD = exp(-h / Hr);
		mieD = exp(-h / Hm);
	}

	float3 AtmosphereBetaR()
	{
		// Rayleigh coefficients tuned for a cleaner, less stylized daytime sky.
		float densityScale = max(0.02f, g_atmosphere.density);
		float rayleighStrength = max(0.0f, g_atmosphere.rayleighStrength);
		return float3(0.0046f, 0.0092f, 0.0154f) * densityScale * rayleighStrength;
	}

	float AtmosphereBetaM()
	{
		float densityScale = max(0.02f, g_atmosphere.density);
		float mieStrength = max(0.0f, g_atmosphere.mieStrength);
		return 0.0088f * densityScale * mieStrength;
	}

	float3 AtmosphereExtinction(float rayleighD, float mieD, float ozoneD)
	{
		float3 betaR = AtmosphereBetaR();
		float betaM = AtmosphereBetaM();
		return betaR * rayleighD + betaM * mieD + AtmosphereBetaO() * ozoneD;
	}

	float AtmosphereOzoneDensityAtPos(float3 pos)
	{
		return saturate(1.0f - abs((pos.y - ATMOSPHERE_BASE_HEIGHT) - ATMOSPHERE_OZONE_CENTER) / ATMOSPHERE_OZONE_HALF_WIDTH);
	}

	float3 ComputePhysicalSunTransmittance(float3 samplePos, float3 sunDir)
	{
		float rayleighD, mieD;
		AtmosphereDensitiesAtPos(samplePos, rayleighD, mieD);
		float ozoneD = AtmosphereOzoneDensityAtPos(samplePos);

		float3 betaR = AtmosphereBetaR();
		float betaM = AtmosphereBetaM();
		float3 betaO = AtmosphereBetaO();

		float sunPath = lerp(140.0f, 24.0f, saturate(sunDir.y * 0.5f + 0.5f));
		float3 sunSigma = betaR * rayleighD + betaM * mieD + betaO * ozoneD;
		return exp(-sunSigma * sunPath);
	}

	float3 ComputePhysicalSunColour(float3 samplePos, float3 sunDir)
	{
		float sunEnergy = lerp(18.0f, 30.0f, saturate(sunDir.y * 0.5f + 0.5f)) * max(g_globalLight[0], 0.35f);
		float sunsetAmount = saturate((0.22f - sunDir.y) / 0.32f);
		float3 solarRadiance = lerp(float3(1.0f, 0.985f, 0.965f), float3(1.0f, 0.58f, 0.20f), sunsetAmount * 0.92f);
		float3 transmittance = ComputePhysicalSunTransmittance(samplePos, sunDir);
		transmittance.g = lerp(transmittance.g, transmittance.g * 0.48f, sunsetAmount);
		transmittance.b = lerp(transmittance.b, transmittance.b * 0.10f, sunsetAmount);
		return solarRadiance * transmittance * sunEnergy;
	}

	PhysicalAtmosphereSample IntegrateAtmospherePhysical(
		float3 rayOrigin,
		float3 rayDir,
		float tMax,
		float3 sunDir,
		int numSteps,
		bool addSunDisk)
	{
		PhysicalAtmosphereSample result;
		result.inscatter = float3(0.0f, 0.0f, 0.0f);
		result.transmittance = float3(1.0f, 1.0f, 1.0f);

		if (tMax <= 0.001f || numSteps <= 0)
			return result;

		float g = clamp(g_atmosphere.anisotropicIntensity * 0.35f, -0.85f, 0.85f);
		float dt = tMax / (float)numSteps;
		float t = dt * 0.5f;
		float mu = dot(rayDir, sunDir);
		float phaseR = RayleighPhase(mu);
		float phaseM = MiePhaseHG(mu, g);

		float3 betaR = AtmosphereBetaR();
		float betaM = AtmosphereBetaM();
		float ambientStrength = max(0.0f, g_atmosphere.ambientStrength);
		float sunHazeStrength = max(0.0f, g_atmosphere.sunHazeStrength);
		float3 ambientSky = max(lerp(float3(0.14f, 0.15f, 0.18f), g_atmosphere.ambientLight.rgb, 0.32f), float3(0.04f, 0.045f, 0.05f));

		[loop]
		for (int i = 0; i < numSteps; ++i)
		{
			float3 p = rayOrigin + rayDir * t;
			float rayleighD, mieD;
			AtmosphereDensitiesAtPos(p, rayleighD, mieD);
			float ozoneD = AtmosphereOzoneDensityAtPos(p);

			float3 sigmaE = AtmosphereExtinction(rayleighD, mieD, ozoneD);
			float3 localScatter = betaR * rayleighD * phaseR + betaM * mieD * phaseM * sunHazeStrength;

			float3 lightAtten = ComputePhysicalSunTransmittance(p, sunDir);
			float3 ambientScatter = ambientSky * (betaR * rayleighD * 0.080f + betaM * mieD * 0.045f) * ambientStrength;
			float3 stepInscatter = (localScatter * (lightAtten * max(g_globalLight[0], 0.35f) * 19.4f) + ambientScatter) * dt;

			result.inscatter += result.transmittance * stepInscatter;
			result.transmittance *= exp(-sigmaE * dt);
			t += dt;
		}

		if (addSunDisk)
		{
			float sunDisk = smoothstep(0.9996f, 0.99992f, mu);
			float3 sunColour = ComputePhysicalSunColour(rayOrigin, sunDir);
			result.inscatter += sunColour * sunDisk * 0.8f;
		}

		return result;
	}
}
