"GlobalIncludes"
{
	Global
}
"Global"
{
	static const float3 f0 = float3(0.04, 0.04, 0.04);
	static const float MinRoughness = 0.04;
	static const float PI = 3.141592653589793;

	/*float3 getIBLContribution(float perceptualRoughness, float NdotV, float3 diffuseColor, float3 specularColor, float3 n, float3 reflection)
	{
		const float lod = perceptualRoughness * NumSpecularMipLevels;

		const float3 brdf = BRDFTexture.Sample(BRDFSampler, float2(NdotV, 1.0 - perceptualRoughness)).rgb;

		const float3 diffuseLight = DiffuseTexture.Sample(IBLSampler, n).rgb;
		const float3 specularLight = SpecularTexture.SampleLevel(IBLSampler, reflection, lod).rgb;

		const float3 diffuse = diffuseLight * diffuseColor;
		const float3 specular = specularLight * (specularColor * brdf.x + brdf.y);

		return diffuse + specular;
	}*/

	float3 diffuse(float3 diffuseColor)
	{
		return diffuseColor;// / PI;
	}

	float3 specularReflection(float3 reflectance0, float3 reflectance90, float VdotH)
	{
		return reflectance0 + (reflectance90 - reflectance0) * pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0);
	}

	float geometricOcclusion(float NdotL, float NdotV, float alphaRoughness)
	{
		const float attenuationL = 2.0 * NdotL / (NdotL + sqrt(alphaRoughness * alphaRoughness + (1.0 - alphaRoughness * alphaRoughness) * (NdotL * NdotL)));
		const float attenuationV = 2.0 * NdotV / (NdotV + sqrt(alphaRoughness * alphaRoughness + (1.0 - alphaRoughness * alphaRoughness) * (NdotV * NdotV)));
		return attenuationL * attenuationV;
	}

	float microfacetDistribution(float NdotH, float alphaRoughness)
	{
		const float roughnessSq = alphaRoughness * alphaRoughness;
		const float f = (NdotH * roughnessSq - NdotH) * NdotH + 1.0;
		return roughnessSq / (PI * f * f);
	}

	float ApplySpecularAntiAliasing(float3 normal, float perceptualRoughness)
	{
		const float3 dndx = ddx(normal);
		const float3 dndy = ddy(normal);
		const float normalVariance = max(dot(dndx, dndx), dot(dndy, dndy));
		const float kernelRoughness = saturate(normalVariance * 0.5f);
		return saturate(max(perceptualRoughness, sqrt(kernelRoughness)));
	}

	// Decode the per-material-model id from the features GBuffer .r channel. The
	// channel is an unorm encoding of (id / 4) (see DefaultPixel), so we recover
	// with a multiply + round. Tolerant to RGBA8 quantisation since adjacent ids
	// are 0.25 apart vs ~0.004 of quantisation noise.
	uint DecodeMaterialModelId(float r)
	{
		return (uint)floor(r * 4.0f + 0.5f);
	}

	// Anisotropic GGX normal distribution. Two roughness axes (alongTangent /
	// alongBitangent); when alphaX == alphaY this collapses to standard isotropic
	// GGX, so we use this for the aniso case only and let the base path keep the
	// cheaper isotropic version.
	float MicrofacetDistributionAniso(float NdotH, float TdotH, float BdotH, float alphaX, float alphaY)
	{
		const float ax2 = alphaX * alphaX;
		const float ay2 = alphaY * alphaY;
		const float denom = (TdotH * TdotH) / max(ax2, 1e-6f)
		                  + (BdotH * BdotH) / max(ay2, 1e-6f)
		                  + NdotH * NdotH;
		return 1.0f / max(PI * alphaX * alphaY * denom * denom, 1e-6f);
	}

	// Charlie sheen distribution (Estevez & Kulla 2017). Mimics retroreflective
	// fuzz - the bright rim on velvet / cloth / leaves at grazing angles.
	float SheenDistribution(float NdotH, float sheenRoughness)
	{
		const float invR = 1.0f / max(sheenRoughness, 0.04f);
		const float cos2 = NdotH * NdotH;
		const float sin2 = max(1.0f - cos2, 0.0f);
		return (2.0f + invR) * pow(sin2, invR * 0.5f) / (2.0f * PI);
	}

	// Add the extra BRDF lobes that the per-pixel shading model demands. For
	// MATERIAL_MODEL_STANDARD (0) and MATERIAL_MODEL_SSS (1) this is a no-op:
	// standard surfaces only use the base PBR lobes, and SSS is handled by the
	// screen-space SSS post-process (the underlying surface still shades as
	// standard PBR here).
	//
	// modelParams layout:
	//   Clearcoat:   x = strength [0,1], y = roughness [0,1]
	//   Anisotropic: x = anisotropy [0,1] mapped to [-1,1], y/z = tangent.xy
	//                (z reconstructed via sqrt). Strength scaled by x's magnitude.
	//   Sheen:       x = strength [0,1], y/z/w = sheen tint RGB
	//
	// All lobes return contribution PER LIGHT (multiplied by NdotL and the light
	// colour); the caller adds this on top of the base CalculatePBR result, so a
	// material can mix base PBR + clearcoat (clearcoat over a metal/paint base),
	// base PBR + sheen, etc.
	float3 ApplyMaterialFeatures(
		uint modelId,
		float4 modelParams,
		float3 normal,
		float3 viewDir,      // surface -> camera
		float3 lightDir,     // surface -> light (already normalised)
		float3 lightColor,
		float perceptualRoughness,
		float depthValue,
		float attenuation)
	{
		if (modelId == MATERIAL_MODEL_STANDARD || modelId == MATERIAL_MODEL_SSS)
			return 0.0f.xxx;

		const float3 V = normalize(viewDir);
		const float3 L = normalize(lightDir);
		const float3 H = normalize(L + V);
		const float NdotL = saturate(dot(normal, L));
		if (NdotL <= 0.0f)
			return 0.0f.xxx;
		const float NdotV = abs(dot(normal, V)) + 0.001f;
		const float NdotH = saturate(dot(normal, H));
		const float VdotH = saturate(dot(V, H));

		if (modelId == MATERIAL_MODEL_CLEARCOAT)
		{
			// Thin dielectric (IOR ~ 1.5, F0 = 0.04) layer sitting on top of the
			// base shading. Adds a sharp specular highlight even on rough/metallic
			// bases - the wet-coat / car-paint / lacquered-wood look.
			const float ccStrength = saturate(modelParams.x);
			if (ccStrength <= 0.0001f)
				return 0.0f.xxx;
			const float ccRoughness = max(lerp(0.02f, 0.6f, saturate(modelParams.y)), 0.04f);
			const float ccAlpha = ccRoughness * ccRoughness;
			const float ccF = 0.04f + 0.96f * pow(1.0f - VdotH, 5.0f);
			const float ccG = geometricOcclusion(NdotL, NdotV, ccAlpha);
			const float ccD = microfacetDistribution(NdotH, ccAlpha);
			const float ccSpec = (ccF * ccG * ccD) / (4.0f * NdotL * NdotV);
			return NdotL * lightColor * attenuation * depthValue * ccSpec * ccStrength;
		}

		if (modelId == MATERIAL_MODEL_ANISOTROPIC)
		{
			// Brushed-metal / hair / fabric-weave shading. Tangent provided in
			// modelParams.yz (xy, with z reconstructed); anisotropy strength in
			// modelParams.x (0=isotropic, 1=fully stretched along tangent).
			const float anisoStrength = saturate(modelParams.x);
			if (anisoStrength <= 0.0001f)
				return 0.0f.xxx;
			float3 tangent;
			tangent.xy = modelParams.yz;
			tangent.z = sqrt(saturate(1.0f - dot(tangent.xy, tangent.xy)));
			// Project onto the tangent plane so it lies on the surface.
			tangent = normalize(tangent - normal * dot(tangent, normal) + 1e-5f.xxx);
			const float3 bitangent = normalize(cross(normal, tangent));
			const float baseAlpha = max(perceptualRoughness * perceptualRoughness, 0.0016f);
			const float alphaX = max(baseAlpha * (1.0f + anisoStrength * 1.5f), 0.0016f);
			const float alphaY = max(baseAlpha * (1.0f - anisoStrength * 0.85f), 0.0016f);
			const float TdotH = dot(tangent, H);
			const float BdotH = dot(bitangent, H);
			const float D = MicrofacetDistributionAniso(NdotH, TdotH, BdotH, alphaX, alphaY);
			const float G = geometricOcclusion(NdotL, NdotV, max(alphaX, alphaY));
			const float F = 0.04f + 0.96f * pow(1.0f - VdotH, 5.0f);
			const float spec = (D * G * F) / (4.0f * NdotL * NdotV);
			// Subtract the equivalent isotropic specular so we only contribute the
			// anisotropic delta - the base PBR pass already laid down isotropic spec.
			const float isoD = microfacetDistribution(NdotH, baseAlpha);
			const float isoG = geometricOcclusion(NdotL, NdotV, baseAlpha);
			const float isoSpec = (isoD * isoG * F) / (4.0f * NdotL * NdotV);
			return NdotL * lightColor * attenuation * depthValue * max(spec - isoSpec, 0.0f) * anisoStrength;
		}

		if (modelId == MATERIAL_MODEL_SHEEN)
		{
			// Velvet / cloth / dust / foliage backscatter. Charlie distribution.
			const float sheenStrength = saturate(modelParams.x);
			if (sheenStrength <= 0.0001f)
				return 0.0f.xxx;
			const float3 sheenTint = saturate(float3(modelParams.y, modelParams.z, modelParams.w));
			// Anchor sheen roughness to surface roughness - rougher surfaces fuzz wider.
			const float sheenRoughness = max(perceptualRoughness, 0.1f);
			const float D = SheenDistribution(NdotH, sheenRoughness);
			// Neubelt & Pettineo cloth visibility term (avoids the energy spike at
			// grazing angles that the cook-torrance G gives for low NdotV).
			const float V_term = 1.0f / (4.0f * (NdotL + NdotV - NdotL * NdotV));
			const float3 sheenSpec = sheenTint * D * V_term;
			return NdotL * lightColor * attenuation * depthValue * sheenSpec * sheenStrength;
		}

		return 0.0f.xxx;
	}

	float3 CalculateLightningSurfaceLighting(
		float3 normal,
		float3 positionWorld,
		float3 baseColor,
		float perceptualRoughness,
		float metallic)
	{
		const float lightningFlash = saturate(g_weatherSurface.lightningFlash);
		if (lightningFlash <= 0.0001f)
			return 0.0f.xxx;

		const float3 lightningDir = normalize(g_weatherSurface.lightningBoltDirection.xyz + float3(1e-5f, 1e-5f, 1e-5f));
		const float3 V = normalize(g_eyePos.xyz - positionWorld);
		const float3 L = lightningDir;
		const float3 H = normalize(L + V);
		const float NdotL = saturate(dot(normal, L));
		const float NdotH = saturate(dot(normal, H));
		const float VdotH = saturate(dot(V, H));

		const float3 lightningColor = float3(0.64f, 0.78f, 1.0f);
		const float3 diffuseTerm = baseColor * (0.22f + 0.78f * (1.0f - metallic)) * NdotL;
		const float specTightness = lerp(56.0f, 16.0f, perceptualRoughness);
		const float fresnel = pow(1.0f - VdotH, 5.0f);
		const float specularTerm = pow(max(NdotH, 0.0f), specTightness) * (0.35f + 0.65f * (1.0f - perceptualRoughness) + fresnel * 0.45f);
		const float horizonLift = saturate(0.35f + 0.65f * lightningDir.y);
		return lightningColor * lightningFlash * horizonLift * (diffuseTerm * 0.55f + specularTerm * 0.85f);
	}

	float4 CalculatePBR(
		Texture2D materialTex,
		SamplerState samp,
		float2 TexCoord0,
		float3 normal,
		float3 PositionWorld,
		float3 LightDirection,
		float3 LightColor,
		float3 pixelColour,
		float depthValue,
		float attenuation
	)
	{
		// Roughness is stored in the 'g' channel, metallic is stored in the 'b' channel.
		// This layout intentionally reserves the 'r' channel for (optional) occlusion map data
		const float3 mrSample = materialTex.Sample(samp, TexCoord0);
		const float3 baseColor = pixelColour;
		const float metallic = saturate(mrSample.r);
		float perceptualRoughness = clamp(mrSample.g, MinRoughness, 1.0);
		perceptualRoughness = ApplySpecularAntiAliasing(normal, perceptualRoughness);

		// Roughness is authored as perceptual roughness; as is convention,
		// convert to material roughness by squaring the perceptual roughness [2].
		const float alphaRoughness = perceptualRoughness * perceptualRoughness;

		const float3 diffuseColor = (baseColor.rgb * (float3(1.0, 1.0, 1.0) - f0)) * (1.0 - metallic);
		const float3 specularColor = lerp(f0, baseColor.rgb, metallic);

		// Compute reflectance.
		const float reflectance = max(max(specularColor.r, specularColor.g), specularColor.b);

		// For typical incident reflectance range (between 4% to 100%) set the grazing reflectance to 100% for typical fresnel effect.
		// For very low reflectance range on highly diffuse objects (below 4%), incrementally reduce grazing reflecance to 0%.
		const float reflectance90 = saturate(reflectance * 25.0);
		const float3 specularEnvironmentR0 = specularColor.rgb;
		const float3 specularEnvironmentR90 = float3(1.0, 1.0, 1.0) * reflectance90;

		const float3 v = normalize(g_eyePos.xyz - PositionWorld);   // Vector from surface point to camera
		const float3 l = normalize(LightDirection);                           // Vector from surface point to light
		const float3 h = normalize(l + v);                                    // Half vector between both l and v
		const float3 reflection = -normalize(reflect(v, normal));

		const float NdotL = clamp(dot(normal, l), 0.001, 1.0);
		const float NdotV = abs(dot(normal, v)) + 0.001;
		const float NdotH = saturate(dot(normal, h));
		const float LdotH = saturate(dot(l, h));
		const float VdotH = saturate(dot(v, h));

		// Calculate the shading terms for the microfacet specular shading model
		const float3 F = specularReflection(specularEnvironmentR0, specularEnvironmentR90, VdotH);
		const float G = geometricOcclusion(NdotL, NdotV, alphaRoughness);
		const float D = microfacetDistribution(NdotH, alphaRoughness);

		// Calculation of analytical lighting contribution
		const float3 diffuseContrib = (1.0 - F) * diffuse(diffuseColor);
		const float3 specContrib = F * G * D / (4.0 * NdotL * NdotV);
		float3 color = NdotL * LightColor * attenuation * (diffuseContrib + specContrib) * depthValue;

		//if(depthValue > 0.0f)
		//color *= 1.3f;

		float3 ambient = pixelColour.rgb * g_atmosphere.ambientLight.rgb;

		color += ambient;
		color += CalculateLightningSurfaceLighting(normal, PositionWorld, baseColor, perceptualRoughness, metallic);

		// Calculate lighting contribution from image based lighting source (IBL)
		//color += getIBLContribution(perceptualRoughness, NdotV, diffuseColor, specularColor, n, reflection);

		return float4(color, 1.0f);
	}

	float4 CalculatePBRSurface(
		float metallic,
		float perceptualRoughness,
		float3 normal,
		float3 PositionWorld,
		float3 LightDirection,
		float3 LightColor,
		float3 pixelColour,
		float depthValue,
		float attenuation)
	{
		const float3 baseColor = pixelColour;
		metallic = saturate(metallic);
		perceptualRoughness = clamp(perceptualRoughness, MinRoughness, 1.0f);
		perceptualRoughness = ApplySpecularAntiAliasing(normal, perceptualRoughness);
		const float alphaRoughness = perceptualRoughness * perceptualRoughness;

		const float3 diffuseColor = (baseColor.rgb * (float3(1.0, 1.0, 1.0) - f0)) * (1.0 - metallic);
		const float3 specularColor = lerp(f0, baseColor.rgb, metallic);
		const float reflectance = max(max(specularColor.r, specularColor.g), specularColor.b);
		const float reflectance90 = saturate(reflectance * 25.0);
		const float3 specularEnvironmentR0 = specularColor.rgb;
		const float3 specularEnvironmentR90 = float3(1.0, 1.0, 1.0) * reflectance90;

		const float3 v = normalize(g_eyePos.xyz - PositionWorld);
		const float3 l = normalize(LightDirection);
		const float3 h = normalize(l + v);

		const float NdotL = clamp(dot(normal, l), 0.001, 1.0);
		const float NdotV = abs(dot(normal, v)) + 0.001;
		const float NdotH = saturate(dot(normal, h));
		const float VdotH = saturate(dot(v, h));

		const float3 F = specularReflection(specularEnvironmentR0, specularEnvironmentR90, VdotH);
		const float G = geometricOcclusion(NdotL, NdotV, alphaRoughness);
		const float D = microfacetDistribution(NdotH, alphaRoughness);

		const float3 diffuseContrib = (1.0 - F) * diffuse(diffuseColor);
		const float3 specContrib = F * G * D / (4.0 * NdotL * NdotV);
		float3 color = NdotL * LightColor * attenuation * (diffuseContrib + specContrib) * depthValue;
		color += pixelColour.rgb * g_atmosphere.ambientLight.rgb;
		color += CalculateLightningSurfaceLighting(normal, PositionWorld, baseColor, perceptualRoughness, metallic);
		return float4(color, 1.0f);
	}

	float4 CalculatePBRPointLighting(
		Texture2D materialTex,
		SamplerState samp,
		float2 TexCoord0,
		float3 normal,
		float3 PositionWorld,
		float3 LightDirection,
		float3 LightColor,
		float3 pixelColour,
		float depthValue,
		float attenuation
	)
	{
		// Roughness is stored in the 'g' channel, metallic is stored in the 'b' channel.
		// This layout intentionally reserves the 'r' channel for (optional) occlusion map data
		const float3 mrSample = materialTex.Sample(samp, TexCoord0);
		const float3 baseColor = pixelColour;
		const float metallic = saturate(mrSample.r);
		float perceptualRoughness = clamp(mrSample.g, MinRoughness, 1.0);
		perceptualRoughness = ApplySpecularAntiAliasing(normal, perceptualRoughness);

		// Roughness is authored as perceptual roughness; as is convention,
		// convert to material roughness by squaring the perceptual roughness [2].
		const float alphaRoughness = perceptualRoughness * perceptualRoughness;

		const float3 diffuseColor = (baseColor.rgb * (float3(1.0, 1.0, 1.0) - f0)) * (1.0 - metallic);
		const float3 specularColor = lerp(f0, baseColor.rgb, metallic);

		// Compute reflectance.
		const float reflectance = max(max(specularColor.r, specularColor.g), specularColor.b);

		// For typical incident reflectance range (between 4% to 100%) set the grazing reflectance to 100% for typical fresnel effect.
		// For very low reflectance range on highly diffuse objects (below 4%), incrementally reduce grazing reflecance to 0%.
		const float reflectance90 = saturate(reflectance * 25.0);
		const float3 specularEnvironmentR0 = specularColor.rgb;
		const float3 specularEnvironmentR90 = float3(1.0, 1.0, 1.0) * reflectance90;

		const float3 v = normalize(g_eyePos.xyz - PositionWorld);   // Vector from surface point to camera
		const float3 l = normalize(LightDirection);                           // Vector from surface point to light
		const float3 h = normalize(l + v);                                    // Half vector between both l and v
		const float3 reflection = -normalize(reflect(v, normal));

		const float NdotL = clamp(dot(normal, l), 0.001, 1.0);
		const float NdotV = abs(dot(normal, v)) + 0.001;
		const float NdotH = saturate(dot(normal, h));
		const float LdotH = saturate(dot(l, h));
		const float VdotH = saturate(dot(v, h));

		//return float4(NdotL, NdotL, NdotL, 1.0f);

		// Calculate the shading terms for the microfacet specular shading model
		const float3 F = specularReflection(specularEnvironmentR0, specularEnvironmentR90, VdotH);
		const float G = geometricOcclusion(NdotL, NdotV, alphaRoughness);
		const float D = microfacetDistribution(NdotH, alphaRoughness);

		// Calculation of analytical lighting contribution
		const float3 diffuseContrib = (1.0 - F) * diffuse(diffuseColor);
		const float3 specContrib = F * G * D / (4.0 * NdotL * NdotV);
		float3 color = NdotL * LightColor * (diffuseContrib + specContrib);

		color *= attenuation;
		color *= depthValue;

		// Calculate lighting contribution from image based lighting source (IBL)
		//color += getIBLContribution(perceptualRoughness, NdotV, diffuseColor, specularColor, n, reflection);

		return float4(color, 1.0f);
	}

	float4 CalculatePBRSpotLighting(
		Texture2D materialTex,
		SamplerState samp,
		float2 TexCoord0,
		float3 normal,
		float3 PositionWorld,
		float3 LightDirection,
		float3 LightColor,
		float3 pixelColour,
		float depthValue,
		float attenuation
	)
	{
		// Roughness is stored in the 'g' channel, metallic is stored in the 'b' channel.
		// This layout intentionally reserves the 'r' channel for (optional) occlusion map data
		const float3 mrSample = materialTex.Sample(samp, TexCoord0);
		const float3 baseColor = pixelColour;
		const float metallic = saturate(mrSample.r);
		float perceptualRoughness = clamp(mrSample.g, MinRoughness, 1.0);
		perceptualRoughness = ApplySpecularAntiAliasing(normal, perceptualRoughness);

		// Roughness is authored as perceptual roughness; as is convention,
		// convert to material roughness by squaring the perceptual roughness [2].
		const float alphaRoughness = perceptualRoughness * perceptualRoughness;

		const float3 diffuseColor = (baseColor.rgb * (float3(1.0, 1.0, 1.0) - f0)) * (1.0 - metallic);
		const float3 specularColor = lerp(f0, baseColor.rgb, metallic);

		// Compute reflectance.
		const float reflectance = max(max(specularColor.r, specularColor.g), specularColor.b);

		// For typical incident reflectance range (between 4% to 100%) set the grazing reflectance to 100% for typical fresnel effect.
		// For very low reflectance range on highly diffuse objects (below 4%), incrementally reduce grazing reflecance to 0%.
		const float reflectance90 = saturate(reflectance * 25.0);
		const float3 specularEnvironmentR0 = specularColor.rgb;
		const float3 specularEnvironmentR90 = float3(1.0, 1.0, 1.0) * reflectance90;

		const float3 v = normalize(g_eyePos.xyz - PositionWorld);   // Vector from surface point to camera
		const float3 l = normalize(LightDirection);                           // Vector from surface point to light
		const float3 h = normalize(l + v);                                    // Half vector between both l and v
		const float3 reflection = -normalize(reflect(v, normal));

		const float NdotL = clamp(dot(normal, l), 0.001, 1.0);
		const float NdotV = abs(dot(normal, v)) + 0.001;
		const float NdotH = saturate(dot(normal, h));
		const float LdotH = saturate(dot(l, h));
		const float VdotH = saturate(dot(v, h));

		//return float4(NdotL, NdotL, NdotL, 1.0f);

		// Calculate the shading terms for the microfacet specular shading model
		const float3 F = specularReflection(specularEnvironmentR0, specularEnvironmentR90, VdotH);
		const float G = geometricOcclusion(NdotL, NdotV, alphaRoughness);
		const float D = microfacetDistribution(NdotH, alphaRoughness);

		// Calculation of analytical lighting contribution
		const float3 diffuseContrib = (1.0 - F) * diffuse(diffuseColor);
		const float3 specContrib = F * G * D / (4.0 * NdotL * NdotV);
		float3 color = NdotL * LightColor * (diffuseContrib + specContrib);

		color *= attenuation;
		color *= depthValue;

		// Calculate lighting contribution from image based lighting source (IBL)
		//color += getIBLContribution(perceptualRoughness, NdotV, diffuseColor, specularColor, n, reflection);

		return float4(color, 1.0f);
	}
}
