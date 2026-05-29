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

	// Computes the cell grid the rain-drip system uses, independent of wetness.
	// Used by the debug visualizer so we can SEE the basis the noise samples
	// (squares = good 2D basis; horizontal stripes = degenerate basis) and the
	// scroll direction (the grid should slide DOWN over time on walls).
	// Returns (cellFrac.x, cellFrac.y, gridLine) in [0, 1].
	float3 RainDripsCellGridDebug(float3 baseNormalWS, float3 worldPos, float time, float isHorizontal)
	{
		// Mirror the *new* (v2) ApplyRainDroplets behaviour exactly:
		//   - LAYER A cells DO NOT scroll over time (drops pulse in place)
		//   - cell size matches LAYER A's kCellSize (0.12)
		// So a viewer looking at this debug output should see cells STATIC in
		// world space, NOT translating. If you see cells scrolling vertically,
		// the runtime is still loading the v1 .hcs (uniform UV scroll) and the
		// pkg has stale compiled shaders even though PBRutils source was edited.
		// Definitive "is the new code actually loaded?" test.
		const float kCellSize = 0.12f;
		const float3 worldUp = float3(0.0f, 1.0f, 0.0f);
		const float3 horizAxis = normalize(cross(worldUp, baseNormalWS) + float3(1e-4f, 0.0f, 0.0f));
		const float wallHorizCoord = dot(worldPos, horizAxis);
		const float2 uv = lerp(float2(wallHorizCoord, worldPos.y), worldPos.xz, isHorizontal) / kCellSize;
		// NO scroll - matches LAYER A's "pulse in place" model.
		const float2 cellFrac = frac(uv);
		// Additionally pulse the cells via a per-cell sin so you can see the
		// per-cell lifetime variation (matches dropPhase logic in LAYER A).
		const float2 cell = floor(uv);
		const float dropPhase = Hash21_Rain(cell + 7.13f);
		const float pulse = saturate(sin((time + dropPhase * 10.0f) * 2.0f) * 0.5f + 0.5f);
		const float gridLine = step(0.95f, max(cellFrac.x, cellFrac.y));
		// Encode pulse into the blue channel so observers can see cells "blinking"
		// at different phases - confirms the new per-cell lifetime is wired up.
		return float3(cellFrac.x, cellFrac.y, max(gridLine, pulse * 0.6f));
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
		// Cheap early-out so dry materials cost nothing.
		if (wetness <= 0.001f)
			return float4(baseNormalWS, 1.0f);

		// Surface-local horizontal axis. cross(up, normal) sweeps left/right
		// across any vertical wall regardless of which world axis the wall is
		// aligned to. The 1e-4 bias keeps the normalize stable for surfaces with
		// normals close to world up; those go through the isHorizontal path
		// anyway so the bias never matters visually.
		const float3 worldUp   = float3(0.0f, 1.0f, 0.0f);
		const float3 horizAxis = normalize(cross(worldUp, baseNormalWS) + float3(1e-4f, 0.0f, 0.0f));
		const float wallHorizCoord = dot(worldPos, horizAxis);

		// Output accumulators. Start with a base "wet film" roughness drop
		// (whole surface gets slightly smoother in rain) and the unperturbed
		// normal. Layers below add to these.
		float3 perturbedNormal = baseNormalWS;
		float  roughMul        = lerp(1.0f, 0.55f, wetness);

		// =====================================================================
		// LAYER A: small static beads
		//
		// Cell-based pulsing drops that don't translate over time - they grow
		// in then fade out in place via a per-cell sin lifetime. Matches the
		// fine stippling visible between the streaks on a real wet window.
		// World-XZ space for floors / world-(horizAxis, Y) for walls.
		// Amplitude is deliberately reduced from earlier versions - this layer
		// is supporting texture now, the dominant effect on walls comes from
		// the streak layer below.
		// =====================================================================
		{
			const float kCellSize = 0.12f;
			const float2 uvA = lerp(float2(wallHorizCoord, worldPos.y), worldPos.xz, isHorizontal) / kCellSize;
			const float2 cellA     = floor(uvA);
			const float2 cellFracA = frac(uvA);

			const float2 dropCentre = Hash22_Rain(cellA) * 0.6f + 0.2f;
			const float  dropPhase  = Hash21_Rain(cellA + 7.13f);
			const float  dropPeriod = 1.5f + dropPhase * 1.5f;
			const float  tA         = (time + dropPhase * 10.0f) / dropPeriod;
			const float  lifetime   = saturate(sin(tA * 6.2831f) * 0.5f + 0.5f);
			const float  maxRadius  = 0.28f * wetness * lifetime;

			const float2 toCentre  = cellFracA - dropCentre;
			const float  dist      = length(toCentre);
			const float  dropMask  = saturate(1.0f - dist / max(maxRadius, 0.001f));
			const float  dropH     = smoothstep(0.0f, 1.0f, dropMask);

			if (dist > 0.001f && dropH > 0.001f)
			{
				const float2 dir = toCentre / dist;
				// Reduced amp (0.45 vs 0.9) since this layer is the supporting one.
				// Convex bump: ADD the outward gradient.
				const float  amp = dropH * dropH * 0.45f * wetness;
				perturbedNormal += (dir.x * tangentWS + dir.y * binormalWS) * amp;
			}
			roughMul = lerp(roughMul, 0.25f, dropMask * wetness);
		}

		// =====================================================================
		// LAYER B: sliding streaks (walls only)
		//
		// For each "lane" (vertical strip of the wall) we drop a single bead
		// that falls top-to-bottom at a per-lane RANDOM velocity, with a thin
		// trail above the current head position (where the bead has just been).
		// Per-lane random parameters break the lockstep uniform-scroll look
		// from the previous design:
		//   - velocity in [0.4, 1.4] m/s -> visibly different fall rates
		//   - lane height in [2, 4] m   -> stacks don't repeat at a fixed period
		//   - phase offset random       -> drops don't all spawn together
		//
		// We sample 3 adjacent lanes per pixel so streaks aren't snapped to a
		// hard grid - neighbouring lanes' streaks at different horizontal
		// offsets will overlap at lane boundaries and give a natural variation
		// in horizontal spacing.
		// =====================================================================
		if (isHorizontal < 0.5f)
		{
			// Lane / streak sizing dial. Previous values had lanes 18 cm apart
			// with streaks 2.2 cm wide ~= 12% horizontal coverage = mostly-dry
			// wall with rare invisible streaks. Tightening the lane spacing AND
			// widening the streak so coverage is ~65%, which reads as "wall with
			// many rain streaks running down" - the look the user wants.
			// Streak sizing. Earlier visibility pass overshot - 8 cm wide streaks
			// with 4.5 cm heads looked like "wet patches" rather than rain.
			// Real rain droplets on glass are mm-scale; at this stylised game
			// scale ~2 cm wide reads as proper streaks, ~1.5 cm tall heads stay
			// readable but don't blob out. Lanes are 6 cm apart for density,
			// trails kept generous at 60 cm so the runs-down character is clear.
			const float kLaneWidth        = 0.06f;  // 6 cm between streaks
			const float kStreakHalfWidth  = 0.012f; // 2.4 cm wide streak (~40% lane coverage)
			const float kHeadHalfHeight   = 0.018f; // 1.8 cm tall head
			const float kTrailLength      = 0.6f;   // 60 cm trail

			[unroll]
			for (int laneOff = -1; laneOff <= 1; ++laneOff)
			{
				const float laneIdx          = floor(wallHorizCoord / kLaneWidth) + (float)laneOff;
				const float laneCenterXBase  = (laneIdx + 0.5f) * kLaneWidth;

				const float2 hLane     = Hash22_Rain(float2(laneIdx, 17.0f));
				// Slower default velocity range - water clinging to a wall by
				// surface tension moves slowly until it gets heavy. 0.2 to 0.8
				// m/s reads as "running down the window" rather than "sprayed".
				const float  vel       = lerp(0.2f, 0.8f, hLane.x);
				const float  cycleTime = lerp(2.5f, 5.0f, hLane.y);
				const float  phase     = hLane.x * 17.13f + (float)laneOff * 3.7f;
				const float  laneH     = vel * cycleTime; // total fall distance per cycle

				const float pixelInLane = frac((worldPos.y + phase * 13.7f) / laneH) * laneH;
				const float dropAge      = frac((time + phase) / cycleTime) * cycleTime;
				const float dropPosInLane = laneH - vel * dropAge;

				const float dy = pixelInLane - dropPosInLane;
				if (dy < -0.05f || dy > kTrailLength) continue;

				// Lateral wiggle: water droplets running down a real surface follow
				// a curved path because gravity isn't perfectly aligned with the
				// wall's tangent plane (micro-imperfections, surface tension
				// asymmetry). Modelled here as the sum of two sin waves at
				// different frequencies + random per-lane phase:
				//   high freq (~3 cycles/m) gives small jitter
				//   low  freq (~0.7 cycles/m) gives long sweeping arcs
				// Each pixel computes the centre line at its OWN worldY, so the
				// trail naturally follows the curve the drop took on the way down.
				// Total max drift is ~1.2cm, kept under kLaneWidth/2 so streaks
				// stay mostly within their lane.
				const float wiggleHi = sin(worldPos.y * 18.85f + phase * 7.31f) * 0.006f;
				const float wiggleLo = sin(worldPos.y *  4.40f + phase * 11.7f) * 0.008f;
				const float laneCenterX = laneCenterXBase + wiggleHi + wiggleLo;

				const float dxFromCenter = wallHorizCoord - laneCenterX;
				if (abs(dxFromCenter) > kStreakHalfWidth) continue;
				const float hMask = saturate(1.0f - abs(dxFromCenter) / kStreakHalfWidth);

				const float headMask = saturate(1.0f - abs(dy) / kHeadHalfHeight);

				// Trail is brighter so it reads against the dry-ish neighbours.
				const float trailMask = (dy > 0.0f) ? saturate(1.0f - dy / kTrailLength) * 0.7f : 0.0f;

				const float streakIntensity = (headMask + trailMask) * hMask * wetness;

				roughMul = lerp(roughMul, 0.10f, streakIntensity);

				// Reduced normal-perturbation strength to match the smaller head
				// size - was 0.6/0.5 when heads were 4.5 cm; for 1.8 cm heads we
				// want less aggressive bumping so the heads read as small beads,
				// not raised blisters.
				const float xPerturbAmp = (dxFromCenter / max(kStreakHalfWidth, 0.001f)) * headMask * 0.35f * wetness;
				const float yPerturbAmp = (dy           / max(kHeadHalfHeight, 0.001f)) * headMask * 0.30f * wetness;
				perturbedNormal += horizAxis * xPerturbAmp + worldUp * yPerturbAmp;
			}
		}

		return float4(normalize(perturbedNormal), saturate(roughMul));
	}
}
