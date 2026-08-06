"Requirements"
{
	// EMPTY on purpose (O1 modernisation). The legacy flags (GBuffer, Beauty)
	// made Scene::RenderInstance bind gbuffer t0-t4 + beauty t5 via the
	// implicit slot counter, pushing the material textures up to t6..t13 -
	// colliding with the modern transparent-pass binds (scene colour t10,
	// gbuffer normal t12 / position t13, sky atlas t14, sun cascades t15+,
	// SceneRenderer::RenderTransparent ~:5259-5309). With no requirements the
	// implicit counter starts at 0: material textures land at the STANDARD
	// t0..t7 slots (same as Default.shader) and the pass binds are read
	// directly at their explicit registers below.
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
	Utils
	Atmosphere
	AtmospherePhysical
	PBRutils
}
"GlobalIncludes"
{
	Global
}
"VertexShader"
{
#define ENABLE_WAVES 1

	float3 GerstnerWave(
		float4 wave, float3 p, inout float3 tangent, inout float3 binormal
	) {
		float steepness = wave.z / WaveSizeMultiplier;
		float wavelength = wave.w / WaveSizeMultiplier;
		float k = 2 * 3.14159f / wavelength;
		float c = sqrt(9.8 / k);
		float2 d = normalize(wave.xy);
		float f = k * (dot(d, p.xz) - c * g_time * 4.2f);

		float a = steepness / k;

		tangent += float3(
			-d.x * d.x * (steepness * sin(f)),
			d.x * (steepness * cos(f)),
			-d.x * d.y * (steepness * sin(f))
			);
		binormal += float3(
			-d.x * d.y * (steepness * sin(f)),
			d.y * (steepness * cos(f)),
			-d.y * d.y * (steepness * sin(f))
			);

		return float3(
			d.x * (a * cos(f)),
			a * sin(f),
			d.y * (a * cos(f))
			);
	}

	MeshPixelInput ShaderMain(MeshVertexInput input, MeshInstanceData instance, uint instanceID : SV_INSTANCEID)
	{
		MeshPixelInput output = (MeshPixelInput)0;

		input.position.w = 1.0f;

		float3 worldPos = instance.world[3].xyz;

		// Waves are evaluated in WORLD space so the 92 sea tiles stay
		// seamless - a shared edge vertex computes the same displacement on
		// both sides regardless of which tile draws it.
		float3 gridPoint = input.position.xyz + worldPos;

		float3 tangent = input.tangent;
		float3 binormal = input.binormal;
		float3 normal = input.normal;
		float3 p = gridPoint;

#if ENABLE_WAVES == 1
		p += GerstnerWave(_WaveA, gridPoint, tangent, binormal);
		p += GerstnerWave(_WaveB, gridPoint, tangent, binormal);
		p += GerstnerWave(_WaveC, gridPoint, tangent, binormal);
		p += GerstnerWave(_WaveD, gridPoint, tangent, binormal);

		tangent = normalize(tangent);
		binormal = normalize(binormal);

		normal = normalize(cross(binormal, tangent));
#endif

		input.position = float4(p.xyz - worldPos, 1.0f);

		input.normal = normal;
		input.binormal = binormal;
		input.tangent = tangent;

		output.position = mul(input.position, instance.world);
		output.positionWS = output.position;

		output.position = mul(output.position, g_viewProjectionMatrix);

		input.texcoord.xy -= g_time * 0.03f;
		output.texcoord = input.texcoord * 1.4;

		matrix normalMatrix = mul(instance.worldInverseTranspose, g_worldMatrix);

		output.normal = mul(input.normal, (float3x3)normalMatrix);
		output.normal = normalize(output.normal);

		output.tangent = mul(input.tangent, (float3x3)normalMatrix);
		output.tangent = normalize(output.tangent);

		output.binormal = mul(input.binormal, (float3x3)normalMatrix);
		output.binormal = normalize(output.binormal);

		// Determine the viewing direction based on the position of the camera and the position of the vertex in the world.
		output.viewDirection.xyz = g_eyePos.xyz - output.positionWS.xyz;

		// Normalize the viewing direction vector.
		output.viewDirection.xyz = normalize(output.viewDirection.xyz);

		output.colour = instance.colour;

		return output;
	}
}
"PixelShader"
{
	// Material textures at the STANDARD implicit slots (empty Requirements ->
	// the counter starts at 0; same array order every material uses:
	// Albedo/Normal/Roughness/Metallic/Height/Emission/Opacity/AO).
	Texture2D g_albedoMap : register(t0);
	Texture2D g_normalMap : register(t1);

	// Transparent-pass binds (SceneRenderer::RenderTransparent ~:5259-5309;
	// same registers DefaultPixel's transparency path reads).
	// Scene colour: the pre-transparency opaque beauty snapshot.
	Texture2D g_sceneColourTex : register(t10);
	// Opaque gbuffer normal: xyz = world normal, w = view depth (-1 = sky).
	Texture2D g_sceneNormalTex : register(t12);
	// Opaque gbuffer position: xyz = world position.
	Texture2D g_scenePositionTex : register(t13);

	SamplerState g_TexSamplerAniso : register(s0);
	SamplerComparisonState g_cmpSampler : register(s1);
	SamplerState g_TexSamplerPoint : register(s2);

	// Legacy self-contained reflection march (predates the SSR stack - it is
	// this shader's pattern that SSR.shader ported). Replaced by the inline
	// transparent SSR + env fallback in slice O3; retargeted to the modern
	// binds until then.
	float4 GetReflection(float3 eyeDir, float3 worldPos, float3 worldNormal, float4 originalColour, float currentDepth)
	{
		float3 rayStart = worldPos + worldNormal * 0.25f;
		float3 rayDir = normalize(reflect(eyeDir, worldNormal));

		const int stepCount = 24;
		const int refinementStepCount = 5;
		const float minStepLen = 2.0f;
		const float maxStepLen = 8.0f;
		const float baseThickness = 2.0f;

		float3 fragPos = rayStart;
		float3 previousFragPos = fragPos;
		float totalDistanceTravelled = 0.0f;
		float previousDistanceTravelled = 0.0f;
		float2 texCoord = 0.0f;
		float actualDepth = currentDepth;

		[loop]
		for (int i = 0; i < stepCount; ++i)
		{
			const float marchFraction = (float)i / (float)(stepCount - 1);
			const float stepLen = lerp(minStepLen, maxStepLen, marchFraction * marchFraction);
			const float thickness = baseThickness + totalDistanceTravelled * 0.02f;

			previousFragPos = fragPos;
			previousDistanceTravelled = totalDistanceTravelled;
			fragPos += rayDir * stepLen;
			totalDistanceTravelled += stepLen;

			float4 fragScr = float4(fragPos.xyz, 1.0f);
			float4 fragView = mul(fragScr, g_viewMatrix);
			float4 fragClip = mul(fragView, g_projectionMatrix);
			fragClip.xyz /= fragClip.w;

			float fragDepth = -fragView.z;
			fragClip.xy = fragClip.xy * 0.5 + 0.5;
			float2 fragTex = float2(fragClip.x, 1.0f - fragClip.y);

			if (fragTex.x < 0.0f || fragTex.x > 1.0f || fragTex.y < 0.0f || fragTex.y > 1.0f)
				return originalColour;

			actualDepth = g_sceneNormalTex.Sample(g_TexSamplerPoint, fragTex).w;
			float fragHeight = g_scenePositionTex.Sample(g_TexSamplerPoint, fragTex).y;
			bool isOnCorrectPlane = fragHeight >= rayStart.y;

			if (g_eyePos.y <= 0.0f)
				isOnCorrectPlane = fragHeight < rayStart.y;

			if ((fragDepth >= actualDepth - thickness) && isOnCorrectPlane && actualDepth > currentDepth)
			{
				float3 refineStart = previousFragPos;
				float3 refineEnd = fragPos;
				float refineStartDistance = previousDistanceTravelled;
				float refineEndDistance = totalDistanceTravelled;

				[loop]
				for (int j = 0; j < refinementStepCount; ++j)
				{
					float3 candidatePos = lerp(refineStart, refineEnd, 0.5f);
					float candidateDistance = lerp(refineStartDistance, refineEndDistance, 0.5f);
					float candidateThickness = baseThickness + candidateDistance * 0.02f;

					float4 candidateScr = float4(candidatePos.xyz, 1.0f);
					float4 candidateView = mul(candidateScr, g_viewMatrix);
					float4 candidateClip = mul(candidateView, g_projectionMatrix);
					candidateClip.xyz /= candidateClip.w;

					float candidateDepth = -candidateView.z;
					candidateClip.xy = candidateClip.xy * 0.5 + 0.5;
					float2 candidateTex = float2(candidateClip.x, 1.0f - candidateClip.y);

					if (candidateTex.x < 0.0f || candidateTex.x > 1.0f || candidateTex.y < 0.0f || candidateTex.y > 1.0f)
					{
						refineEnd = candidatePos;
						refineEndDistance = candidateDistance;
						continue;
					}

					float candidateActualDepth = g_sceneNormalTex.Sample(g_TexSamplerPoint, candidateTex).w;
					float candidateHeight = g_scenePositionTex.Sample(g_TexSamplerPoint, candidateTex).y;
					bool candidatePlane = candidateHeight >= rayStart.y;
					if (g_eyePos.y <= 0.0f)
						candidatePlane = candidateHeight < rayStart.y;

					if ((candidateDepth >= candidateActualDepth - candidateThickness) && candidatePlane && candidateActualDepth > currentDepth)
					{
						refineEnd = candidatePos;
						refineEndDistance = candidateDistance;
						fragTex = candidateTex;
						actualDepth = candidateActualDepth;
					}
					else
					{
						refineStart = candidatePos;
						refineStartDistance = candidateDistance;
					}
				}

				return g_sceneColourTex.Sample(g_TexSamplerPoint, fragTex);
			}

			texCoord = fragTex;
		}

		if (actualDepth > currentDepth)
			return g_sceneColourTex.Sample(g_TexSamplerPoint, texCoord);

		return originalColour;
	}

	// Screen-space refraction: offset the scene-colour lookup along the
	// refracted direction, depth-rejected so geometry NEARER than the water
	// surface never smears into the refraction. Rebuilt properly in O4.
	float4 GetWorldColour(float3 eyeDir, inout float2 screenPos, float3 worldNormal, float4 originalWorldDiffuse, float3 pixelPos, float pixelDepth)
	{
		float eta = 0.75f;

		float2 origScreenPos = screenPos;

		float3 refractedNormal = refract(eyeDir, -(worldNormal), eta);

		// Project the refracted DIRECTION and use it as a screen-space UV
		// offset. Crude but stable; the real per-pixel march is O4's job.
		float4 jitterNormal = float4(refractedNormal, 0.0f);
		jitterNormal = mul(jitterNormal, g_viewProjectionMatrix);

		const float jitterAmmount = 0.018f;

		jitterNormal = jitterNormal * jitterAmmount;

		screenPos = screenPos + jitterNormal.xy;

		if (screenPos.x < 0.0f || screenPos.x > 1.0f || screenPos.y < 0.0f || screenPos.y > 1.0f)
		{
			screenPos = origScreenPos;
			return originalWorldDiffuse;
		}

		float fragDepth = g_sceneNormalTex.Sample(g_TexSamplerPoint, screenPos).w;

		if (fragDepth < pixelDepth)
		{
			screenPos = origScreenPos;
			return originalWorldDiffuse;
		}

		// resample the scene at the refracted position
		float4 jitterDiffuse = g_sceneColourTex.Sample(g_TexSamplerPoint, screenPos);
		jitterDiffuse.a = 1.0f;

		return jitterDiffuse;
	}

	// Tangent-space normal mapping with the CORRECT [0,1] -> [-1,1] unpack.
	// The legacy version had the expansion commented out, so the raw texel
	// fed the TBN mix and the resulting basis was biased toward +tangent
	// +binormal - water bump normals have been mathematically wrong for
	// years (visibly: lighting that never quite tracked the waves).
	float3 ANM(float3 worldNormal, float3 tangent, float3 binormal, Texture2D normalTex, SamplerState samp, float2 texcoord)
	{
		float3 bumpMap = normalTex.Sample(samp, texcoord).xyz;

		// Expand the range of the normal value from (0, +1) to (-1, +1).
		bumpMap = (bumpMap * 2.0f) - 1.0f;

		float3 bumpNormal =
			(bumpMap.x * tangent) +
			(bumpMap.y * binormal) +
			(bumpMap.z * worldNormal);

		return normalize(bumpNormal);
	}

	float4 ShaderMain(MeshPixelInput input) : SV_Target
	{
		float4 albedo = g_albedoMap.Sample(g_TexSamplerAniso, input.texcoord) * input.colour;

		float4 specular = float4(0, 0, 0, 1);

		float3 eyeVector = normalize(g_eyePos.xyz - input.positionWS.xyz);
		const float cameraDistance = length(input.positionWS.xyz - g_eyePos.xyz);
		const float nearQualityDistance = g_oceanConfig.reflectionNearDistance;
		const float midQualityDistance = lerp(g_oceanConfig.reflectionNearDistance, g_oceanConfig.reflectionFarDistance, 0.6f);
		const float farQualityDistance = max(g_oceanConfig.reflectionFarDistance, midQualityDistance + 1.0f);
		const float refractionQualityWeight = 1.0f - saturate((cameraDistance - nearQualityDistance) / max(midQualityDistance - nearQualityDistance, 1.0f));
		const float ssrQualityWeight = 1.0f - saturate((cameraDistance - nearQualityDistance) / max(farQualityDistance - nearQualityDistance, 1.0f));
		const float distantNormalFade = ssrQualityWeight;

		float3 worldNormal = normalize(input.normal.xyz);
		float3 refractionNormal = -worldNormal;

		float3 originalWorldNormal = worldNormal;

		float3 lightDir = -normalize(g_lightDirection.xyz);

		float4 worldViewPosition = mul(input.positionWS, g_viewMatrix);
		float pixelDepth = -worldViewPosition.z;

		float2 screenPos = float2(input.position.x / (float)g_screenWidth, input.position.y / (float)g_screenHeight);

		// The pre-transparency opaque scene colour at this pixel.
		float4 worldDiffuse = g_sceneColourTex.Sample(g_TexSamplerPoint, screenPos);
		worldDiffuse.a = 1.0f;

		// make a copy, we might need this again
		float4 originalWorldDiffuse = worldDiffuse;

		// BUMP MAPPING - fades out with distance (far water keeps the smooth
		// Gerstner normal; per-texel detail at the horizon just aliases).
		if (distantNormalFade > 0.001f)
		{
			float3 bumpNormal = ANM(worldNormal, input.tangent, input.binormal, g_normalMap, g_TexSamplerAniso, input.texcoord);
			bumpNormal = normalize(lerp(input.normal.xyz, bumpNormal, distantNormalFade));

			refractionNormal = bumpNormal;
			worldNormal = bumpNormal;
		}

		float4 normalAndDepth = g_sceneNormalTex.Sample(g_TexSamplerPoint, screenPos);
		float worldDepth = normalAndDepth.w;

		if (refractionQualityWeight > 0.001f && (worldDepth >= pixelDepth || worldDepth == -1.0f))
		{
			float4 refractedWorldDiffuse = GetWorldColour(-eyeVector, screenPos, refractionNormal, worldDiffuse, input.positionWS.xyz, pixelDepth);
			worldDiffuse = lerp(originalWorldDiffuse, refractedWorldDiffuse, refractionQualityWeight);

			// re-read depth at the refracted position so the shore/absorption
			// terms below use the surface the refraction actually shows
			normalAndDepth = g_sceneNormalTex.Sample(g_TexSamplerPoint, screenPos);
			worldDepth = normalAndDepth.w;
		}

		float lightIntensity = dot(worldNormal, lightDir) * g_globalLight[0];

		if (lightIntensity > 0.0f)
		{
			const float waterPerceptualRoughnessBase = 0.08f;
			const float waterMetallic = 0.0f;
			const float3 viewDir = normalize(g_eyePos.xyz - input.positionWS.xyz);
			const float3 halfVector = normalize(lightDir + viewDir);
			const float3 specularNormal = normalize(lerp(originalWorldNormal, worldNormal, 0.35f));
			const float NdotL = clamp(dot(specularNormal, lightDir), 0.001f, 1.0f);
			const float NdotV = abs(dot(specularNormal, viewDir)) + 0.001f;
			const float NdotH = saturate(dot(specularNormal, halfVector));
			const float VdotH = saturate(dot(viewDir, halfVector));
			float waterPerceptualRoughness = ApplySpecularAntiAliasing(specularNormal, waterPerceptualRoughnessBase);
			const float alphaRoughness = waterPerceptualRoughness * waterPerceptualRoughness;
			const float3 specularColor = lerp(f0, float3(1.0f, 1.0f, 1.0f), waterMetallic);
			const float reflectance = max(max(specularColor.r, specularColor.g), specularColor.b);
			const float reflectance90 = saturate(reflectance * 25.0f);
			const float3 F = specularReflection(specularColor, float3(1.0f, 1.0f, 1.0f) * reflectance90, VdotH);
			const float G = geometricOcclusion(NdotL, NdotV, alphaRoughness);
			const float D = microfacetDistribution(NdotH, alphaRoughness);
			const float3 directSpecular = F * G * D / max(4.0f * NdotL * NdotV, 0.001f);
			const float sunSpecularBoost = 5.75f;
			specular = float4(ComputePhysicalSunColour(input.positionWS.xyz, lightDir) * (NdotL * directSpecular * sunSpecularBoost), 1.0f);
		}

		// Water-column depth terms from the opaque scene behind the surface.
		float waterDepth = pixelDepth;
		float depthDifference = (worldDepth - waterDepth);
		float relativeDepth = worldDepth == -1.0f ? 1.0f : saturate(depthDifference / g_frustumDepths[3]);

		const float fresnelPow = g_oceanConfig.fresnelPow;
		const float shoreFadeStrength = g_oceanConfig.shoreFadeStrength;

		float depthMultiplier = saturate(relativeDepth * g_frustumDepths[3]);
		float transmission = exp(-relativeDepth * max(g_oceanConfig.fadeFactor, 0.001f));

		float4 fadeColour = lerp(g_oceanConfig.shallowColour, g_oceanConfig.deepColour, saturate(1 - exp(-relativeDepth * g_oceanConfig.fadeFactor)));
		float fresnel = 1 - pow(saturate(dot(eyeVector, originalWorldNormal)), fresnelPow);
		float shoreFade = 1.0f - exp(-relativeDepth * shoreFadeStrength);

		float fadeFactor = saturate(fresnel * shoreFade);

		float4 ambient = float4(g_atmosphere.ambientLight.rgb * fadeColour.rgb, 1.0f);
		float4 diffuseColour = float4(fadeColour.rgb * lightIntensity, 1.0f);

		float4 finalColour = diffuseColour;

		float finalFadeFactor = fadeFactor;

		if (g_eyePos.y <= 0.0f)
			finalFadeFactor *= 0.35f;

		float3 transmittedColour = lerp(fadeColour.rgb, worldDiffuse.rgb, transmission);
		float3 waterBodyColour = lerp(transmittedColour, finalColour.rgb + (ambient.rgb * 0.35f), finalFadeFactor);
		float4 retCol = float4(waterBodyColour, 1.0f);

		{
			float3 reflectionNormal = worldNormal;

			if (g_eyePos.y <= 0.0f)
				reflectionNormal *= -1.0f;

			const float reflectionStrength = g_oceanConfig.reflectionStrength;

			float4 cheapReflectionCol = g_sceneColourTex.Sample(g_TexSamplerPoint, screenPos);
			cheapReflectionCol.xyz = lerp(cheapReflectionCol.xyz, fadeColour.xyz, 0.25f);

			float4 reflectionCol = cheapReflectionCol;
			if (ssrQualityWeight > 0.001f)
			{
				float4 ssrReflectionCol = GetReflection(-eyeVector, input.positionWS.xyz, reflectionNormal, retCol, pixelDepth);
				reflectionCol = lerp(cheapReflectionCol, ssrReflectionCol, ssrQualityWeight);
			}

			// NO saturate: the beauty target is R16G16B16A16_FLOAT and every
			// input here is linear HDR. The legacy double LDR clamp crushed
			// sun glints and bright reflections to 1.0, which is why water
			// always read dull next to lit geometry.
			retCol.xyz = lerp(retCol.xyz, reflectionCol.xyz + specular.xyz, reflectionStrength * fadeFactor);
		}

		retCol.a = 1.0f;

		return retCol;
	}
}
