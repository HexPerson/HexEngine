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

	// Lambertian diffuse. The reference term is albedo/PI; the divide had been commented out,
	// which left diffuse response PI times hot relative to the specular lobe on every lit
	// surface. Restoring it is a deliberate, large change in overall brightness that needs
	// light intensities and exposure retuned, so it rides on g_pbrEnergyFix (r_pbrEnergyFix,
	// default off) rather than silently changing every existing scene.
	//
	// Every lighting path funnels through here - CalculatePBR, CalculatePBRSurface, the
	// point/spot variants and the forward-transparency path - so the flag covers all of them.
	float3 diffuse(float3 diffuseColor)
	{
		return diffuseColor * lerp(1.0f, 1.0f / PI, g_pbrEnergyFix);
	}

	// EnvBRDFApprox moved to EnvMapCommon.shader. The SSR resolve now evaluates the
	// same environment-specular term as the deferred pass and needs the fit without
	// pulling in this whole lighting header; keeping a second copy here would be a
	// duplicate definition in every shader that includes both.

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

	// Cheap 3D hash + value noise helpers used by the snow / drip layers below.
	// Standard iq-style hash; good enough for spatially-coherent procedural
	// patterns at the scales we sample (cm to metres in world space).
	float Hash13_PBR(float3 p)
	{
		p = frac(p * 0.1031f);
		p += dot(p, p.yzx + 33.33f);
		return frac((p.x + p.y) * p.z);
	}

	float ValueNoise3(float3 p)
	{
		const float3 pi = floor(p);
		const float3 pf = frac(p);
		const float3 w  = pf * pf * (3.0f - 2.0f * pf);

		const float n000 = Hash13_PBR(pi + float3(0,0,0));
		const float n100 = Hash13_PBR(pi + float3(1,0,0));
		const float n010 = Hash13_PBR(pi + float3(0,1,0));
		const float n110 = Hash13_PBR(pi + float3(1,1,0));
		const float n001 = Hash13_PBR(pi + float3(0,0,1));
		const float n101 = Hash13_PBR(pi + float3(1,0,1));
		const float n011 = Hash13_PBR(pi + float3(0,1,1));
		const float n111 = Hash13_PBR(pi + float3(1,1,1));

		const float nx00 = lerp(n000, n100, w.x);
		const float nx10 = lerp(n010, n110, w.x);
		const float nx01 = lerp(n001, n101, w.x);
		const float nx11 = lerp(n011, n111, w.x);
		const float nxy0 = lerp(nx00, nx10, w.y);
		const float nxy1 = lerp(nx01, nx11, w.y);
		return lerp(nxy0, nxy1, w.z);
	}

	// =====================================================================
	// Snow accumulation
	//
	// Drives the surface toward white-albedo + soft-roughness when the weather
	// system reports snow on upward-facing geometry. Unlike rain drips this is
	// applied globally (no per-material opt-in slider) - any horizontal-ish
	// surface naturally takes snow during a snowfall regardless of material.
	// Vertical walls / ceilings stay clear (no normal.y component to catch the
	// snow). The slope falloff is soft so eaves / cambered roads get partial
	// snow on the upward side and dry on the downward side - more natural than
	// a hard step.
	//
	// Inputs:
	//   baseAlbedo   - albedo BEFORE snow modification
	//   baseRoughness - perceptual roughness BEFORE snow
	//   worldNormalWS - surface normal (world space)
	//   worldPos      - surface world position (for noise)
	//   snowCoverage  - g_weatherSurface.snowCoverage (0..1)
	//
	// Returns: float4(modifiedAlbedo, modifiedRoughness) - drop into the
	// existing gbuffer write.
	// =====================================================================
	// Shelter/rain occlusion top-down depth map (see the doc block further
	// down at SampleRainShelter). Declared here because the snow function
	// below also reads it - neighbourhood depth deltas reveal walls for
	// drift banks.
	Texture2D<float> g_rainOcclusionMap : register(t26);

	// Snow micro-relief height at a world XZ position, in metres. Two
	// octaves: 45 cm drift undulation + 13 cm surface clumping. Shared by
	// the value and the finite-difference gradient below.
	float SnowHeightField(float2 xz)
	{
		const float h1 = ValueNoise3(float3(xz.x, 0.0f, xz.y) / 0.45f);
		const float h2 = ValueNoise3(float3(xz.x, 3.7f, xz.y) / 0.13f);
		return h1 * 0.7f + h2 * 0.3f;
	}

	// snowMelt (slice 4): erodes the mask and turns powder into slush.
	// Melting snow retreats from the noise-thin areas first (the same
	// bias direction low coverage uses), and what remains reads wet -
	// roughness drops toward slush instead of powder's 0.85. The melt ->
	// ground-wetness coupling happens at the call sites, not here.
	//
	// Slice 5 (volumetric-look snow): the flat texture-overlay read is
	// gone - snow now carries a HEIGHT FIELD. Its finite-difference
	// gradient perturbs the surface normal (micro-relief drifts and
	// clumps), tall geometry nearby raises it into DRIFT BANKS (the
	// shelter map at t26 is a top-down depth map, so neighbouring texels
	// that are much nearer the sky than this surface mean "a wall stands
	// half a metre away" - snow piles against building bases for free),
	// and melt scales the height toward zero so slush flattens
	// GEOMETRICALLY before the threshold retreat removes it. worldNormal
	// is inout for the relief; samp samples the shelter map.
	float4 ApplySnowAccumulation(
		float3 baseAlbedo,
		float baseRoughness,
		inout float3 worldNormalWS,
		float3 worldPos,
		float snowCoverage,
		float snowMelt,
		SamplerState samp)
	{
		if (snowCoverage <= 0.001f)
			return float4(baseAlbedo, baseRoughness);

		// Slope mask: smoothstep 0.35 -> 0.85 on normal.y gives partial snow
		// from "slight slope" up to full snow on perfectly flat surfaces. Below
		// 0.35 (~70 degrees from horizontal) no snow forms.
		const float slopeMask = smoothstep(0.35f, 0.85f, worldNormalWS.y);
		if (slopeMask <= 0.0f)
			return float4(baseAlbedo, baseRoughness);

		// Procedural noise so snow patches read as "actual snow with texture",
		// not a flat white paint. Two-octave value noise at world-space XZ so
		// the pattern stays anchored as the camera moves.
		const float kNoiseScale = 0.45f; // 45 cm per noise cycle - snow drift scale
		const float n1 = ValueNoise3(float3(worldPos.x, 0.0f, worldPos.z) / kNoiseScale);
		const float n2 = ValueNoise3(float3(worldPos.x, 0.0f, worldPos.z) / (kNoiseScale * 0.4f));
		// Melt erodes the noise THRESHOLD rather than scaling the mask: thin
		// snow (noise-low areas) vanishes first, drift cores survive longest
		// - spatially progressive retreat, which is both how real melt looks
		// and a far more legible slider response than the uniform fade this
		// used to be (any melt read as "on", the magnitude was invisible).
		const float melt = saturate(snowMelt);
		const float patchNoise = saturate((n1 * 0.65f + n2 * 0.35f)
			- (1.0f - snowCoverage) * 0.45f
			- melt * 0.55f);

		// Snow mask: combine slope + patch noise + global coverage.
		// At snowCoverage = 1, almost everything in the slope-permissive band
		// is white. At snowCoverage ~ 0.3 only the densest patch-noise areas
		// catch snow, giving the "dusting -> blanket" progression. The flat
		// melt scale on top thins what survives the threshold; full melt
		// leaves ~30% of the drift cores as wet slush remnants.
		float snowMask = saturate(slopeMask * (0.2f + patchNoise * 1.4f) * snowCoverage)
			* (1.0f - melt * 0.7f);

		// Drift banks: sample the top-down depth map ~55 cm to each side.
		// A neighbour whose recorded depth is >=1.5 m nearer the sky than
		// this surface is a wall/prop face - snow drifts pile against it.
		// Melt kills drifts fastest (banks are where slush pools).
		float driftBank = 0.0f;
		if (g_rainOcclusionParams.x > 0.5f)
		{
			const float4 clip = mul(float4(worldPos, 1.0f), g_rainOcclusionVP);
			const float2 uv = clip.xy * float2(0.5f, -0.5f) + 0.5f;
			if (all(uv >= 0.0f) && all(uv <= 1.0f) && clip.z > 0.0f && clip.z < 1.0f)
			{
				// 0.55 m in UV: footprint is 2*extent metres across.
				const float2 stepUv = g_rainOcclusionParams.z * 6.0f;
				const float kWallDelta = 1.5f / 160.0f; // metres over depth range
				[unroll]
				for (int i = 0; i < 4; ++i)
				{
					const float2 o = float2((i & 1) ? stepUv.x : -stepUv.x,
					                        (i & 2) ? stepUv.y : -stepUv.y);
					const float neighbourDepth = g_rainOcclusionMap.SampleLevel(samp, uv + o, 0);
					driftBank += (clip.z - neighbourDepth > kWallDelta) ? 0.25f : 0.0f;
				}
				driftBank *= (1.0f - melt);
			}
		}
		snowMask = saturate(snowMask + driftBank * 0.5f * slopeMask * snowCoverage);

		// Micro-relief: finite-difference gradient of the height field bends
		// the normal so drifts and clumps actually SHADE - the difference
		// between a white decal and a snow surface. Height amplitude scales
		// with the mask (thin dustings are flat) and collapses with melt
		// (slush flattens geometrically before it retreats). Drift banks
		// steepen the relief where they pile.
		if (snowMask > 0.02f)
		{
			const float kSampleDist = 0.09f; // metres between height taps
			const float hC = SnowHeightField(worldPos.xz);
			const float hX = SnowHeightField(worldPos.xz + float2(kSampleDist, 0.0f));
			const float hZ = SnowHeightField(worldPos.xz + float2(0.0f, kSampleDist));
			// 0.18 gives ~25-35 degree clump slopes - the original 0.05
			// topped out near 8 degrees, which flat overcast lighting
			// swallowed entirely (user: "I don't see any difference").
			const float amplitude = 0.18f * snowMask * (1.0f - melt) * (1.0f + driftBank * 1.5f);
			const float3 reliefNormal = normalize(float3(
				-(hX - hC) / kSampleDist * amplitude,
				1.0f,
				-(hZ - hC) / kSampleDist * amplitude));
			// Blend in world space: snow relief overrides the underlying
			// surface detail as the blanket thickens.
			worldNormalWS = normalize(lerp(worldNormalWS, reliefNormal, snowMask * 0.85f));
		}

		// Snow colour: very slightly blue-tinted white (real snow scatters short
		// wavelengths more, plus diffuse sky tint). Pure-white reads as paint.
		// The height field also shades the albedo slightly - crevices between
		// clumps read a touch darker, which sells the volume even where the
		// lighting is flat.
		const float crevice = SnowHeightField(worldPos.xz);
		const float3 snowColour = float3(0.92f, 0.94f, 0.98f) * (0.78f + 0.22f * crevice);
		const float3 newAlbedo = lerp(baseAlbedo, snowColour, snowMask);

		// Snow is highly diffuse (lots of micro-scattering between snowflakes)
		// so roughness goes UP, not down. 0.85 is the typical snow roughness
		// in PBR refs; melting slush is water-bound and markedly shinier.
		const float snowRough = lerp(0.85f, 0.35f, melt);
		const float newRoughness = lerp(baseRoughness, snowRough, snowMask);

		return float4(newAlbedo, newRoughness);
	}

	// =====================================================================
	// Dust/sand accumulation (slice 4) - the first consumer of the
	// previously dead dirtAmount field (the sandstorm preset authors 0.7).
	// Same universal shape as snow: up-facing slope mask x world-XZ patch
	// noise x amount, tinting toward a dry sand colour and roughening.
	// Coarser noise than snow (dust drifts in broader sheets), a weaker
	// slope requirement (dust clings to shallower slopes than snow lays
	// on), and only PARTIAL shelter response - wind carries dust under
	// cover, so shelter attenuates it by half instead of zeroing it.
	// =====================================================================
	float4 ApplyDustAccumulation(
		float3 baseAlbedo,
		float baseRoughness,
		float3 worldNormalWS,
		float3 worldPos,
		float dirtAmount)
	{
		if (dirtAmount <= 0.001f)
			return float4(baseAlbedo, baseRoughness);

		const float slopeMask = smoothstep(0.15f, 0.75f, worldNormalWS.y);
		if (slopeMask <= 0.0f)
			return float4(baseAlbedo, baseRoughness);

		const float kNoiseScale = 1.1f; // broad drift sheets
		const float n1 = ValueNoise3(float3(worldPos.x, 0.0f, worldPos.z) / kNoiseScale);
		const float n2 = ValueNoise3(float3(worldPos.x, 0.0f, worldPos.z) / (kNoiseScale * 0.31f));
		const float patchNoise = saturate((n1 * 0.7f + n2 * 0.3f) - (1.0f - dirtAmount) * 0.35f);

		const float dustMask = saturate(slopeMask * (0.15f + patchNoise * 1.2f) * dirtAmount) * 0.85f;

		const float3 dustColour = float3(0.52f, 0.42f, 0.30f);
		const float3 newAlbedo = lerp(baseAlbedo, dustColour, dustMask);
		const float newRoughness = lerp(baseRoughness, 0.92f, dustMask);

		return float4(newAlbedo, newRoughness);
	}

	// =====================================================================
	// Shelter/rain occlusion (Phase 3 slice 2). Top-down ortho depth map
	// rendered around the camera by SceneRenderer (static geometry only,
	// cached and refreshed on recentre / a slow timer). Bound at t26
	// during the opaque pass - t21..t24 are taken by the terrain layer
	// textures and t27..t29 by the forward cluster lists, t26 is free in
	// every shader that includes this file. The declaration only claims
	// the register in shaders that actually call SampleRainShelter (fxc
	// strips unused bindings). g_rainOcclusionVP/Params live in the Global cbuffer,
	// which every consumer of this include already lists first (the
	// lightning code above depends on the same ordering).
	//
	// Returns exposure to the sky: 1 = rain/snow reaches this surface,
	// 0 = covered by static geometry above it. Pixels outside the map's
	// footprint are treated as exposed - the map follows the camera, so
	// distant surfaces degrade to the pre-shelter behaviour instead of
	// popping dry. 2x2 taps soften the shelter edge by one texel.
	// (The t26 texture itself is declared above ApplySnowAccumulation,
	// which also reads it for drift-bank detection.)
	// =====================================================================
	float SampleRainShelter(float3 worldPos, SamplerState samp)
	{
		if (g_rainOcclusionParams.x < 0.5f)
			return 1.0f;

		const float4 clip = mul(float4(worldPos, 1.0f), g_rainOcclusionVP);
		const float2 uv = clip.xy * float2(0.5f, -0.5f) + 0.5f;
		if (any(uv < 0.0f) || any(uv > 1.0f) || clip.z < 0.0f || clip.z > 1.0f)
			return 1.0f;

		const float bias = g_rainOcclusionParams.y;
		const float texel = g_rainOcclusionParams.z;
		float exposed = 0.0f;
		[unroll]
		for (int i = 0; i < 4; ++i)
		{
			const float2 o = float2((i & 1) ? texel : -texel, (i & 2) ? texel : -texel) * 0.5f;
			const float mapDepth = g_rainOcclusionMap.SampleLevel(samp, uv + o, 0);
			// The map stores the depth of the highest surface. If that is
			// meaningfully NEARER the sky than this pixel, something is
			// overhead: sheltered.
			exposed += (clip.z <= mapDepth + bias) ? 0.25f : 0.0f;
		}
		return exposed;
	}

	// =====================================================================
	// Rain-impact ripples (Phase 3 slice 3). Expanding rings on wet
	// up-facing surfaces while precipitation is actually falling - the
	// missing life in the wet-street look (the drip system's Layer A beads
	// pulse in place; real rain reads as rings spreading from impacts).
	//
	// Two staggered layers of world-space cells; each cell runs a looping
	// ring whose radius grows over its cycle while the amplitude dies out.
	// A per-cell hash offsets the cycle phase so neighbouring cells never
	// pulse in sync. The normal is bent radially by the ring's slope. All
	// world-anchored, so ripples stay put under camera motion.
	//
	// intensity: shelteredWetness * precipitationIntensity - ripples need
	// BOTH a water film to ride on and active rainfall (a wet street after
	// the rain stops must not keep rippling). Cheap early-out when ~0.
	// =====================================================================
	float3 ApplyRainRipples(float3 normalWS, float3 worldPos, float time, float intensity)
	{
		const float up = saturate(normalWS.y);
		const float strength = saturate(intensity) * up * up;
		if (strength <= 0.001f)
			return normalWS;

		float2 grad = float2(0.0f, 0.0f);
		[unroll]
		for (int layer = 0; layer < 2; ++layer)
		{
			// 35 cm / 23 cm cells, second layer offset so cell walls never line up.
			const float cellSize = (layer == 0) ? 0.35f : 0.23f;
			const float2 layerOffset = (layer == 0) ? float2(0.0f, 0.0f) : float2(0.17f, 0.11f);
			const float2 p = worldPos.xz / cellSize + layerOffset;
			const float2 cell = floor(p);
			const float2 local = (p - cell - 0.5f) * cellSize; // metres from cell centre

			const float phase = Hash21_Rain(cell + (layer == 0 ? 3.7f : 9.1f));
			// ~1.4 cycles/sec, per-cell phase offset. t = 0 impact, t = 1 faded.
			const float t = frac(time * 1.4f + phase);

			const float ringRadius = t * (cellSize * 0.55f);
			const float d = length(local);
			const float band = d - ringRadius;
			// Ring profile: a single sine arch localised to the band, its
			// height dying with age. The gradient of the height field w.r.t.
			// XZ is radial - that is what bends the normal.
			const float kBandWidth = 0.045f; // metres
			const float envelope = exp(-(band * band) / (kBandWidth * kBandWidth));
			const float age = (1.0f - t) * (1.0f - t);
			const float slope = envelope * age * (-2.0f * band / (kBandWidth * kBandWidth));
			grad += (d > 1e-4f ? local / d : float2(0.0f, 0.0f)) * slope * 0.0035f;
		}

		normalWS.xz += grad * strength;
		return normalize(normalWS);
	}

	// =====================================================================
	// Universal wet-surface response (Phase 3 slice 1). Unlike the drip
	// system below - which is per-material opt-in via rainDripIntensity -
	// this applies to EVERY opaque surface, the same way snow does: rain
	// wets the whole world, materials only differ in HOW they respond.
	//
	// With no per-material porosity authored anywhere, porosity is
	// estimated from base roughness: rough dielectrics (concrete, brick,
	// fabric) soak water into their micro-structure, darkening strongly
	// while the film's gloss is damped; smooth sealed surfaces (painted
	// metal, glass, polished stone) barely darken but gain the water film
	// gloss almost fully. Metals don't absorb water, so darkening fades
	// with metalness. The film response uses wetness^2 - light drizzle
	// barely films, saturation comes on toward storm wetness (matches the
	// Frostbite wetness curve's shape).
	//
	//   albedo    - inout, darkened in place
	//   roughness - inout, driven toward the water film's ~0.12
	//   metalness - metal mask (kills darkening)
	//   wetness   - g_weatherSurface.wetness (0..1)
	//   darkenStrength - g_wetnessDarkening cvar lane (passed in so this
	//                    include doesn't depend on Global.shader defines)
	//
	// Returns the water-film strength (0 dry .. ~1 storm on sealed
	// surfaces). Callers feed it into the gbuffer smoothness channel
	// (mat.b, the SSR gate) - roughness alone doesn't open SSR, and a wet
	// street that never screen-space-reflects misses the entire point.
	// =====================================================================
	float ApplyWetSurface(
		inout float3 albedo,
		inout float roughness,
		float metalness,
		float wetness,
		float darkenStrength)
	{
		const float w = saturate(wetness);
		const float porosity = saturate((roughness - 0.25f) / 0.5f);
		const float darken = darkenStrength * lerp(0.4f, 1.0f, porosity) * (1.0f - metalness);
		albedo *= lerp(1.0f, 1.0f - darken, w);

		const float film = w * w * lerp(1.0f, 0.55f, porosity);
		roughness = lerp(roughness, 0.12f, film);
		return film;
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
