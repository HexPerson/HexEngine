"GlobalIncludes"
{
	Global
}
"Global"
{
	static const float pi = 3.14159265359;
	static const float invPi = 1.0 / pi;

	static const float zenithOffset = -0.05;
	static const float multiScatterPhase = 0.1;
	//static const float density = 0.5;

	//static const float anisotropicIntensity = 1.0; //Higher numbers result in more anisotropic scattering

	//static const float3 skyColor = float3(0.39, 0.57, 1.0) * (1.0 + g_atmosphere.anisotropicIntensity); //Make sure one of the conponents is never 0.0

#define smooth(x) x*x*(3.0-2.0*x)
#define zenithDensity(x) g_atmosphere.density / pow(max(x - zenithOffset, 0.35e-2), 0.75)

	float3 getSkyAbsorption(float3 x, float y)
	{
		float3 absorption = x * -y;
		absorption = exp2(absorption) * g_atmosphere.zenithExponent;

		return absorption;
	}

	float getSunPoint(float2 p, float2 lp, float aspect)
	{
		float2 hyp = p - lp;
		hyp.x *= aspect;

		return smoothstep(0.04, 0.023, length(hyp)) * 50.0;
	}

	float getRayleigMultiplier(float2 p, float2 lp, float aspect)
	{
		float2 hyp = p - lp;
		hyp.x *= aspect;

		return 1.0 + pow(1.0 - clamp(length(hyp), 0.0, 1.0), 2.0) * pi * 0.5;
	}

	float getMie(float2 p, float2 lp, float aspect)
	{
		float2 hyp = p - lp;
		hyp.x *= aspect;

		float disk = clamp(1.0 - pow(length(hyp), 0.045), 0.0, 1.0);

		return disk * disk * (3.0 - 2.0 * disk) * 2.0 * pi;
	}

	float3 getAtmosphericScattering(float skyHeight, float gradientPos, float2 p, float2 lp, float aspect, bool doSun)
	{
		float fixedSunHeight = skyHeight;

		//if (fixedSunHeight < -0.15f)
		//	fixedSunHeight = -0.15f;

		//fixedSunHeight *= 2.0f;

		float3 skyColor = float3(0.39, 0.57, 1.0) * (1.0 + g_atmosphere.anisotropicIntensity);

		//gradientPos /= 2.0f;

		//fixedSunHeight /= 2.0f;

		float2 correctedLp = lp;// / max(iResolution.x, iResolution.y) * iResolution.xy;

		float zenith = zenithDensity(gradientPos);
		float sunPointDistMult = clamp(length(max(fixedSunHeight + multiScatterPhase - zenithOffset, 0.0)), 0.0, 1.0);

		float rayleighMult = getRayleigMultiplier(p, correctedLp, aspect);

		float3 absorption = getSkyAbsorption(skyColor, zenith);
		float3 sunAbsorption = getSkyAbsorption(skyColor, zenithDensity(fixedSunHeight + multiScatterPhase));
		float3 sky = skyColor * zenith * rayleighMult;
		float3 sun = doSun ? (getSunPoint(p, correctedLp, aspect) * absorption) : float3(0, 0, 0);
		float3 mie = doSun ? (getMie(p, correctedLp, aspect) * sunAbsorption) : float3(0, 0, 0);

		float3 totalSky = lerp(sky * absorption, sky / (sky + 0.5), sunPointDistMult);
		totalSky += sun + mie;
		totalSky *= sunAbsorption * 0.5 + 0.5 * length(sunAbsorption);

		return totalSky;
	}

	float3 getAtmosphereColour(float worldPos, float2 p, float2 lp, float aspect, bool doSun)
	{
		if(worldPos < 0.0f)
			worldPos = 0.0f;

		float fixedSunHeight = -g_lightDirection.y;// / 2500.0f;

		//if (fixedSunHeight < -0.15f)
		//	fixedSunHeight = -0.15f;

		//fixedSunHeight *= 2.0f;

		float2 correctedLp = lp;

		float3 skyColor = float3(0.39, 0.57, 1.0) * (1.0 + g_atmosphere.anisotropicIntensity);

		float zenith = zenithDensity(worldPos);
		float sunPointDistMult = clamp(length(max(fixedSunHeight + multiScatterPhase - zenithOffset, 0.0)), 0.0, 1.0);

		//float rayleighMult = getRayleigMultiplier(p, correctedLp, aspect);

		float3 absorption = getSkyAbsorption(skyColor, zenith);
		float3 sunAbsorption = getSkyAbsorption(skyColor, zenithDensity(fixedSunHeight + multiScatterPhase));
		float3 sky = skyColor * zenith;// *rayleighMult;
		float3 mie = doSun ? (getMie(p, correctedLp, aspect) * sunAbsorption) : float3(0, 0, 0);

		float3 totalSky = lerp(sky * absorption, sky / (sky + 0.5), sunPointDistMult);
			   totalSky += mie;
			   totalSky *= sunAbsorption * 0.5 + 0.5 * length(sunAbsorption);

		return totalSky;
	}

	float3 jodieReinhardTonemap(float3 c)
	{
		float l = dot(c, float3(0.2126, 0.7152, 0.0722));
		float3 tc = c / (c + 1.0);

		return lerp(c / (l + 1.0), tc, tc);
	}

	float3 getSunColour()
	{
		//return float3(188.0f / 255.0f, 207.0f / 255.0f, 187.0f / 255.0f);

		float fixedSunHeight = -g_lightDirection.y;// / 2500.0f;

		//if (fixedSunHeight < -0.15f)
		//	fixedSunHeight = -0.15f;

		//fixedSunHeight *= 2.0f;

		float sunPointDistMult = clamp(length(max(fixedSunHeight + multiScatterPhase - zenithOffset, 0.0)), 0.0, 1.0);

		float3 skyColor = float3(0.39, 0.57, 1.0) * (1.0 + g_atmosphere.anisotropicIntensity);

		float3 sunAbsorption = getSkyAbsorption(skyColor, zenithDensity(fixedSunHeight + multiScatterPhase));

		float zenith = zenithDensity(fixedSunHeight);
		float3 absorption = getSkyAbsorption(skyColor, zenith);
		float3 sky = skyColor * zenith;
		float3 sun = absorption * 30.0f;

		float3 totalSky = sun;// = lerp(sky * absorption, sky / (sky + 0.5), sunPointDistMult);
		//totalSky += sun;
		//	totalSky *= sunAbsorption * 0.5 + 0.5 * length(sunAbsorption);

		totalSky = jodieReinhardTonemap(totalSky);
		totalSky = pow(totalSky, float3(2.2f.xxx));

		return totalSky;
	}	
}
