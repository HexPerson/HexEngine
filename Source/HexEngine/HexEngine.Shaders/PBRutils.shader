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

		// Calculate lighting contribution from image based lighting source (IBL)
		//color += getIBLContribution(perceptualRoughness, NdotV, diffuseColor, specularColor, n, reflection);

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