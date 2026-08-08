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
	WaterCommon
}
"HullShaderIncludes"
{
	MeshCommon
	WaterCommon
}
"DomainShaderIncludes"
{
	MeshCommon
	WaterCommon
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
	// Tessellation VS (O7): no waves, no projection - transform each
	// control point to WORLD space and hand it on. The 17x17-vert sea
	// tiles have ~8 m triangles; the wave shape now comes from the
	// subdivided domain-shader evaluation instead of being carried by the
	// coarse grid.
	WaterCP ShaderMain(MeshVertexInput input, MeshInstanceData instance, uint instanceID : SV_INSTANCEID)
	{
		WaterCP o;
		input.position.w = 1.0f;

		o.worldPos  = mul(input.position, instance.world).xyz;
		o.worldPrev = mul(input.position, instance.worldPrev).xyz;

		// Bump advection via the CPU-INTEGRATED wind-scroll phase
		// (g_timeParams2.zw) - the stateless dir x g_time x rate form slews
		// during weather transitions; an integral cannot (see O5).
		o.texcoord = (input.texcoord - g_timeParams2.zw) * 1.4f;
		o.instanceID = instanceID + entityId;
		return o;
	}
}
"HullShader"
{
	// Distance LOD from the EDGE MIDPOINT in world space: a shared tile
	// edge computes identical factors on both sides regardless of which
	// tile draws it - that agreement is what keeps the 92-tile sea
	// watertight while it animates. Factor 16 over ~8 m triangles = ~0.5 m
	// segments near the camera, collapsing to the raw grid by ~165 m.
	float WaterTessFactor(float3 worldMid)
	{
		const float d = distance(worldMid, g_eyePos.xyz);
		return max(1.0f, lerp(16.0f, 1.0f, saturate((d - 15.0f) / 150.0f)));
	}

	WaterHSConst ConstantHS(InputPatch<WaterCP, 3> ip, uint pid : SV_PrimitiveID)
	{
		WaterHSConst o;
		const float3 c0 = ip[0].worldPos;
		const float3 c1 = ip[1].worldPos;
		const float3 c2 = ip[2].worldPos;
		// SV_TessFactor[i] governs the edge OPPOSITE control point i.
		o.edges[0] = WaterTessFactor((c1 + c2) * 0.5f);
		o.edges[1] = WaterTessFactor((c2 + c0) * 0.5f);
		o.edges[2] = WaterTessFactor((c0 + c1) * 0.5f);
		o.inside   = (o.edges[0] + o.edges[1] + o.edges[2]) / 3.0f;
		return o;
	}

	[domain("tri")]
	[partitioning("fractional_odd")]
	[outputtopology("triangle_cw")]
	[outputcontrolpoints(3)]
	[patchconstantfunc("ConstantHS")]
	WaterCP ShaderMain(InputPatch<WaterCP, 3> ip, uint i : SV_OutputControlPointID, uint pid : SV_PrimitiveID)
	{
		return ip[i];
	}
}
"DomainShader"
{
	// Subdivided wave evaluation (O7): EvalOcean at the interpolated world
	// position, at BOTH g_time (position/TBN/crest) and g_timePrev (motion
	// vectors) - exactly what the pre-tess VS did per grid vertex, but at
	// LOD'd density, so storm swell gets real shape and a broken
	// silhouette instead of riding 8 m triangles. LOW-frequency waves only
	// live here (the four-wave table); fine detail stays per-pixel bump -
	// the snow lesson: displacement must not contain frequencies the tess
	// density cannot represent.
	[domain("tri")]
	MeshPixelInput ShaderMain(WaterHSConst patchConst, float3 bary : SV_DomainLocation, const OutputPatch<WaterCP, 3> patch)
	{
		MeshPixelInput o = (MeshPixelInput)0;

		const float3 gridPos  = patch[0].worldPos  * bary.x + patch[1].worldPos  * bary.y + patch[2].worldPos  * bary.z;
		const float3 gridPrev = patch[0].worldPrev * bary.x + patch[1].worldPrev * bary.y + patch[2].worldPrev * bary.z;
		const float2 uv       = patch[0].texcoord  * bary.x + patch[1].texcoord  * bary.y + patch[2].texcoord  * bary.z;

		float2 windDir;
		float windAlign, ampScale;
		OceanWindParams(g_weatherSurface.windDirectionAndSpeed, g_oceanConfig2.x,
			windDir, windAlign, ampScale);

		float3 tangent, binormal;
		float crest01;
		const float3 p = EvalOcean(gridPos, g_time, windDir, windAlign, ampScale, tangent, binormal, crest01);
		const float3 normal = normalize(cross(binormal, tangent));

		// Storms foam harder: scale the crest factor the PS thresholds.
		crest01 *= saturate(0.35f + ampScale);

		o.positionWS = float4(p, 1.0f);
		o.position = mul(float4(p, 1.0f), g_viewProjectionMatrix);

		// Motion vectors: same evaluation at g_timePrev; the tiles are
		// static, so the displacement delta is the whole velocity.
		{
			float3 tPrev, bPrev;
			float cPrev;
			const float3 pPrev = EvalOcean(gridPrev, g_timePrev, windDir, windAlign, ampScale, tPrev, bPrev, cPrev);
			o.previousPositionUnjittered = mul(float4(pPrev, 1.0f), g_viewProjectionMatrixPrev);
		}
		o.currentPositionUnjittered = o.position;

		// TAA jitter, matching the standard VS.
		o.position.xy += g_jitterOffsets * o.position.w;

		o.texcoord = uv;
		o.normal = normal;
		o.tangent = tangent;
		o.binormal = binormal;
		o.viewDirection = float4(normalize(g_eyePos.xyz - p), 0.0f);
		// colour.x = crest factor for foam (O4).
		o.colour = float4(crest01, 0.0f, 0.0f, 1.0f);
		o.instanceID = patch[0].instanceID;
		o.cullDistance = 1.0f;
		return o;
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

		// The acceptance window must scale WITH the stride: a fixed 0.6 m
		// window under a ~1 m distant stride steps clean over thin geometry,
		// so one pixel hits (dark cliff) and its neighbour misses (bright
		// env) - the per-pixel speckle in distant reflections.
		const float thickness = max(kThicknessWorld, strideWorld * 1.5f);

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
			if (dz > 0.0f && dz < thickness)
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
	float3 ANM(float3 worldNormal, float3 tangent, float3 binormal, Texture2D normalTex, SamplerState samp, float2 texcoord, float strength)
	{
		float3 bumpMap = normalTex.Sample(samp, texcoord).xyz;

		// Expand the range of the normal value from (0, +1) to (-1, +1).
		bumpMap = (bumpMap * 2.0f) - 1.0f;

		// Strength scales the tangent-plane deflection only. NOTE (user-
		// found): the years-broken unpack was accidentally ATTENUATING the
		// map - raw [0,1] texels perturb at half amplitude around a constant
		// bias - so fixing it unleashed the texture at full strength and the
		// per-texel normal scatter shredded the SSR mirror image. The unpack
		// is correct; the amplitude needed an explicit dial (and the
		// reflection ray now uses a mostly-Gerstner normal besides).
		bumpMap.xy *= strength;

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
			// r_oceanBumpStrength: live normal-map deflection dial.
			float3 bumpNormal = ANM(worldNormal, input.tangent, input.binormal, g_normalMap, g_TexSamplerAniso, input.texcoord, g_oceanConfig2.w);
			bumpNormal = normalize(lerp(input.normal.xyz, bumpNormal, distantNormalFade));

			refractionNormal = bumpNormal;
			worldNormal = bumpNormal;
		}

		// Rain ripples (O6): impact rings dimple the surface while
		// precipitation falls. Gated out under snow (blizzard flakes don't
		// ring like raindrops). No shelter sampling - the shelter map isn't
		// bound in the transparent pass (Mesh.shader has the same
		// constraint), and open water is rarely sheltered.
		const float rainAmount = saturate(g_weatherSurface.precipitationIntensity)
			* saturate(1.0f - g_weatherSurface.snowCoverage * 3.0f);
		if (rainAmount > 0.001f)
		{
			worldNormal = ApplyRainRipples(worldNormal, input.positionWS.xyz, g_time, rainAmount);
			refractionNormal = worldNormal;
		}

		// Sea state for SHADING (the VS already couples wave geometry to this).
		// A storm sea must stay legible through CONTRAST, not mirror
		// reflection: in a blizzard the env atlas and the fog converge to the
		// same grey, so reflected-minus-body goes to ~zero and untreated water
		// simply vanishes into the weather (user screenshot). Wind therefore
		// drives: darker slate body, boosted whitecaps, rougher (softer,
		// broader) glints and blurrier env reflection.
		const float seaState = saturate(g_weatherSurface.windDirectionAndSpeed.w / 30.0f);

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
			// Glints soften and broaden as the sea roughens - a storm has no
			// razor-sharp sun line.
			const float waterPerceptualRoughnessBase = lerp(0.06f, 0.35f, seaState);
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

		// Beer-Lambert absorption per METRE. reflection_pad0 (per-scene)
		// overrides when set (> 0); otherwise the live r_oceanAbsorption cvar.
		const float absorbK = g_oceanConfig.reflection_pad0 > 0.0f ? g_oceanConfig.reflection_pad0 : max(g_oceanConfig2.z, 0.005f);
		float transmission = exp(-columnDepth * absorbK);

		// CONTROL SEPARATION (user-clarified semantics):
		//  - shoreFadeStrength: how fast DEPTH fades shallowColour->deepColour
		//  - absorption (pad0): how fast the refracted scene stops showing
		//    through (transmission above)
		//  - Fresnel ALONE decides reflectance - depth plays no part in it
		const float colourFade = 1.0f - exp(-columnDepth * g_oceanConfig.shoreFadeStrength / 30.0f);
		float4 fadeColour = lerp(g_oceanConfig.shallowColour, g_oceanConfig.deepColour, colourFade);
		// Storm seas read DARK SLATE - more absorption, less back-scatter.
		// This is also what keeps the water visible in a blizzard: the body
		// separates from the fog instead of matching it.
		fadeColour.rgb *= (1.0f - 0.45f * seaState);

		// SCHLICK Fresnel, not the legacy 1-cos^pow: that curve sat near 0.98
		// at ordinary viewing angles, so the exponent shifted the WHOLE sea's
		// reflectivity. Real water reflects ~2% at normal incidence and only
		// approaches a mirror toward grazing - which also means you can see
		// INTO the water near the camera. fresnelPow shapes the grazing rise
		// (5 = physical; lower = reflectivity comes in earlier).
		float fresnel = 0.02f + 0.98f * pow(1.0f - saturate(dot(eyeVector, originalWorldNormal)), max(fresnelPow, 0.5f));

		// Procedural foam (O4): crest foam where the waves peak (VS crest
		// interpolant) + a shore band where the column is centimetres deep.
		// No foam textures exist in the project - two octaves of ValueNoise3
		// shape both, advected slowly so the pattern churns. reflection_pad1
		// scales overall coverage (> 0 to override).
		float foam = 0.0f;
		{
			// Per-scene pad overrides when set; otherwise the live r_oceanFoam cvar.
			const float foamScale = g_oceanConfig.reflection_pad1 > 0.0f ? g_oceanConfig.reflection_pad1 : g_oceanConfig2.y;
			// SLOW noise churn. The first version advected at 0.22 and put a
			// hard smoothstep threshold on the crest factor - which
			// oscillates at wave-phase speed - so foam snapped on/off as each
			// crest swept past the threshold and the sea strobed ("looks
			// like lightning"). Continuous power curves + slow erosion make
			// foam wax and wane with the swell instead of flashing.
			const float3 np = float3(input.positionWS.x * 0.35f, g_time * 0.06f, input.positionWS.z * 0.35f);
			const float n = ValueNoise3(np) * 0.65f + ValueNoise3(np * 3.1f + float3(0.0f, g_time * 0.03f, 0.0f)) * 0.35f;

			// Crests: continuous response (no threshold to flash across),
			// noise shaping the coverage into streaks. The exponent RELAXES
			// with sea state - a storm sea whitecaps far below the theoretical
			// max crest, a calm sea only foams at true peaks.
			const float crest = saturate(input.colour.x);
			const float crestFoam = pow(crest, lerp(3.0f, 1.6f, seaState)) * (0.35f + 0.65f * n);

			// Shore: strongest at zero depth, fading over the first ~1.5 m,
			// continuous curve, noise-broken.
			const float shoreBand = saturate(1.0f - columnDepth / 1.5f);
			const float shoreFoam = shoreBand * shoreBand * (0.45f + 0.7f * n);

			// Whitecap coverage climbs with the wind - white water on the
			// darkened storm body is what keeps the sea legible when the sky,
			// fog and reflection all converge to grey.
			foam = saturate((crestFoam + shoreFoam) * foamScale * (1.0f + seaState));
		}

		float4 ambient = float4(g_atmosphere.ambientLight.rgb * fadeColour.rgb, 1.0f);
		float4 diffuseColour = float4(fadeColour.rgb * lightIntensity, 1.0f);

		// Optically thin water shows the refracted scene; thick water shows
		// the lit body colour. ONE blend, driven by absorption alone - the
		// old second lerp keyed on fresnel*shoreFade coupled body colour to
		// reflectance, which is why the controls fought each other.
		float3 litBody = diffuseColour.rgb + ambient.rgb * 0.35f;
		float3 waterBodyColour = lerp(litBody, worldDiffuse.rgb, transmission);
		float4 retCol = float4(waterBodyColour, 1.0f);

		// Reflections (O3): inline screen-space march for near-field content
		// + prefiltered sky-atlas fallback everywhere the march misses (off-
		// screen, behind camera, beyond march range, and the whole far sea -
		// the atlas is weather-tinted, so a storm sky reflects as OVERCAST).
		// This replaced both the legacy 24-step march and the "cheap
		// reflection" (beauty at the pixel's own position - positionally
		// meaningless, it reflected whatever was BEHIND the water).
		{
			// Reflect off a mostly-GERSTNER normal: a mirror image needs a
			// far smoother surface than shading does (per-texel bump scatter
			// sends adjacent SSR rays to unrelated targets and shreds the
			// reflection - the deferred SSR's puddle-flatten exists for the
			// same reason). 0.35 bump influence near the camera, fading to
			// PURE Gerstner with distance: far pixels cover many bump texels,
			// so any bump residue there is per-pixel ray divergence = noise.
			float3 reflectionNormal = normalize(lerp(originalWorldNormal, worldNormal, 0.35f * distantNormalFade));

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

			// Environment fallback wherever the march found nothing. The atlas
			// row follows the sea state: calm water mirrors a sharp bright
			// sky; a wind-chopped surface reflects a blurred (and naturally
			// dimmer) prefiltered row - which also takes the edge off the
			// clear-sky brightness on rippled water.
			{
				const float envRoughness = lerp(0.06f, 0.5f, seaState);
				const float3 envColour = SampleEnvAtlas(g_iblSkyEnvFwd, g_TexSamplerAniso, R, envRoughness);
				// Downward rays would pick up horizon sky the atlas has no
				// ground radiance for.
				const float envHorizon = saturate(R.y * 3.0f + 0.35f);
				const float envWeight = (1.0f - reflectionWeight) * envHorizon;

				reflection = reflection * reflectionWeight + envColour * envWeight;
				reflectionWeight = saturate(reflectionWeight + envWeight);
				reflection = reflectionWeight > 1e-4f ? reflection / reflectionWeight : float3(0.0f, 0.0f, 0.0f);
			}

			// Compose: reflection replaces body colour by FRESNEL x artist
			// strength - depth/shore terms removed from reflectance (they
			// belong to colour and see-through, not to how mirror-like the
			// surface is). The sun glint ADDS on top (its GGX F term carries
			// its own Fresnel). Foam suppresses both - scattered white water
			// is matte, not a mirror.
			// NO saturate: linear HDR into an R16G16B16A16_FLOAT target.
			const float reflectionStrength = g_oceanConfig.reflectionStrength;
			retCol.xyz = lerp(retCol.xyz, reflection,
				saturate(reflectionStrength * fresnel * reflectionWeight) * (1.0f - foam));
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
