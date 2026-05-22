"Requirements"
{
}
"InputLayout"
{
	PosNormTanBinTex_INSTANCED
}
"VertexShaderIncludes"
{
	MeshCommon
}
"PixelShaderIncludes"
{
	MeshCommon
	ShadowUtils
	PBRutils
}
"VertexShader"
{
	MeshPixelInput ShaderMain(MeshVertexInput input, MeshInstanceData instance)
	{
		MeshPixelInput output = (MeshPixelInput)0;

		input.position.w = 1.0f;

		output.position = mul(input.position, instance.world);
		output.position = mul(output.position, g_viewProjectionMatrix);

		output.positionWS = mul(input.position, instance.world);
		//output.positionWS = float4(instance.world[3].xyz, instance.world[0].x);// mul(input.position, instance.instanceWorld);

		output.colour = instance.colour;

		// we'll use this for radius and strength
		output.texcoord = instance.uvScale;

		// store the world pos of the light in tangent
		output.tangent = instance.world[3].xyz;

		output.normal = mul(input.normal, (float3x3)instance.worldInverseTranspose);
		output.normal = normalize(output.normal);

		// use viewDirection to store forwards
		//output.viewDirection = instance.customData;

		return output;
	}
}
"PixelShader"
{
	GBUFFER_RESOURCE(0, 1, 2, 3, 4);
	SHADOWMAPS_RESOURCE(5);
	// Material-features RT. SHADOWMAPS occupies t5..t10, so t11 is the first
	// free slot for the features SRV.
	GBUFFER_FEATURES_RESOURCE(11)

	SamplerState g_textureSampler : register(s0);
	SamplerComparisonState g_cmpSampler : register(s1);
	SamplerState g_pointSampler : register(s2);

	float ComputeScattering(float lightDotView)
	{
		float result = 1.0f - g_atmosphere.volumetricScattering * g_atmosphere.volumetricScattering;
		result /= (4.0f * PI * pow(1.0f + g_atmosphere.volumetricScattering * g_atmosphere.volumetricScattering - (2.0f * g_atmosphere.volumetricScattering) * lightDotView, 1.5f));
		return result;
	}

	int GetVolumetricSampleCount()
	{
		if (g_atmosphere.volumetricQuality <= 0)
			return 8;
		if (g_atmosphere.volumetricQuality >= 2)
			return 20;

		return 14;
	}

	bool GetRaySphereInterval(float3 rayStart, float3 rayDirection, float3 sphereCenter, float sphereRadius, out float tMin, out float tMax)
	{
		float3 oc = rayStart - sphereCenter;
		float b = dot(oc, rayDirection);
		float c = dot(oc, oc) - (sphereRadius * sphereRadius);
		float h = b * b - c;
		if (h <= 0.0f)
		{
			tMin = 0.0f;
			tMax = 0.0f;
			return false;
		}

		float s = sqrt(h);
		tMin = -b - s;
		tMax = -b + s;
		return tMax > 0.0f;
	}

	float CalculateVolumetricScattering(float3 raySurfacePos, float3 lightPos, float3 lightDir, float radius, float lightStrength, float sceneDepth)
	{
		if (radius <= 0.0001f || lightStrength <= 0.0f || g_atmosphere.volumetricStrength <= 0.0f)
			return 0.0f;

		float3 rayStart = g_eyePos.xyz;
		float3 direction = normalize(raySurfacePos - rayStart);
		float distanceToLight = length(lightPos - rayStart);
		bool cameraInsideVolume = distanceToLight < radius;

		float tMin = 0.0f;
		float tMax = 0.0f;
		if (!GetRaySphereInterval(rayStart, direction, lightPos, radius, tMin, tMax))
			return 0.0f;
		float traceStart = max(0.0f, tMin);
		float traceEnd = tMax;
		float rayLength = traceEnd - traceStart;
		if (rayLength <= 0.0001f)
			return 0.0f;

		const int sampleCount = GetVolumetricSampleCount();
		const float stepDistance = rayLength / (float)sampleCount;
		float3 currentPos = rayStart + direction * (traceStart + stepDistance * 0.5f);
		float accumFog = 0.0f;
		float phase = ComputeScattering(dot(direction, lightDir));

		[loop]
		for (int i = 0; i < sampleCount; ++i)
		{
			float sampleDepth = -mul(float4(currentPos, 1.0f), g_viewMatrix).z;
			if (sampleDepth <= 0.0f)
			{
				currentPos += direction * stepDistance;
				continue;
			}

			if (sceneDepth > 0.0f)
			{
				float depthBias = max(0.02f, sampleDepth * 0.002f);
				if (sampleDepth > sceneDepth + depthBias)
					break;
			}

			float3 lightToSample = lightPos - currentPos;
			float d = length(lightToSample);
			if (d > 0.0001f)
			{
				lightToSample /= d;

				// Softened inverse-square for the volumetric path - see the matching
				// comment in PointLight.shader. Hard 1/d^2 across only ~14 ray samples
				// collapses the cone to one spike per ray, breaking the visual into a
				// bright blob near the source and a lit floor patch with nothing in
				// between. Soft denominator (d*d + softness^2) caps the peak smoothly so
				// every sample inside the cone contributes a meaningful amount. Surface
				// path still uses strict 1/d^2 below.
				const float softness = max(radius * 0.08f, 0.5f);
				const float softnessSqr = softness * softness;
				float distanceFalloff = saturate(1.0f - pow(d / radius, 4.0f));
				distanceFalloff *= distanceFalloff;
				float attenuation = distanceFalloff / (d * d + softnessSqr);

				// Soft cone: smoothstep between the cosines of the outer and inner cone
				// angles. Outside the outer cone the contribution is 0, inside the inner
				// cone it's 1, and between them it's a Hermite blend (zero derivative at
				// both ends so the cone edge doesn't band). Falls back to the legacy
				// Phong-exponent shape when cosOuter/cosInner are zero (means "no
				// SpotLight pushed its per-light cone state into the buffer", e.g. an
				// older code path that only set g_spotLightConeSize).
				float coneDot = dot(-lightToSample, lightDir);
				float coneAtten;
				if (g_spotLightCosOuter > 0.0f || g_spotLightCosInner > 0.0f)
				{
					coneAtten = smoothstep(g_spotLightCosOuter, max(g_spotLightCosInner, g_spotLightCosOuter + 1e-4f), coneDot);
				}
				else
				{
					coneAtten = pow(max(coneDot, 0.0f), g_spotLightConeSize);
				}

				accumFog += phase * coneAtten * attenuation * stepDistance;
			}

			currentPos += direction * stepDistance;
		}

		accumFog /= max(radius, 0.0001f);

		accumFog *= lightStrength * g_atmosphere.volumetricStrength;

		// Inside the light volume we reduce gain, but keep it distance-weighted so it does not look flat/dull.
		if (cameraInsideVolume)
		{
			float centerFactor = saturate(distanceToLight / max(radius, 0.0001f));
			float insideGain = lerp(g_atmosphere.volumetricSpotInsideMin, g_atmosphere.volumetricSpotInsideMax, centerFactor);
			accumFog *= insideGain;
		}

		return max(0.0f, accumFog);
	}

	float4 ShaderMain(MeshPixelInput input) : SV_Target
	{
		float2 screenPos = float2(input.position.x / (float)g_screenWidth, input.position.y / (float)g_screenHeight);

		float4 pixelColour = GBUFFER_DIFFUSE.Sample(g_pointSampler, screenPos);
		float4 pixelNormal = GBUFFER_NORMAL.Sample(g_pointSampler, screenPos);
		float4 pixelPosWS = GBUFFER_POSITION.Sample(g_pointSampler, screenPos);

		float3 lightPos = input.tangent;
		float lightRange = input.texcoord.x;
		float lightIntensity = input.colour.a;
		float3 lightDir = normalize(g_shadowCasterLightDir);

		float volumetricScattering = CalculateVolumetricScattering(
			input.positionWS.xyz,
			lightPos,
			lightDir,
			lightRange,
			lightIntensity,
			pixelNormal.w);

		float3 volumetricContribution = max(input.colour.rgb * volumetricScattering, 0.0f);
		if (!all(isfinite(volumetricContribution)))
			volumetricContribution = 0.0f.xxx;

		bool hasGeometry = !(pixelPosWS.a > 0.0f || pixelColour.a == -1.0f || pixelNormal.w <= 0.0f);
		if (!hasGeometry)
			return float4(volumetricContribution, 1.0f);

		float3 normalWS = pixelNormal.xyz;
		const float normalLenSq = dot(normalWS, normalWS);
		if (normalLenSq <= 0.000001f)
			return float4(volumetricContribution, 1.0f);
		normalWS *= rsqrt(normalLenSq);

		float3 lightToPixelVec = lightPos - pixelPosWS.xyz;
		float d = length(lightToPixelVec);
		if (d <= 0.0001f || d > lightRange)
			return float4(volumetricContribution, 1.0f);
		lightToPixelVec /= d;

		// Physical distance attenuation (same form as PointLight; see comment there).
		const float minDistSqr = 0.01f * 0.01f;
		float distanceFalloff = saturate(1.0f - pow(d / lightRange, 4.0f));
		distanceFalloff *= distanceFalloff;
		float attenuation = distanceFalloff / max(d * d, minDistSqr);

		// Soft cone falloff via smoothstep between cos(outer) and cos(inner). At
		// cosTheta >= cosInner the surface is fully lit by the spot; below cosOuter
		// it's dark; the band between is a Hermite blend. Legacy callers that only
		// populated g_spotLightConeSize still produce reasonable output via the
		// pow() fallback - matters during the deserialise window for old scenes.
		float coneDot = dot(-lightToPixelVec, lightDir);
		float coneAtten;
		if (g_spotLightCosOuter > 0.0f || g_spotLightCosInner > 0.0f)
		{
			coneAtten = smoothstep(g_spotLightCosOuter, max(g_spotLightCosInner, g_spotLightCosOuter + 1e-4f), coneDot);
		}
		else
		{
			coneAtten = pow(max(coneDot, 0.0f), g_spotLightConeSize);
		}

		float depthValue = 1.0f;

		// CalculatePBRSpotLighting applies `attenuation` internally (line ~333 in
		// PBRutils.shader: `color *= attenuation;`). Pass attenuation*coneAtten to the
		// PBR function and DO NOT re-multiply by them outside - the previous code
		// applied them on both sides, squaring the falloff (typically 0.01 -> 0.0001
		// at the cone center 8m below an 8m lamp). The visible symptom was a wide
		// volumetric cone visible in the air but a virtually unlit ground patch
		// underneath. Matches the PointLight shader's pattern, which already passes
		// attenuation to PBR and multiplies the result only by lightIntensity.
		float4 pbr = CalculatePBRSpotLighting(
			GBUFFER_SPECULAR,
			g_pointSampler,
			screenPos,
			normalWS,
			pixelPosWS.xyz,
			lightToPixelVec,
			input.colour.rgb,
			pixelColour.rgb,
			depthValue,
			attenuation * coneAtten
			);

		// Per-model feature lobes (clearcoat / aniso / sheen). Same approach as
		// the directional + point paths so spot lights stay consistent.
		const float4 featurePx = GBUFFER_FEATURES.Sample(g_pointSampler, screenPos);
		const uint modelId = DecodeMaterialModelId(featurePx.r);
		if (modelId != MATERIAL_MODEL_STANDARD)
		{
			const float perceptualRoughnessForFeatures = clamp(GBUFFER_SPECULAR.Sample(g_pointSampler, screenPos).g, MinRoughness, 1.0f);
			const float3 viewDir = g_eyePos.xyz - pixelPosWS.xyz;
			pbr.rgb += ApplyMaterialFeatures(
				modelId,
				float4(featurePx.g, featurePx.b, featurePx.a, 1.0f - 0.5f * (featurePx.b + featurePx.a)),
				normalWS,
				viewDir,
				lightToPixelVec,
				input.colour.rgb,
				perceptualRoughnessForFeatures,
				depthValue,
				attenuation * coneAtten);
		}

		float3 lightContribution = max((pbr.rgb * lightIntensity), 0.0f);
		lightContribution += volumetricContribution;
		if (!all(isfinite(lightContribution)))
			lightContribution = 0.0f.xxx;
		return float4(lightContribution, 1.0f);
	}
}
