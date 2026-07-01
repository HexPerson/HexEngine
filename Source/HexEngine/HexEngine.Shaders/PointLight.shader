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

		// Per-fragment world position on the light volume mesh.
		output.positionWS = mul(input.position, instance.world);

		// Store light center for pixel shader calculations.
		output.tangent = instance.world[3].xyz;

		output.colour = instance.colour;

		// we'll use this for radius and strength
		output.texcoord = instance.uvScale;

		return output;
	}
}
"PixelShader"
{
	GBUFFER_RESOURCE(0, 1, 2, 3, 4);
	// This light's 6 cube-face depth maps at t5..t10, bound per-light by
	// RenderPointLights in ENGINE face order (PointLight.cpp gLightDirs):
	// [Forward(-Z), Backward(+Z), Up(+Y), Down(-Y), Left(-X), Right(+X)] -
	// the same order as g_lightViewProjectionMatrix[0..5].
	SHADOWMAPS_RESOURCE(5);

	Texture2D g_beautyTex : register(t13);

	// Material-features RT. Moved from t5 to t11 when the shadow maps took
	// t5..t10 - matches SpotLight.shader's slot layout so both punctual
	// light shaders share conventions.
	GBUFFER_FEATURES_RESOURCE(11)

	SamplerState g_textureSampler : register(s0);
	SamplerComparisonState g_cmpSampler : register(s1);
	SamplerState g_pointSampler : register(s2);

	// Per-pixel point-light shadow term. Picks the cube face from the dominant
	// axis of the light->pixel direction (engine face order above), projects
	// the pixel through that face's view-projection matrix, and does a 4-tap
	// PCF compare against the face's depth map. Returns 1 = lit, 0 = occluded.
	//
	// SM5.0 requires literal indices into resource arrays, so the actual taps
	// dispatch through a switch with one case per face.
	float CalculatePointShadowSample(float3 worldPos, float3 worldNormal, float3 lightPos)
	{
		// Non-shadow-casting lights bind null at t5..t10; SampleCmpLevelZero
		// against a null SRV reads 0 = fully occluded, so gate on the flag
		// (same rationale as SpotLight.shader).
		if (g_shadowConfig.castsShadowsFlag == 0)
			return 1.0f;

		const float3 toPixel = worldPos - lightPos;
		const float ax = abs(toPixel.x);
		const float ay = abs(toPixel.y);
		const float az = abs(toPixel.z);

		// Engine face order follows PointLight.cpp's gLightDirs array:
		// [Forward, Backward, Up, Down, Left, Right]. SimpleMath is RIGHT-
		// handed, so Forward = (0,0,-1): the order in world axes is
		// [-Z, +Z, +Y, -Y, -X, +X]. (An earlier comment elsewhere claimed
		// Forward = +Z, which silently swapped the Z faces - don't trust
		// direction NAMES here, only the vectors.)
		int face;
		if (az >= ax && az >= ay)
			face = toPixel.z > 0.0f ? 1 : 0;
		else if (ay >= ax)
			face = toPixel.y > 0.0f ? 2 : 3;
		else
			face = toPixel.x > 0.0f ? 5 : 4;

		float4 lightClip = mul(float4(worldPos, 1.0f), g_lightViewProjectionMatrix[face]);
		if (lightClip.w <= 0.0f)
			return 1.0f;

		float3 lightNdc = lightClip.xyz / lightClip.w;
		if (lightNdc.z <= 0.0f || lightNdc.z >= 1.0f)
			return 1.0f;

		float2 uv = float2(lightNdc.x * 0.5f + 0.5f, -lightNdc.y * 0.5f + 0.5f);
		if (any(uv < 0.0f) || any(uv > 1.0f))
			return 1.0f;

		// Slope-scaled bias, slightly larger than the spot path's: point
		// lights typically sit very close to the surfaces they light (street
		// lamps, interior fixtures), which concentrates the depth range near
		// the far end of the [0,1] NDC curve where precision is worst.
		float NdotL = saturate(dot(worldNormal, normalize(-toPixel)));
		float slopeBias = 0.0015f * (1.0f - NdotL) + 0.0008f;
		float compareZ = lightNdc.z - slopeBias;

		float texel = 1.0f / max(g_shadowConfig.shadowMapSize, 1.0f);

		// 4 corner taps around the projected UV; literal face index per case.
		#define POINT_SHADOW_TAPS(IDX)                                                                  \
			vis += SHADOWMAPS[IDX].SampleCmpLevelZero(g_cmpSampler, uv + float2( texel,  texel), compareZ).r; \
			vis += SHADOWMAPS[IDX].SampleCmpLevelZero(g_cmpSampler, uv + float2(-texel,  texel), compareZ).r; \
			vis += SHADOWMAPS[IDX].SampleCmpLevelZero(g_cmpSampler, uv + float2( texel, -texel), compareZ).r; \
			vis += SHADOWMAPS[IDX].SampleCmpLevelZero(g_cmpSampler, uv + float2(-texel, -texel), compareZ).r;

		float vis = 0.0f;
		switch (face)
		{
			case 0: { POINT_SHADOW_TAPS(0) } break;
			case 1: { POINT_SHADOW_TAPS(1) } break;
			case 2: { POINT_SHADOW_TAPS(2) } break;
			case 3: { POINT_SHADOW_TAPS(3) } break;
			case 4: { POINT_SHADOW_TAPS(4) } break;
			case 5: { POINT_SHADOW_TAPS(5) } break;
		}
		#undef POINT_SHADOW_TAPS

		return saturate(vis * 0.25f);
	}

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

	float CalculateVolumetricScattering(float3 raySurfacePos, float3 lightPos, float radius, float lightStrength, float sceneDepth)
	{
		// Per-light distance gate. Lights further from the camera than
		// volumetricLightMaxDistance skip the ray-march loop entirely -
		// surface lighting still applies but the expensive volumetric pass is
		// the dominant per-pixel cost on scenes with many lights, and most
		// players won't notice the missing fog cone on a light 100m away.
		// Compare squared distances to avoid the sqrt.
		const float maxDistSqr = g_atmosphere.volumetricLightMaxDistance * g_atmosphere.volumetricLightMaxDistance;
		if (maxDistSqr > 0.0f && dot(lightPos - g_eyePos.xyz, lightPos - g_eyePos.xyz) > maxDistSqr)
			return 0.0f;

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
				// Volumetric uses the same smooth-window cutoff as surface shading but a
				// SOFTER 1/d^2 peak: with only ~14 ray-march samples a strict 1/d^2 at
				// the source dominates one sample per ray and collapses the rest of the
				// cone to invisible (the "single bright blob near the source + lit floor
				// patch with empty space between" failure mode). Replacing the hard floor
				// `max(d*d, eps)` with the soft denominator `d*d + softness^2` caps the
				// peak at 1/softness^2 and gives a smooth curve that 14 samples can
				// integrate across the cone without spikes. Surface shading keeps the
				// strict 1/d^2 in the pixel path because it sees an actual hit surface
				// rather than a quadrature approximation.
				const float softness = max(radius * 0.08f, 0.5f);
				const float softnessSqr = softness * softness;
				float distanceFalloff = saturate(1.0f - pow(d / radius, 4.0f));
				distanceFalloff *= distanceFalloff;
				float attenuation = distanceFalloff / (d * d + softnessSqr);

				float3 lightDirection = normalize(currentPos - lightPos);
				float phase = ComputeScattering(dot(direction, lightDirection));

				accumFog += phase * attenuation * stepDistance;
			}

			currentPos += direction * stepDistance;
		}

		accumFog /= max(radius, 0.0001f);

		accumFog *= lightStrength * g_atmosphere.volumetricStrength;

		// Inside the light volume we reduce gain, but keep it distance-weighted so it does not look flat/dull.
		if (cameraInsideVolume)
		{
			float centerFactor = saturate(distanceToLight / max(radius, 0.0001f));
			float insideGain = lerp(g_atmosphere.volumetricPointInsideMin, g_atmosphere.volumetricPointInsideMax, centerFactor);
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
		float volumetricScattering = CalculateVolumetricScattering(
			input.positionWS.xyz,
			lightPos,
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

		// Physical distance attenuation: classic inverse-square law (1/d^2) with a
		// smooth window that drives the contribution cleanly to zero at lightRange.
		// This is the standard UE4 / Frostbite / Filament punctual-light formulation:
		//   window(x) = saturate(1 - (d/r)^4)
		//   att      = window(d/r)^2 / max(d^2, eps^2)
		// The (d/r)^4 windowing keeps the curve close to true inverse-square through
		// most of the volume and only kicks in near the edge to hit zero; squaring
		// gives a C1 transition (no visible seam at the boundary). Clamping d^2 from
		// below avoids blow-up right at the light center - 1cm is the typical lower
		// bound used by physically based engines to represent the light source size.
		const float minDistSqr = 0.01f * 0.01f;
		float distanceFalloff = saturate(1.0f - pow(d / lightRange, 4.0f));
		distanceFalloff *= distanceFalloff;
		float attenuation = distanceFalloff / max(d * d, minDistSqr);

		// Per-pixel shadow term. Same depth maps the volumetric froxel pass
		// samples for this light - so a surface pixel behind a wall inside
		// the light's radius goes dark instead of being lit through the wall,
		// matching the shadowed fog volume around it.
		float depthValue = CalculatePointShadowSample(pixelPosWS.xyz, normalWS, lightPos);

		float4 pbr = CalculatePBRPointLighting(
			GBUFFER_SPECULAR,
			g_pointSampler,
			screenPos,
			normalWS,
			pixelPosWS.xyz,
			lightToPixelVec,
			input.colour.rgb,
			pixelColour.rgb,
			depthValue,
			attenuation
		);

		// Per-model feature lobes (clearcoat / aniso / sheen). Same per-pixel
		// model id and params as the directional path - so a clearcoated car gets
		// sharp specular highlights from point lights the same way it gets them
		// from the sun.
		const float4 featurePx = GBUFFER_FEATURES.Sample(g_pointSampler, screenPos);
		const uint modelId = DecodeMaterialModelId(featurePx.r);
		if (modelId != MATERIAL_MODEL_STANDARD)
		{
			const float perceptualRoughnessForFeatures = clamp(GBUFFER_SPECULAR.Sample(g_pointSampler, screenPos).g, MinRoughness, 1.0f);
			const float3 viewDir = g_eyePos.xyz - pixelPosWS.xyz;
			pbr.rgb += ApplyMaterialFeatures(
				modelId,
				float4(featurePx.g, featurePx.b, featurePx.a, DecodePackedModelParamW(featurePx.r)),
				normalWS,
				viewDir,
				lightToPixelVec,
				input.colour.rgb,
				perceptualRoughnessForFeatures,
				depthValue,
				attenuation);
		}

		float3 lightContribution = max(pbr.rgb * lightIntensity, 0.0f);
		lightContribution += volumetricContribution;
		if (!all(isfinite(lightContribution)))
			lightContribution = 0.0f.xxx;
		return float4(lightContribution, 1.0f);
	}
}
