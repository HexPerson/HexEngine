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
	// channel packs (modelId << 5) | (modelParams.w_quant) into a single RGBA8
	// byte; upper 3 bits = id (0..7 supported, 0..4 used), lower 5 bits = a
	// quantised modelParams.w (needed by sheen for tint.b - the fourth modelParam
	// we couldn't otherwise store with only 4 RT channels). See DefaultPixel for
	// the encoding side. Tolerant to point sampling; linear filtering at material
	// boundaries (e.g. SSS pixel next to standard pixel) corrupts both fields,
	// which is acceptable since the SSS/etc post-effects already gate on
	// strength / mask and silently no-op on the ambiguous boundary pixels.
	uint DecodeMaterialModelId(float r)
	{
		const uint byteVal = (uint)floor(r * 255.0f + 0.5f);
		return byteVal >> 5;
	}

	// Recover the quantised modelParams.w from the same packed .r channel as
	// DecodeMaterialModelId. Range [0,1] with 5-bit quantisation (32 levels) -
	// adequate for sheen tint .b which is the only consumer; visible banding
	// would only show up if we were modulating a high-frequency parameter,
	// which sheen tint is not.
	float DecodePackedModelParamW(float r)
	{
		const uint byteVal = (uint)floor(r * 255.0f + 0.5f);
		return (float)(byteVal & 31u) / 31.0f;
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

	// =====================================================================
	// Procedural rain droplets
	//
	// Cheap world-space "wet surface with rain drops" effect. Two-layer cell
	// noise drives discrete drop impacts (cell centres) overlaid with a slow
	// running-streak pattern that drifts downward along world -Y. Returns
	// {perturbed normal, roughness multiplier} so the caller can drop normal
	// + lower roughness in one shot.
	//
	// Inputs:
	//   baseNormalWS - the surface normal BEFORE droplet perturbation (world space)
	//   worldPos     - the surface world-space position
	//   tangentWS    - any world-space tangent direction (for offsetting the normal)
	//   binormalWS   - the world-space binormal (typically cross(normal, tangent))
	//   wetness      - scalar 0..1 driving overall droplet density + amplitude
	//   time         - per-frame g_time, for animation
	//   isHorizontal - 1 if the surface is mostly up-facing (drops bead), 0 if
	//                  vertical (drops streak downward)
	//
	// Outputs:
	//   .xyz = perturbed normal (already normalized)
	//   .w   = roughness multiplier (1 = no change, 0 = mirror)
	// =====================================================================
	float Hash21_Rain(float2 p)
	{
		p = frac(p * float2(123.34f, 456.21f));
		p += dot(p, p + 45.32f);
		return frac(p.x * p.y);
	}

	float2 Hash22_Rain(float2 p)
	{
		float3 p3 = frac(float3(p.xyx) * float3(0.1031f, 0.1030f, 0.0973f));
		p3 += dot(p3, p3.yzx + 33.33f);
		return frac((p3.xx + p3.yz) * p3.zy);
	}

	float4 ApplyRainDroplets(
		float3 baseNormalWS,
		float3 worldPos,
		float3 tangentWS,
		float3 binormalWS,
		float wetness,
		float time,
		float isHorizontal)
	{
		// Cheap early-out so dry materials cost nothing - this gets called on
		// every pixel in DefaultPixel so we want the no-op path to be a single
		// branch.
		if (wetness <= 0.001f)
			return float4(baseNormalWS, 1.0f);

		// Drop pattern: cell noise in world XZ for horizontal surfaces, world XY
		// for vertical (streaks fall in Y). Picking the dominant projection plane
		// from isHorizontal lets the SAME function handle both cases cleanly.
		const float kCellSize = 0.15f; // ~15 cm between drop centres
		float2 uv = lerp(worldPos.xy, worldPos.xz, isHorizontal) / kCellSize;

		// Vertical surfaces: scroll the UV along world -Y over time so drops
		// streak downward. Horizontal: time-pulse drops in place.
		const float streakSpeed = 0.6f;
		uv.y -= time * streakSpeed * (1.0f - isHorizontal);

		const float2 cell     = floor(uv);
		const float2 cellFrac = frac(uv);

		// Drop centre within the cell, jittered randomly so the grid isn't visible.
		const float2 dropCentre = Hash22_Rain(cell) * 0.6f + 0.2f;

		// Lifetime: each drop has a per-cell phase + period. Pulses between 0..1
		// using a sin curve so drops grow then shrink rather than blinking on/off.
		const float dropPhase  = Hash21_Rain(cell + 7.13f);
		const float dropPeriod = 1.5f + dropPhase * 1.5f; // 1.5..3 sec per drop
		const float t          = (time + dropPhase * 10.0f) / dropPeriod;
		const float lifetime   = saturate(sin(t * 6.2831f) * 0.5f + 0.5f);

		// Max drop radius (relative to cell), modulated by wetness + lifetime.
		const float maxRadius  = 0.35f * wetness * lifetime;

		// Distance from this pixel to drop centre.
		const float2 toCentre  = cellFrac - dropCentre;
		const float  dist      = length(toCentre);

		// Drop height field: 1 at centre, 0 at radius edge, smoothstep falloff.
		const float dropMask   = saturate(1.0f - dist / max(maxRadius, 0.001f));
		const float dropHeight = smoothstep(0.0f, 1.0f, dropMask);

		// Normal perturbation: gradient of the height field points outward from
		// the drop centre. Project that into the tangent-space basis to get a
		// world-space offset we can add to the base normal. The 2.0 multiplier
		// is just a strength dial - higher = more dramatic drops.
		const float2 dir = (dist > 0.001f) ? (toCentre / dist) : float2(0.0f, 0.0f);
		const float  amp = dropHeight * dropHeight * 0.8f * wetness;
		const float3 perturbWS = (dir.x * tangentWS + dir.y * binormalWS) * amp;
		const float3 newNormal = normalize(baseNormalWS - perturbWS);

		// Wet area is also smoother (lower roughness) - between drops the
		// thin water film already smooths the surface, drops just punch it more.
		// Mix between 1.0 (dry) and ~0.25 (full wet) by the dropMask, so drops
		// stand out as the smoothest pixels.
		const float wetFloor = lerp(1.0f, 0.55f, wetness);   // base "wet but no drop"
		const float roughMul = lerp(wetFloor, 0.25f, dropMask * wetness);

		return float4(newNormal, roughMul);
	}
}
