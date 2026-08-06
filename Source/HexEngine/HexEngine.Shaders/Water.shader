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
	EnvMapCommon
}
"GlobalIncludes"
{
	Global
}
"VertexShader"
{
#define ENABLE_WAVES 1

	float3 GerstnerWave(
		float4 wave, float3 p, inout float3 tangent, inout float3 binormal,
		inout float crest
	) {
		float steepness = wave.z / WaveSizeMultiplier;
		float wavelength = wave.w / WaveSizeMultiplier;
		float k = 2 * 3.14159f / wavelength;
		float c = sqrt(9.8 / k);
		float2 d = normalize(wave.xy);
		float f = k * (dot(d, p.xz) - c * g_time * 4.2f);

		float a = steepness / k;

		// Crest proxy for foam (O4): steepness-weighted phase height. Where
		// several waves peak together this sum approaches the total
		// steepness; troughs go negative. Normalised in ShaderMain.
		crest += steepness * sin(f);

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

		float crest = 0.0f;

#if ENABLE_WAVES == 1
		p += GerstnerWave(_WaveA, gridPoint, tangent, binormal, crest);
		p += GerstnerWave(_WaveB, gridPoint, tangent, binormal, crest);
		p += GerstnerWave(_WaveC, gridPoint, tangent, binormal, crest);
		p += GerstnerWave(_WaveD, gridPoint, tangent, binormal, crest);

		tangent = normalize(tangent);
		binormal = normalize(binormal);

		normal = normalize(cross(binormal, tangent));
#endif

		// Normalise the crest sum to [0,1] against the theoretical maximum
		// (all four waves peaking in phase). Only the positive half matters -
		// foam forms on crests, not in troughs.
		const float totalSteepness =
			(_WaveA.z + _WaveB.z + _WaveC.z + _WaveD.z) / WaveSizeMultiplier;
		const float crest01 = saturate(crest / max(totalSteepness, 0.0001f));

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

		// The colour interpolant is repurposed (O4): .x carries the crest
		// factor for foam. (Instance colour was only ever multiplied into a
		// dead albedo sample - water's surface is entirely procedural.)
		output.colour = float4(crest01, 0.0f, 0.0f, 1.0f);

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

	// Prefiltered sky environment atlas - the same environment the deferred
	// IBL and glass use, so the sea and every other surface agree about what
	// the sky looks like (a storm sky reflects as overcast, not clear blue).
	Texture2D g_iblSkyEnvFwd : register(t14);

	// Sun cascade shadow maps - bound pass-wide for transparents at t15+
	// (SceneRenderer::RenderTransparent), same slots DefaultPixel uses.
	SHADOWMAPS_RESOURCE(15);

	SamplerState g_TexSamplerAniso : register(s0);
	SamplerComparisonState g_cmpSampler : register(s1);
	SamplerState g_TexSamplerPoint : register(s2);

	// Inline screen-space reflection for water (O3) - the DefaultPixel
	// transparency pattern (third copy; dedup across DefaultPixel /
	// DefaultAnimated / here is a tracked follow-up). Replaces the legacy
	// 24-step self-contained march this shader carried since before the SSR
	// stack existed. Water-specific tuning: a wider thickness window (the
	// reflecting surface is a DISPLACED wavy plane, so ray/depth
	// disagreements up to a wave amplitude are normal, not misses).
	bool TraceWaterSSR(float3 surfaceWorldPos, float3 reflectDirWorld,
		out float3 reflectedColour, out float hitConfidence)
	{
		reflectedColour = float3(0.0f, 0.0f, 0.0f);
		hitConfidence = 0.0f;

		const int kMaxSteps = 48;
		const float kStrideWorld = 0.12f;    // world-space step length, scaled by distance below
		const float kThicknessWorld = 0.6f;  // wider than glass's 0.35 - see header comment

		// Step length grows with distance from camera so distant rays don't take many steps.
		const float distFromEye = length(g_eyePos.xyz - surfaceWorldPos);
		const float strideWorld = kStrideWorld * max(0.5f, distFromEye * 0.08f);

		float3 rayPos = surfaceWorldPos + reflectDirWorld * (strideWorld * 0.5f);

		[loop]
		for (int step = 0; step < kMaxSteps; ++step)
		{
			rayPos += reflectDirWorld * strideWorld;

			// Project the ray sample into clip / screen space.
			const float4 clip = mul(float4(rayPos, 1.0f), g_viewProjectionMatrix);
			if (clip.w <= 0.0f)
				return false;
			const float2 ndc = clip.xy / clip.w;
			if (any(abs(ndc) > 1.0f))
				return false;

			const float2 uv = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);

			// Compare ray's view-space depth with the opaque scene at the same
			// UV (normal.w carries view depth; -1/far = sky).
			const float rayViewZ = -mul(float4(rayPos, 1.0f), g_viewMatrix).z;
			const float sceneViewZ = g_sceneNormalTex.SampleLevel(g_TexSamplerPoint, uv, 0).w;

			// Skip the sky / very far depth.
			if (sceneViewZ <= 0.0f || sceneViewZ >= g_frustumDepths[3] * 0.999f)
				continue;

			const float dz = rayViewZ - sceneViewZ;
			if (dz > 0.0f && dz < kThicknessWorld)
			{
				reflectedColour = g_sceneColourTex.SampleLevel(g_TexSamplerPoint, uv, 0).rgb;
				// Fade out near screen edges to hide the missing-data band.
				const float2 edgeFade = smoothstep(0.0f, 0.1f, uv) * smoothstep(0.0f, 0.1f, 1.0f - uv);
				hitConfidence = saturate(edgeFade.x * edgeFade.y);
				return true;
			}
		}
		return false;
	}

	// Screen-space refraction: offset the scene-colour lookup along the
	// refracted direction, depth-rejected so geometry NEARER than the water
	// surface never smears into the refraction. Rebuilt properly in O4.
	float4 GetWorldColour(float3 eyeDir, inout float2 screenPos, float3 worldNormal, float4 originalWorldDiffuse, float3 pixelPos, float pixelDepth, float offsetScale)
	{
		float eta = 0.75f;

		float2 origScreenPos = screenPos;

		float3 refractedNormal = refract(eyeDir, -(worldNormal), eta);

		// Project the refracted DIRECTION and use it as a screen-space UV
		// offset, SCALED by the water-column depth (offsetScale): centimetres
		// of water over a shore stone barely displace it, a deep column bends
		// hard. The unscaled version smeared the shoreline.
		float4 jitterNormal = float4(refractedNormal, 0.0f);
		jitterNormal = mul(jitterNormal, g_viewProjectionMatrix);

		const float jitterAmmount = 0.018f;

		jitterNormal = jitterNormal * (jitterAmmount * offsetScale);

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
			// Pre-refraction column estimate just for the offset scale (the
			// accurate metre-based column is computed below at the final UV).
			const float preColumn = (worldDepth == -1.0f) ? 100.0f : (worldDepth - pixelDepth);
			const float refractOffsetScale = saturate(preColumn * 0.6f);

			float4 refractedWorldDiffuse = GetWorldColour(-eyeVector, screenPos, refractionNormal, worldDiffuse, input.positionWS.xyz, pixelDepth, refractOffsetScale);
			worldDiffuse = lerp(originalWorldDiffuse, refractedWorldDiffuse, refractionQualityWeight);

			// re-read depth at the refracted position so the shore/absorption
			// terms below use the surface the refraction actually shows
			normalAndDepth = g_sceneNormalTex.Sample(g_TexSamplerPoint, screenPos);
			worldDepth = normalAndDepth.w;
		}

		// Sun cascade shadows (O2): water was never shadowed - a dock's shadow
		// stopped dead at the waterline while the sea sparkled underneath it.
		// Same cheap-PCF + gate as DefaultPixel's transparency path (the
		// cascades + b2 caster constants are only valid when the pass bound
		// them; g_taaParams.z carries that).
		float sunShadow = 1.0f;
		if (g_taaParams.z > 0.5f)
		{
			const float ndl = dot(worldNormal, normalize(g_shadowCasterLightDir.xyz));
			const float shadowBias = g_shadowConfig.biasMultiplier * (1.0f - ndl);
			sunShadow = CalculateShadowsCheapPCF(input.positionWS.xyz, g_cmpSampler, SHADOWMAPS, shadowBias);
		}

		float lightIntensity = dot(worldNormal, lightDir) * g_globalLight[0] * sunShadow;

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
			// Sun radiance in the same units the glass path uses
			// (getSunColour() x g_globalLight[0], Frostbite convention) instead
			// of the ad-hoc ComputePhysicalSunColour x5.75 boost that was
			// compensating for the old LDR clamp. Shadowed like the body term.
			specular = float4(getSunColour() * g_globalLight[0]
				* (NdotL * directSpecular) * sunShadow, 1.0f);
		}

		// Water column (O4): METRES of water along the view path, from the
		// opaque world position behind the surface. The legacy terms
		// normalised the view-depth difference by the FAR PLANE, so every
		// absorption knob was scene-scale dependent and the first metre of
		// water - where all the shore detail lives - occupied a sliver of the
		// parameter range.
		const float3 scenePosWS = g_scenePositionTex.Sample(g_TexSamplerPoint, screenPos).xyz;
		const float columnDepth = (worldDepth <= 0.0f)
			? 500.0f
			: max(distance(scenePosWS, input.positionWS.xyz), 0.0f);

		const float fresnelPow = g_oceanConfig.fresnelPow;

		// Beer-Lambert absorption per METRE. reflection_pad0 overrides when
		// the scene sets it (> 0); the 0.18/m default reads as coastal sea.
		const float absorbK = g_oceanConfig.reflection_pad0 > 0.0f ? g_oceanConfig.reflection_pad0 : 0.18f;
		float transmission = exp(-columnDepth * absorbK);

		float4 fadeColour = lerp(g_oceanConfig.shallowColour, g_oceanConfig.deepColour, saturate(1.0f - transmission));
		// SCHLICK Fresnel, not the legacy 1-cos^pow: that curve sat near 0.98
		// at ordinary viewing angles, so the exponent shifted the WHOLE sea's
		// reflectivity (user: "fresnel factor has a lot of influence on how
		// reflective the water is overall"). Real water reflects ~2% at
		// normal incidence and only approaches a mirror toward grazing -
		// which also means you can finally see INTO the water near the
		// camera. fresnelPow now shapes the grazing rise (5 = physical;
		// lower = reflectivity comes in earlier).
		float fresnel = 0.02f + 0.98f * pow(1.0f - saturate(dot(eyeVector, originalWorldNormal)), max(fresnelPow, 0.5f));
		// Shore fade: scaled so the strength slider works over METRES-scale
		// shallows (the raw per-metre form saturated in ~25 cm and the
		// control did nothing - the legacy far-plane form is why it used to
		// feel useful). Default 12 -> full effect by ~8 m of column.
		float shoreFade = 1.0f - exp(-columnDepth * g_oceanConfig.shoreFadeStrength / 30.0f);

		float fadeFactor = saturate(fresnel * shoreFade);

		// Procedural foam (O4): crest foam where the waves peak (VS crest
		// interpolant) + a shore band where the column is centimetres deep.
		// No foam textures exist in the project - two octaves of ValueNoise3
		// shape both, advected slowly so the pattern churns. reflection_pad1
		// scales overall coverage (> 0 to override).
		float foam = 0.0f;
		{
			const float foamScale = g_oceanConfig.reflection_pad1 > 0.0f ? g_oceanConfig.reflection_pad1 : 1.0f;
			// SLOW noise churn. The first version advected at 0.22 and put a
			// hard smoothstep threshold on the crest factor - which
			// oscillates at wave-phase speed - so foam snapped on/off as each
			// crest swept past the threshold and the sea strobed ("looks
			// like lightning"). Continuous power curves + slow erosion make
			// foam wax and wane with the swell instead of flashing.
			const float3 np = float3(input.positionWS.x * 0.35f, g_time * 0.06f, input.positionWS.z * 0.35f);
			const float n = ValueNoise3(np) * 0.65f + ValueNoise3(np * 3.1f + float3(0.0f, g_time * 0.03f, 0.0f)) * 0.35f;

			// Crests: continuous cubic response (no threshold to flash
			// across), noise shaping the coverage into streaks.
			const float crest = saturate(input.colour.x);
			const float crestFoam = pow(crest, 3.0f) * (0.35f + 0.65f * n);

			// Shore: strongest at zero depth, fading over the first ~1.5 m,
			// continuous curve, noise-broken.
			const float shoreBand = saturate(1.0f - columnDepth / 1.5f);
			const float shoreFoam = shoreBand * shoreBand * (0.45f + 0.7f * n);

			foam = saturate((crestFoam + shoreFoam) * foamScale);
		}

		float4 ambient = float4(g_atmosphere.ambientLight.rgb * fadeColour.rgb, 1.0f);
		float4 diffuseColour = float4(fadeColour.rgb * lightIntensity, 1.0f);

		float4 finalColour = diffuseColour;

		float finalFadeFactor = fadeFactor;

		if (g_eyePos.y <= 0.0f)
			finalFadeFactor *= 0.35f;

		float3 transmittedColour = lerp(fadeColour.rgb, worldDiffuse.rgb, transmission);
		float3 waterBodyColour = lerp(transmittedColour, finalColour.rgb + (ambient.rgb * 0.35f), finalFadeFactor);
		float4 retCol = float4(waterBodyColour, 1.0f);

		// Reflections (O3): inline screen-space march for near-field content
		// + prefiltered sky-atlas fallback everywhere the march misses (off-
		// screen, behind camera, beyond march range, and the whole far sea -
		// the atlas is weather-tinted, so a storm sky reflects as OVERCAST).
		// This replaced both the legacy 24-step march and the "cheap
		// reflection" (beauty at the pixel's own position - positionally
		// meaningless, it reflected whatever was BEHIND the water).
		{
			float3 reflectionNormal = worldNormal;

			if (g_eyePos.y <= 0.0f)
				reflectionNormal *= -1.0f;

			const float3 R = normalize(reflect(-eyeVector, reflectionNormal));

			float3 reflection = float3(0.0f, 0.0f, 0.0f);
			float reflectionWeight = 0.0f;

			// Near-field: march the opaque depth. Distance-gated - far rays
			// take the env path directly (matches the old ssrQualityWeight
			// ramp and keeps the horizon cheap).
			if (ssrQualityWeight > 0.001f)
			{
				float3 ssrColour;
				float ssrConfidence;
				if (TraceWaterSSR(input.positionWS.xyz, R, ssrColour, ssrConfidence))
				{
					reflectionWeight = ssrConfidence * ssrQualityWeight;
					reflection = ssrColour;
				}
			}

			// Environment fallback wherever the march found nothing. Sharp
			// atlas row - water reflection roughness is near-mirror until the
			// wind-coupled roughness lands in O5.
			{
				const float kWaterEnvRoughness = 0.08f;
				const float3 envColour = SampleEnvAtlas(g_iblSkyEnvFwd, g_TexSamplerAniso, R, kWaterEnvRoughness);
				// Downward rays would pick up horizon sky the atlas has no
				// ground radiance for.
				const float envHorizon = saturate(R.y * 3.0f + 0.35f);
				const float envWeight = (1.0f - reflectionWeight) * envHorizon;

				reflection = reflection * reflectionWeight + envColour * envWeight;
				reflectionWeight = saturate(reflectionWeight + envWeight);
				reflection = reflectionWeight > 1e-4f ? reflection / reflectionWeight : float3(0.0f, 0.0f, 0.0f);
			}

			// Compose: reflection replaces body colour by Fresnel x shore
			// fade x artist strength; the sun glint ADDS on top (its GGX F
			// term already carries its own Fresnel - the legacy code scaled
			// the glint by reflectionStrength, which is why the boost
			// constant existed). Foam suppresses both - scattered white
			// water is matte, not a mirror.
			// NO saturate: linear HDR into an R16G16B16A16_FLOAT target.
			const float reflectionStrength = g_oceanConfig.reflectionStrength;
			retCol.xyz = lerp(retCol.xyz, reflection,
				reflectionStrength * fadeFactor * reflectionWeight * (1.0f - foam));
			retCol.xyz += specular.xyz * (1.0f - foam);
		}

		// Foam sits ON the surface: matte white water lit by ambient + sun
		// diffuse (shadowed), replacing whatever is beneath it.
		if (foam > 0.001f)
		{
			const float foamNdl = saturate(dot(worldNormal, lightDir));
			const float3 foamLit = float3(0.86f, 0.88f, 0.90f)
				* (g_atmosphere.ambientLight.rgb
					+ getSunColour() * g_globalLight[0] * foamNdl * sunShadow);
			retCol.xyz = lerp(retCol.xyz, foamLit, foam);
		}

		retCol.a = 1.0f;

		return retCol;
	}
}
